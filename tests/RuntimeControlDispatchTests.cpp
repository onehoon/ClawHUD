#include "RuntimeControlDispatchBridge.h"
#include "RuntimeControlWireMapping.h"
#include "RuntimeControl.h"
#include "ClawHudControlProtocol.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <thread>

namespace ctl = clawhud::control;

namespace
{
int g_failures = 0;

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

// Records what the semantic boundary was asked to do and from which thread.
class FakeRuntimeControl : public clawhud::IRuntimeControl
{
public:
    // Effective state the fake reports back through GetSettingsSnapshot().
    clawhud::RuntimeSettingsSnapshot state;

    std::thread::id lastCallThread;
    int snapshotCalls{};
    bool hudEnableResult{true};
    bool opacityResult{true};
    std::optional<float> lastPreviewOpacity;
    std::optional<float> lastCommitOpacity;
    std::optional<bool> lastStartWithWindowsRequest;

    clawhud::RuntimeSettingsSnapshot GetSettingsSnapshot() const override
    {
        const_cast<FakeRuntimeControl*>(this)->lastCallThread = std::this_thread::get_id();
        const_cast<FakeRuntimeControl*>(this)->snapshotCalls++;
        return state;
    }
    void SetStartWithWindows(bool enabled) override
    {
        lastCallThread = std::this_thread::get_id();
        lastStartWithWindowsRequest = enabled;
        // Deliberately does NOT adopt `enabled` - the fake simulates a runtime
        // that rolled the request back.
    }
    bool SetHudEnabled(bool enabled) override
    {
        lastCallThread = std::this_thread::get_id();
        if (hudEnableResult) state.hudEnabled = enabled;
        return hudEnableResult;
    }
    void SetHudVisibilityMode(clawhud::HudVisibilityMode mode) override
    {
        lastCallThread = std::this_thread::get_id();
        state.hudOptions.visibilityMode = mode;
    }
    void SetHudSizeOffset(int offset) override
    {
        lastCallThread = std::this_thread::get_id();
        state.hudSizeOffset = offset;
    }
    void SetHudFont(clawhud::HudFont font) override
    {
        lastCallThread = std::this_thread::get_id();
        state.hudFont = font;
    }
    void SetHudAlignment(clawhud::HudAlignment alignment) override
    {
        lastCallThread = std::this_thread::get_id();
        state.hudOptions.alignment = alignment;
    }
    void SetHudBackgroundMode(clawhud::HudBackgroundMode mode) override
    {
        lastCallThread = std::this_thread::get_id();
        state.hudOptions.backgroundMode = mode;
    }
    bool PreviewHudOpacity(float opacity) override
    {
        lastCallThread = std::this_thread::get_id();
        lastPreviewOpacity = opacity;
        if (opacityResult) state.hudOptions.backgroundOpacity = opacity;
        return opacityResult;
    }
    bool CommitHudOpacity(float opacity) override
    {
        lastCallThread = std::this_thread::get_id();
        lastCommitOpacity = opacity;
        if (opacityResult) state.hudOptions.backgroundOpacity = opacity;
        return opacityResult;
    }
    void SetIntelVrrRangeFixEnabled(bool enabled) override
    {
        lastCallThread = std::this_thread::get_id();
        state.intelVrrRangeFixEnabled = enabled;
    }
};

clawhud::RuntimeControlMetadata Metadata()
{
    clawhud::RuntimeControlMetadata m;
    m.applicationVersion = "0.1.0";
    return m;
}

ctl::ControlRequest Request(ctl::Operation op, std::uint32_t id = 1)
{
    ctl::ControlRequest r;
    r.operation = op;
    r.requestId = id;
    return r;
}

// Drives a bridge from a real worker thread with a wake callback that runs the
// main-thread drain on this (the test's "main") thread.
struct Harness
{
    FakeRuntimeControl fake;
    clawhud::RuntimeControlDispatchBridge bridge;
    std::atomic<int> wakeCount{0};
    bool wakeSucceeds{true};

    Harness()
    {
        bridge.Start(std::this_thread::get_id(),
            // PostMessage stand-in: just records the wake and reports delivery.
            [this] { ++wakeCount; return wakeSucceeds; },
            [this](const ctl::ControlRequest& request)
            {
                return clawhud::ExecuteRuntimeControlRequest(request, fake, Metadata());
            });
    }

    // Runs the request from a worker thread while this thread acts as the
    // message loop, draining until the worker's Dispatch() has returned.
    clawhud::RuntimeControlExecutionResult FromWorkerResult(const ctl::ControlRequest& request)
    {
        clawhud::RuntimeControlExecutionResult result;
        std::atomic<bool> done{false};
        std::thread worker([&]
        {
            result = bridge.Dispatch(request);
            done = true;
        });
        while (!done.load())
        {
            bridge.DrainOnMainThread();
            std::this_thread::yield();
        }
        worker.join();
        return result;
    }

    ctl::ControlResponse FromWorker(const ctl::ControlRequest& request)
    {
        return FromWorkerResult(request).response;
    }
};

// ---- 19.1 main-thread execution --------------------------------------

void MainThreadExecution()
{
    Harness h;
    auto request = Request(ctl::Operation::SetHudEnabled);
    request.flag = true;
    const auto response = h.FromWorker(request);

    Check(response.status == ctl::ControlStatus::Ok, "worker SetHudEnabled returns Ok");
    Check(h.fake.lastCallThread == std::this_thread::get_id(),
        "IRuntimeControl executed on the main (drain) thread, not the worker");
    Check(h.fake.snapshotCalls > 0, "the worker reached a real semantic call");
}

// ---- 19.2 authoritative snapshot (not request echo) -----------------

void AuthoritativeSnapshot()
{
    Harness h;
    h.fake.state.startWithWindows = false; // effective state stays false

    auto request = Request(ctl::Operation::SetStartWithWindows);
    request.flag = true; // client asks for ON
    const auto response = h.FromWorker(request);

    Check(response.status == ctl::ControlStatus::Ok, "SetStartWithWindows returns Ok");
    Check(response.snapshot.has_value(), "response carries a snapshot");
    Check(h.fake.lastStartWithWindowsRequest == std::optional<bool>(true),
        "the requested value reached the semantic call");
    Check(response.snapshot && response.snapshot->startWithWindows == false,
        "response reflects rolled-back authoritative state, not the request");
}

// ---- 19.3 every semantic enum mapping ------------------------------

void EnumMapping()
{
    Harness h;

    struct VisibilityCase { ctl::WireVisibilityMode wire; clawhud::HudVisibilityMode semantic; };
    for (auto c : {VisibilityCase{ctl::WireVisibilityMode::Always, clawhud::HudVisibilityMode::Always},
             VisibilityCase{ctl::WireVisibilityMode::InGameOnly, clawhud::HudVisibilityMode::InGameOnly}})
    {
        auto r = Request(ctl::Operation::SetHudVisibilityMode);
        r.wireEnum = static_cast<std::uint8_t>(c.wire);
        Check(h.FromWorker(r).status == ctl::ControlStatus::Ok, "visibility maps");
        Check(h.fake.state.hudOptions.visibilityMode == c.semantic, "visibility mapped value");
    }

    struct AlignmentCase { ctl::WireAlignment wire; clawhud::HudAlignment semantic; };
    for (auto c : {AlignmentCase{ctl::WireAlignment::Left, clawhud::HudAlignment::Left},
             AlignmentCase{ctl::WireAlignment::Center, clawhud::HudAlignment::Center},
             AlignmentCase{ctl::WireAlignment::Right, clawhud::HudAlignment::Right}})
    {
        auto r = Request(ctl::Operation::SetHudAlignment);
        r.wireEnum = static_cast<std::uint8_t>(c.wire);
        Check(h.FromWorker(r).status == ctl::ControlStatus::Ok, "alignment maps");
        Check(h.fake.state.hudOptions.alignment == c.semantic, "alignment mapped value");
    }

    struct FontCase { ctl::WireFont wire; clawhud::HudFont semantic; };
    for (auto c : {FontCase{ctl::WireFont::Unispace, clawhud::HudFont::Unispace},
             FontCase{ctl::WireFont::SegoeUiVariable, clawhud::HudFont::SegoeUiVariable}})
    {
        auto r = Request(ctl::Operation::SetHudFont);
        r.wireEnum = static_cast<std::uint8_t>(c.wire);
        Check(h.FromWorker(r).status == ctl::ControlStatus::Ok, "font maps");
        Check(h.fake.state.hudFont == c.semantic, "font mapped value");
    }

    struct BackgroundCase { ctl::WireBackgroundMode wire; clawhud::HudBackgroundMode semantic; };
    for (auto c : {BackgroundCase{ctl::WireBackgroundMode::FullWidth, clawhud::HudBackgroundMode::FullWidth},
             BackgroundCase{ctl::WireBackgroundMode::ContentWidth, clawhud::HudBackgroundMode::ContentWidth}})
    {
        auto r = Request(ctl::Operation::SetHudBackgroundMode);
        r.wireEnum = static_cast<std::uint8_t>(c.wire);
        Check(h.FromWorker(r).status == ctl::ControlStatus::Ok, "background maps");
        Check(h.fake.state.hudOptions.backgroundMode == c.semantic, "background mapped value");
    }

    // Unknown wire enum value -> InvalidValue, no semantic call side effect.
    auto bad = Request(ctl::Operation::SetHudFont);
    bad.wireEnum = 7;
    Check(h.FromWorker(bad).status == ctl::ControlStatus::InvalidValue,
        "unknown wire enum returns InvalidValue");
}

void IntelVrrStatusMapping()
{
    struct Case { clawhud::IntelVrrRunStatus semantic; ctl::WireIntelVrrStatus wire; };
    const Case cases[] = {
        {clawhud::IntelVrrRunStatus::Disabled, ctl::WireIntelVrrStatus::Disabled},
        {clawhud::IntelVrrRunStatus::Unavailable, ctl::WireIntelVrrStatus::Unavailable},
        {clawhud::IntelVrrRunStatus::UnsupportedPanel, ctl::WireIntelVrrStatus::UnsupportedPanel},
        {clawhud::IntelVrrRunStatus::AmbiguousDisplay, ctl::WireIntelVrrStatus::AmbiguousDisplay},
        {clawhud::IntelVrrRunStatus::AlreadyCorrect, ctl::WireIntelVrrStatus::AlreadyCorrect},
        {clawhud::IntelVrrRunStatus::SkippedUserProfile, ctl::WireIntelVrrStatus::SkippedUserProfile},
        {clawhud::IntelVrrRunStatus::Applied, ctl::WireIntelVrrStatus::Applied},
        {clawhud::IntelVrrRunStatus::ApplyFailed, ctl::WireIntelVrrStatus::ApplyFailed},
        {clawhud::IntelVrrRunStatus::VerificationFailed, ctl::WireIntelVrrStatus::VerificationFailed},
    };
    for (const auto& c : cases)
    {
        Harness h;
        clawhud::IntelVrrRunResult result;
        result.status = c.semantic;
        result.panelName = "MSI Claw";
        result.rangeBefore = "48-120";
        result.rangeAfter = "1-120";
        result.message = "";
        result.timestampUtc = "2026-09-02T00:00:00Z";
        h.fake.state.intelVrrLastResult = result;

        const auto response = h.FromWorker(Request(ctl::Operation::GetSettingsSnapshot));
        Check(response.status == ctl::ControlStatus::Ok, "snapshot with VRR result Ok");
        Check(response.snapshot && response.snapshot->intelVrrLastResult &&
            response.snapshot->intelVrrLastResult->status == static_cast<std::uint8_t>(c.wire),
            "Intel VRR status mapped to the explicit wire value");
    }
}

// ---- 19.4 opacity preview vs commit -----------------------------

void OpacityPreviewCommit()
{
    {
        Harness h;
        auto r = Request(ctl::Operation::PreviewHudOpacity);
        r.opacityPercent = 70;
        Check(h.FromWorker(r).status == ctl::ControlStatus::Ok, "preview Ok");
        Check(h.fake.lastPreviewOpacity.has_value() &&
            *h.fake.lastPreviewOpacity > 0.699f && *h.fake.lastPreviewOpacity < 0.701f,
            "preview passes 0.70f to the semantic call");
        Check(!h.fake.lastCommitOpacity.has_value(), "preview does not invoke commit");
    }
    {
        Harness h;
        auto r = Request(ctl::Operation::CommitHudOpacity);
        r.opacityPercent = 70;
        Check(h.FromWorker(r).status == ctl::ControlStatus::Ok, "commit Ok");
        Check(h.fake.lastCommitOpacity.has_value() &&
            *h.fake.lastCommitOpacity > 0.699f && *h.fake.lastCommitOpacity < 0.701f,
            "commit passes 0.70f to the semantic call");
        Check(!h.fake.lastPreviewOpacity.has_value(), "commit does not invoke preview");
    }
    {
        Harness h;
        h.fake.opacityResult = false;
        auto r = Request(ctl::Operation::CommitHudOpacity);
        r.opacityPercent = 80;
        const auto response = h.FromWorker(r);
        Check(response.status == ctl::ControlStatus::OperationFailed,
            "semantic opacity failure returns OperationFailed");
        Check(!response.snapshot.has_value(), "opacity failure carries no snapshot");
    }
    {
        Harness h;
        auto r = Request(ctl::Operation::PreviewHudOpacity);
        r.opacityPercent = 53; // not a 5% step
        Check(h.FromWorker(r).status == ctl::ControlStatus::InvalidValue,
            "out-of-step opacity returns InvalidValue");
    }
}

// ---- 19.5 HUD enable failure ---------------------------------

void HudEnableFailure()
{
    Harness h;
    h.fake.hudEnableResult = false;
    auto r = Request(ctl::Operation::SetHudEnabled);
    r.flag = true;
    const auto response = h.FromWorker(r);
    Check(response.status == ctl::ControlStatus::OperationFailed,
        "SetHudEnabled failure returns OperationFailed");
    Check(!response.snapshot.has_value(), "enable failure carries no snapshot");
}

// ---- 19.6 runtime info -------------------------------------

void RuntimeInfo()
{
    Harness h;
    const auto response = h.FromWorker(Request(ctl::Operation::GetRuntimeInfo));
    Check(response.status == ctl::ControlStatus::Ok, "GetRuntimeInfo Ok");
    Check(response.runtimeInfo.has_value(), "runtime info present");
    if (response.runtimeInfo)
    {
        const auto& info = *response.runtimeInfo;
        Check(info.minimumProtocolVersion == 1 && info.maximumProtocolVersion == 1,
            "protocol range 1..1");
        Check(info.launchMode == static_cast<std::uint8_t>(ctl::WireLaunchMode::Standalone),
            "launch mode Standalone");
        Check(info.runtimeState == static_cast<std::uint8_t>(ctl::WireRuntimeState::Ready),
            "runtime state Ready");
        Check(!info.applicationVersion.empty(), "application version non-empty");
    }
}

// ---- 17.1 RequestShutdown approval (Ok + shutdownAfterResponse) --------

void RequestShutdownApproval()
{
    Harness h;
    const auto result = h.FromWorkerResult(Request(ctl::Operation::RequestShutdown, 7));
    Check(result.response.status == ctl::ControlStatus::Ok,
        "RequestShutdown is approved with Ok");
    Check(result.response.requestId == 7, "RequestShutdown requestId preserved");
    Check(!result.response.snapshot.has_value() && !result.response.runtimeInfo.has_value(),
        "RequestShutdown response payload is empty");
    Check(result.shutdownAfterResponse, "RequestShutdown arms shutdownAfterResponse");
    // The bridge/mapping never invokes any exit; that is the pipe server's job.

    // Every non-shutdown operation leaves the flag false.
    const auto info = h.FromWorkerResult(Request(ctl::Operation::GetRuntimeInfo));
    Check(!info.shutdownAfterResponse, "GetRuntimeInfo does not arm shutdown");
}

// ---- 19.8 shutdown cancellation --------------------------

void ShutdownCancellation()
{
    FakeRuntimeControl fake;
    clawhud::RuntimeControlDispatchBridge bridge;
    std::atomic<bool> submitted{false};

    // Wake that never drains: the request sits queued until Stop().
    bridge.Start(std::thread::id{} /* no real main thread */,
        [&] { submitted = true; return true; },
        [&](const ctl::ControlRequest& request)
        { return clawhud::ExecuteRuntimeControlRequest(request, fake, Metadata()); });

    clawhud::RuntimeControlExecutionResult result;
    std::thread worker([&] { result = bridge.Dispatch(Request(ctl::Operation::GetSettingsSnapshot)); });

    while (!submitted.load())
        std::this_thread::yield();
    bridge.Stop();
    worker.join();

    Check(result.response.status == ctl::ControlStatus::ShuttingDown,
        "pending request completes with ShuttingDown when the bridge stops");
    Check(!result.shutdownAfterResponse, "Stop cancellation does not arm shutdown");
}

// ---- 19.9 submission after stop ----------------------------

void SubmissionAfterStop()
{
    Harness h;
    h.bridge.Stop();
    const int wakesBefore = h.wakeCount.load();
    const auto result = h.bridge.Dispatch(Request(ctl::Operation::GetSettingsSnapshot));
    Check(result.response.status == ctl::ControlStatus::ShuttingDown,
        "dispatch after stop returns ShuttingDown immediately");
    Check(h.wakeCount.load() == wakesBefore, "dispatch after stop does not wake the main thread");
    Check(!h.bridge.Accepting(), "bridge is not accepting after stop");
}

// ---- 19.10 wake failure -----------------------------------

void WakeFailure()
{
    Harness h;
    h.wakeSucceeds = false;
    // Mimic production: a failed PostMessage means the main loop never drains,
    // so there is no concurrent drain - just the worker and Dispatch's own
    // failure completion.
    clawhud::RuntimeControlExecutionResult result;
    std::thread worker([&] { result = h.bridge.Dispatch(Request(ctl::Operation::GetSettingsSnapshot)); });
    worker.join(); // hangs instead of failing if the waiter never completes
    Check(result.response.status == ctl::ControlStatus::RuntimeUnavailable,
        "a failed wake completes the waiter with RuntimeUnavailable");
    Check(h.fake.snapshotCalls == 0, "a failed wake never reaches the semantic call");
}

// ---- 19.11 main-thread self-dispatch -----------------------

void SelfDispatch()
{
    Harness h; // constructed on this thread => this is the registered main thread
    auto r = Request(ctl::Operation::SetHudSizeOffset);
    r.sizeOffset = 1;
    const auto result = h.bridge.Dispatch(r); // same thread, must not deadlock
    Check(result.response.status == ctl::ControlStatus::Ok, "self-dispatch runs synchronously");
    Check(h.fake.state.hudSizeOffset == 1, "self-dispatch reached the semantic call");
    Check(h.wakeCount.load() == 0, "self-dispatch does not post a wake message");
}
}

int main()
{
    MainThreadExecution();
    AuthoritativeSnapshot();
    EnumMapping();
    IntelVrrStatusMapping();
    OpacityPreviewCommit();
    HudEnableFailure();
    RuntimeInfo();
    RequestShutdownApproval();
    ShutdownCancellation();
    SubmissionAfterStop();
    WakeFailure();
    SelfDispatch();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "RuntimeControlDispatchTests: all checks passed\n";
    return EXIT_SUCCESS;
}
