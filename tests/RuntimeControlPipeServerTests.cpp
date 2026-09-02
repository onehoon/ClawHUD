#include "RuntimeControlPipeServer.h"
#include "RuntimeControlPipeSecurity.h"
#include "RuntimeControlDispatchBridge.h"
#include "RuntimeControlWireMapping.h"
#include "RuntimeControl.h"
#include "RuntimeControlExecutionResult.h"
#include "ClawHudControlCodec.h"
#include "ClawHudControlProtocol.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ctl = clawhud::control;
using Bytes = std::vector<std::uint8_t>;
using clawhud::RuntimeControlExecutionResult;
using clawhud::RuntimeControlPipeServer;

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

std::wstring TestPipeName()
{
    static std::atomic<int> counter{0};
    return L"\\\\.\\pipe\\ClawHUD.Control.test." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(counter.fetch_add(1));
}

ctl::ControlRequest EmptyRequest(ctl::Operation op, std::uint32_t id)
{
    ctl::ControlRequest r;
    r.operation = op;
    r.requestId = id;
    return r;
}

Bytes Encode(const ctl::ControlRequest& request)
{
    auto frame = ctl::EncodeControlRequest(request);
    Check(frame.has_value(), "test helper: request encodes");
    return frame.value_or(Bytes{});
}

RuntimeControlExecutionResult Exec(ctl::ControlResponse response, bool shutdown = false)
{
    return {std::move(response), shutdown};
}

ctl::ControlResponse OkRuntimeInfoResponse(const ctl::ControlRequest& request)
{
    ctl::ControlResponse response;
    response.operationId = static_cast<std::uint16_t>(request.operation);
    response.requestId = request.requestId;
    response.status = ctl::ControlStatus::Ok;
    ctl::WireRuntimeInfo info;
    info.applicationVersion = "0.1.0";
    info.minimumProtocolVersion = 1;
    info.maximumProtocolVersion = 1;
    info.launchMode = static_cast<std::uint8_t>(ctl::WireLaunchMode::Standalone);
    info.runtimeState = static_cast<std::uint8_t>(ctl::WireRuntimeState::Ready);
    response.runtimeInfo = info;
    return response;
}

ctl::WireSettingsSnapshot SampleSnapshot()
{
    ctl::WireSettingsSnapshot s;
    s.hudSizeOffset = 1;
    s.hudFont = static_cast<std::uint8_t>(ctl::WireFont::Unispace);
    s.visibilityMode = static_cast<std::uint8_t>(ctl::WireVisibilityMode::Always);
    s.alignment = static_cast<std::uint8_t>(ctl::WireAlignment::Center);
    s.backgroundMode = static_cast<std::uint8_t>(ctl::WireBackgroundMode::ContentWidth);
    s.backgroundOpacityPercent = 70;
    return s;
}

HANDLE ConnectClient(const std::wstring& name)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        const HANDLE handle = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle != INVALID_HANDLE_VALUE)
        {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(handle, &mode, nullptr, nullptr);
            return handle;
        }
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY)
            return INVALID_HANDLE_VALUE;
        Sleep(10);
    }
    return INVALID_HANDLE_VALUE;
}

// One request -> one response, or nullopt if the server sent nothing.
std::optional<Bytes> Roundtrip(const std::wstring& name, const Bytes& request)
{
    const HANDLE handle = ConnectClient(name);
    if (handle == INVALID_HANDLE_VALUE)
        return std::nullopt;

    DWORD written{};
    WriteFile(handle, request.data(), static_cast<DWORD>(request.size()), &written, nullptr);

    std::array<std::uint8_t, ctl::kMaxFrameBytes> buffer{};
    DWORD read{};
    const BOOL ok = ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
        &read, nullptr);
    CloseHandle(handle);
    if (!ok || read == 0)
        return std::nullopt;
    return Bytes(buffer.begin(), buffer.begin() + read);
}

std::optional<ctl::ControlResponse> RoundtripDecoded(const std::wstring& name, const Bytes& request)
{
    const auto raw = Roundtrip(name, request);
    if (!raw)
        return std::nullopt;
    const auto decoded = ctl::DecodeControlResponse(*raw);
    if (!decoded.ok)
        return std::nullopt;
    return decoded.value;
}

// Records what the semantic boundary was asked to do and from which thread.
class FakeRuntimeControl : public clawhud::IRuntimeControl
{
public:
    clawhud::RuntimeSettingsSnapshot state;
    std::atomic<std::thread::id> lastCallThread{};
    std::optional<bool> lastStartWithWindows;
    std::optional<float> lastPreview;
    std::optional<float> lastCommit;
    float persistedOpacity{0.70f};
    bool hudEnableResult{true};
    bool opacityResult{true};

    clawhud::RuntimeSettingsSnapshot GetSettingsSnapshot() const override
    {
        const_cast<FakeRuntimeControl*>(this)->lastCallThread = std::this_thread::get_id();
        return state;
    }
    void SetStartWithWindows(bool enabled) override
    {
        lastCallThread = std::this_thread::get_id();
        lastStartWithWindows = enabled; // deliberately does NOT adopt it (rollback sim)
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
        lastPreview = opacity;
        if (opacityResult) state.hudOptions.backgroundOpacity = opacity; // no persist
        return opacityResult;
    }
    bool CommitHudOpacity(float opacity) override
    {
        lastCallThread = std::this_thread::get_id();
        lastCommit = opacity;
        if (opacityResult)
        {
            state.hudOptions.backgroundOpacity = opacity;
            persistedOpacity = opacity;
        }
        return opacityResult;
    }
    void SetIntelVrrRangeFixEnabled(bool enabled) override
    {
        lastCallThread = std::this_thread::get_id();
        state.intelVrrRangeFixEnabled = enabled;
    }
};

// Real bridge + real mapping + fake IRuntimeControl, drained on a dedicated
// "main" thread. Pipe server dispatch callback -> bridge.Dispatch.
struct EndToEnd
{
    FakeRuntimeControl fake;
    clawhud::RuntimeControlDispatchBridge bridge;
    std::thread drain;
    std::atomic<bool> running{true};
    std::atomic<bool> started{false};
    std::thread::id mainThreadId;

    EndToEnd()
    {
        drain = std::thread([this]
        {
            mainThreadId = std::this_thread::get_id();
            bridge.Start(mainThreadId, [] { return true; },
                [this](const ctl::ControlRequest& request)
                {
                    clawhud::RuntimeControlMetadata metadata;
                    metadata.applicationVersion = "0.1.0";
                    return clawhud::ExecuteRuntimeControlRequest(request, fake, metadata);
                });
            started = true;
            while (running.load())
            {
                bridge.DrainOnMainThread();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            bridge.DrainOnMainThread();
        });
        while (!started.load())
            std::this_thread::yield();
    }
    ~EndToEnd()
    {
        running = false;
        if (drain.joinable())
            drain.join();
        bridge.Stop();
    }
    RuntimeControlPipeServer::DispatchCallback Callback()
    {
        return [this](const ctl::ControlRequest& request) { return bridge.Dispatch(request); };
    }
};

// ---- 18.1 endpoint derivation --------------------------------------

void EndpointDerivation()
{
    const auto first = ctl::ControlPipeName();
    const auto second = ctl::ControlPipeName();
    Check(first.has_value() && first == second, "production pipe name is deterministic");
    const auto session = ctl::CurrentProcessSessionId();
    Check(session.has_value(), "session id resolves");
    if (first && session)
    {
        Check(*first == L"\\\\.\\pipe\\ClawHUD.Control." + std::to_wstring(*session),
            "pipe name is \\\\.\\pipe\\ClawHUD.Control.<sessionId>");
        Check(first->find(L"test") == std::wstring::npos, "production name has no test suffix");
    }
}

// ---- 18.13 security descriptor -------------------------------------

void SecurityDescriptor()
{
    ctl::ControlPipeSecurity security;
    Check(security.Build(), "security descriptor builds");
    Check(security.Attributes() != nullptr, "security attributes available after build");
    const std::wstring sddl = security.Sddl();
    const auto sid = ctl::CurrentUserSidString();
    Check(sid.has_value(), "current user SID resolves");
    Check(sddl.rfind(L"D:P(A;;GA;;;", 0) == 0, "DACL is protected, current-user full access");
    if (sid)
        Check(sddl.find(*sid) != std::wstring::npos, "DACL names the current user SID");
    Check(sddl.find(L"S-1-1-0") == std::wstring::npos, "DACL does not grant Everyone");
    Check(sddl.find(L"S-1-5-11") == std::wstring::npos, "DACL does not grant Authenticated Users");
    Check(sddl.find(L"S-1-5-32-545") == std::wstring::npos, "DACL does not grant BUILTIN\\Users");
    Check(sddl.find(L";BU)") == std::wstring::npos && sddl.find(L";AU)") == std::wstring::npos &&
            sddl.find(L";WD)") == std::wstring::npos,
        "DACL does not grant BU/AU/WD aliases");
}

// ---- 18.14 session gate helper -----------------------------------

void SessionGate()
{
    Check(ctl::SessionsMatch(1, 1), "same session allowed");
    Check(!ctl::SessionsMatch(1, 2), "different session rejected");
}

// ---- 18.2 / 18.3 read-only round trips (fake dispatch) ---------

void ReadOnlyRoundTrips()
{
    RuntimeControlPipeServer server;
    std::atomic<int> dispatchCount{0};
    const std::wstring name = TestPipeName();
    Check(server.Start(
              [&](const ctl::ControlRequest& request) -> RuntimeControlExecutionResult
              {
                  dispatchCount.fetch_add(1);
                  if (request.operation == ctl::Operation::GetRuntimeInfo)
                      return Exec(OkRuntimeInfoResponse(request));
                  ctl::ControlResponse response;
                  response.operationId = static_cast<std::uint16_t>(request.operation);
                  response.requestId = request.requestId;
                  response.status = ctl::ControlStatus::Ok;
                  response.snapshot = SampleSnapshot();
                  return Exec(std::move(response));
              },
              {}, name),
        "pipe server starts on a test endpoint");

    const auto info = RoundtripDecoded(name, Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 101)));
    Check(info && info->status == ctl::ControlStatus::Ok && info->requestId == 101 &&
            info->runtimeInfo.has_value(),
        "GetRuntimeInfo round trip");

    const auto snap = RoundtripDecoded(name,
        Encode(EmptyRequest(ctl::Operation::GetSettingsSnapshot, 202)));
    Check(snap && snap->status == ctl::ControlStatus::Ok && snap->snapshot.has_value(),
        "GetSettingsSnapshot round trip");
    Check(dispatchCount.load() == 2, "each read-only request reached dispatch once");
    server.Stop();
}

// ---- 17.2 / 17.3 / 17.4 external mutation round trips (end to end) --

ctl::ControlRequest EnumReq(ctl::Operation op, std::uint8_t wire, std::uint32_t id)
{
    ctl::ControlRequest r;
    r.operation = op;
    r.requestId = id;
    r.wireEnum = wire;
    return r;
}
ctl::ControlRequest OpacityReq(ctl::Operation op, std::uint16_t pct, std::uint32_t id)
{
    ctl::ControlRequest r;
    r.operation = op;
    r.requestId = id;
    r.opacityPercent = pct;
    return r;
}

void ExternalMutations()
{
    EndToEnd e2e;
    RuntimeControlPipeServer server;
    const std::wstring name = TestPipeName();
    Check(server.Start(e2e.Callback(), {}, name), "end-to-end pipe server starts");

    struct Case { ctl::ControlRequest request; };
    std::vector<ctl::ControlRequest> requests;
    { ctl::ControlRequest r; r.operation = ctl::Operation::SetStartWithWindows; r.requestId = 1; r.flag = true; requests.push_back(r); }
    { ctl::ControlRequest r; r.operation = ctl::Operation::SetHudEnabled; r.requestId = 2; r.flag = true; requests.push_back(r); }
    requests.push_back(EnumReq(ctl::Operation::SetHudVisibilityMode,
        static_cast<std::uint8_t>(ctl::WireVisibilityMode::InGameOnly), 3));
    { ctl::ControlRequest r; r.operation = ctl::Operation::SetHudSizeOffset; r.requestId = 4; r.sizeOffset = -2; requests.push_back(r); }
    requests.push_back(EnumReq(ctl::Operation::SetHudFont,
        static_cast<std::uint8_t>(ctl::WireFont::SegoeUiVariable), 5));
    requests.push_back(EnumReq(ctl::Operation::SetHudAlignment,
        static_cast<std::uint8_t>(ctl::WireAlignment::Right), 6));
    requests.push_back(EnumReq(ctl::Operation::SetHudBackgroundMode,
        static_cast<std::uint8_t>(ctl::WireBackgroundMode::FullWidth), 7));
    requests.push_back(OpacityReq(ctl::Operation::PreviewHudOpacity, 60, 8));
    requests.push_back(OpacityReq(ctl::Operation::CommitHudOpacity, 90, 9));
    { ctl::ControlRequest r; r.operation = ctl::Operation::SetIntelVrrRangeFixEnabled; r.requestId = 10; r.flag = true; requests.push_back(r); }

    for (const auto& request : requests)
    {
        const auto response = RoundtripDecoded(name, Encode(request));
        Check(response && response->status == ctl::ControlStatus::Ok,
            "mutation round trip returns Ok");
        Check(response && response->requestId == request.requestId, "mutation requestId preserved");
        Check(response && response->snapshot.has_value(), "mutation returns authoritative snapshot");
    }
    Check(e2e.fake.lastCallThread.load() == e2e.mainThreadId,
        "mutations executed on the drain/main thread, not the pipe worker");

    // 17.4: opacity preview vs commit routed to distinct semantic calls.
    Check(e2e.fake.lastPreview.has_value() && *e2e.fake.lastPreview > 0.599f &&
            *e2e.fake.lastPreview < 0.601f,
        "PreviewHudOpacity(60) -> 0.60f");
    Check(e2e.fake.lastCommit.has_value() && *e2e.fake.lastCommit > 0.899f &&
            *e2e.fake.lastCommit < 0.901f,
        "CommitHudOpacity(90) -> 0.90f");
    Check(e2e.fake.persistedOpacity > 0.899f, "only commit changed the persisted value");

    server.Stop();
}

// ---- 17.3 authoritative rollback / failure ---------------------

void AuthoritativeAndFailure()
{
    {
        EndToEnd e2e;
        e2e.fake.state.startWithWindows = false; // effective state stays false
        RuntimeControlPipeServer server;
        const std::wstring name = TestPipeName();
        server.Start(e2e.Callback(), {}, name);

        ctl::ControlRequest request;
        request.operation = ctl::Operation::SetStartWithWindows;
        request.requestId = 1;
        request.flag = true;
        const auto response = RoundtripDecoded(name, Encode(request));
        Check(response && response->status == ctl::ControlStatus::Ok, "rollback case Ok");
        Check(response && response->snapshot && response->snapshot->startWithWindows == false,
            "response reflects rolled-back authoritative state, not the request");
        server.Stop();
    }
    {
        EndToEnd e2e;
        e2e.fake.hudEnableResult = false;
        RuntimeControlPipeServer server;
        const std::wstring name = TestPipeName();
        server.Start(e2e.Callback(), {}, name);
        ctl::ControlRequest request;
        request.operation = ctl::Operation::SetHudEnabled;
        request.requestId = 1;
        request.flag = true;
        const auto response = RoundtripDecoded(name, Encode(request));
        Check(response && response->status == ctl::ControlStatus::OperationFailed,
            "SetHudEnabled failure -> OperationFailed");
        Check(response && !response->snapshot.has_value(), "enable failure carries no snapshot");
        server.Stop();
    }
    {
        EndToEnd e2e;
        e2e.fake.opacityResult = false;
        RuntimeControlPipeServer server;
        const std::wstring name = TestPipeName();
        server.Start(e2e.Callback(), {}, name);
        const auto response = RoundtripDecoded(name,
            Encode(OpacityReq(ctl::Operation::CommitHudOpacity, 80, 1)));
        Check(response && response->status == ctl::ControlStatus::OperationFailed,
            "opacity failure -> OperationFailed");
        server.Stop();
    }
    {
        EndToEnd e2e;
        RuntimeControlPipeServer server;
        const std::wstring name = TestPipeName();
        server.Start(e2e.Callback(), {}, name);
        // 53% is not a 5% step -> InvalidValue from the mapper.
        ctl::ControlRequest request;
        request.operation = ctl::Operation::PreviewHudOpacity;
        request.requestId = 1;
        request.opacityPercent = 55;
        Bytes frame = Encode(request);
        frame[24] = 53; // payload u16 low byte
        const auto response = RoundtripDecoded(name, frame);
        Check(response && response->status == ctl::ControlStatus::InvalidValue,
            "out-of-step opacity -> InvalidValue");
        server.Stop();
    }
}

// ---- 17.5 RequestShutdown response before shutdown-ready callback ---

void RequestShutdownOrdering()
{
    EndToEnd e2e;
    RuntimeControlPipeServer server;
    const std::wstring name = TestPipeName();

    std::atomic<int> shutdownCallbackCount{0};
    std::atomic<bool> responseConsumed{false};

    server.Start(e2e.Callback(),
        [&]() -> bool
        {
            Check(responseConsumed.load(),
                "shutdown-ready callback fires only after the response is delivered");
            shutdownCallbackCount.fetch_add(1);
            return true; // do not actually post anything in the test
        },
        name);

    const HANDLE handle = ConnectClient(name);
    Check(handle != INVALID_HANDLE_VALUE, "shutdown-test client connects");
    if (handle != INVALID_HANDLE_VALUE)
    {
        const Bytes frame = Encode(EmptyRequest(ctl::Operation::RequestShutdown, 321));
        DWORD written{};
        WriteFile(handle, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr);

        std::array<std::uint8_t, ctl::kMaxFrameBytes> buffer{};
        DWORD read{};
        const BOOL ok = ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
            &read, nullptr);
        Check(ok && read > 0, "client reads the RequestShutdown response");
        if (ok && read > 0)
        {
            const auto decoded = ctl::DecodeControlResponse(Bytes(buffer.begin(), buffer.begin() + read));
            Check(decoded.ok && decoded.value.status == ctl::ControlStatus::Ok,
                "RequestShutdown response is Ok");
            Check(decoded.value.requestId == 321, "RequestShutdown requestId preserved");
            Check(!decoded.value.snapshot.has_value() && !decoded.value.runtimeInfo.has_value(),
                "RequestShutdown response payload is empty");
        }
        responseConsumed = true;
        CloseHandle(handle);
    }

    // Give the worker time to complete its drain and fire the callback.
    for (int i = 0; i < 200 && shutdownCallbackCount.load() == 0; ++i)
        Sleep(5);
    Check(shutdownCallbackCount.load() == 1, "shutdown-ready callback fired exactly once");

    server.Stop();
}

// ---- 17.6 failed response does not arm shutdown -------------------

void FailedShutdownResponseDoesNotArm()
{
    RuntimeControlPipeServer server;
    const std::wstring name = TestPipeName();
    std::atomic<int> shutdownCallbackCount{0};

    // Dispatch returns shutdownAfterResponse=true but with a response the codec
    // refuses to encode (Ok status + unknown raw operation id).
    server.Start(
        [](const ctl::ControlRequest& request) -> RuntimeControlExecutionResult
        {
            ctl::ControlResponse response;
            response.operationId = 250; // unknown -> EncodeControlResponse refuses an Ok
            response.requestId = request.requestId;
            response.status = ctl::ControlStatus::Ok;
            return {response, true};
        },
        [&] { shutdownCallbackCount.fetch_add(1); return true; },
        name);

    const auto raw = Roundtrip(name, Encode(EmptyRequest(ctl::Operation::RequestShutdown, 1)));
    Check(!raw.has_value(), "no response is sent when encoding the shutdown ack fails");
    for (int i = 0; i < 40; ++i)
        Sleep(5);
    Check(shutdownCallbackCount.load() == 0,
        "a failed shutdown response never arms the shutdown-ready callback");
    server.Stop();
}

// ---- 18.5 unknown operation correlation --------------------------

void UnknownOperationCorrelation()
{
    RuntimeControlPipeServer server;
    const std::wstring name = TestPipeName();
    server.Start([](const ctl::ControlRequest&) { return RuntimeControlExecutionResult{}; }, {}, name);

    Bytes frame = Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 42));
    frame[10] = 250; // unknown operation

    const auto response = RoundtripDecoded(name, frame);
    Check(response && response->status == ctl::ControlStatus::UnknownOperation,
        "unknown operation -> UnknownOperation");
    Check(response && response->operationId == 250 && response->requestId == 42,
        "raw operationId + requestId echoed");
    server.Stop();
}

// ---- 18.6 invalid known value correlation -----------------------

void InvalidValueCorrelation()
{
    RuntimeControlPipeServer server;
    const std::wstring name = TestPipeName();
    server.Start([](const ctl::ControlRequest&) { return RuntimeControlExecutionResult{}; }, {}, name);

    Bytes frame = Encode(EnumReq(ctl::Operation::SetHudFont,
        static_cast<std::uint8_t>(ctl::WireFont::Unispace), 77));
    frame[24] = 9; // invalid font value

    const auto response = RoundtripDecoded(name, frame);
    Check(response && response->status == ctl::ControlStatus::InvalidValue,
        "invalid known value -> InvalidValue");
    Check(response && response->requestId == 77, "requestId preserved on InvalidValue");
    server.Stop();
}

// ---- 18.7 uncorrelatable malformed frame ------------------------

void UncorrelatableMalformed()
{
    RuntimeControlPipeServer server;
    std::atomic<int> dispatchCount{0};
    const std::wstring name = TestPipeName();
    server.Start(
        [&](const ctl::ControlRequest&) -> RuntimeControlExecutionResult
        {
            dispatchCount.fetch_add(1);
            return Exec(OkRuntimeInfoResponse(EmptyRequest(ctl::Operation::GetRuntimeInfo, 1)));
        },
        {}, name);

    Bytes bad = Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 5));
    bad[0] = 'X';
    Check(!Roundtrip(name, bad).has_value(), "bad-magic frame gets no fabricated response");

    const auto good = RoundtripDecoded(name, Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 6)));
    Check(good && good->status == ctl::ControlStatus::Ok, "server stays alive after a malformed frame");
    Check(dispatchCount.load() == 1, "only the valid frame was dispatched");
    server.Stop();
}

// ---- 18.8 oversized pipe message ------------------------------

void OversizedMessage()
{
    RuntimeControlPipeServer server;
    std::atomic<int> dispatchCount{0};
    const std::wstring name = TestPipeName();
    server.Start(
        [&](const ctl::ControlRequest&) -> RuntimeControlExecutionResult
        {
            dispatchCount.fetch_add(1);
            return Exec(OkRuntimeInfoResponse(EmptyRequest(ctl::Operation::GetRuntimeInfo, 1)));
        },
        {}, name);

    const HANDLE handle = ConnectClient(name);
    Check(handle != INVALID_HANDLE_VALUE, "oversized-test client connects");
    if (handle != INVALID_HANDLE_VALUE)
    {
        Bytes huge(ctl::kMaxFrameBytes + 512, 0xAB);
        DWORD written{};
        WriteFile(handle, huge.data(), static_cast<DWORD>(huge.size()), &written, nullptr);
        std::array<std::uint8_t, 256> buffer{};
        DWORD read{};
        const BOOL ok = ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
            &read, nullptr);
        Check(!ok || read == 0, "oversized message gets no response");
        CloseHandle(handle);
    }
    Check(dispatchCount.load() == 0, "oversized message never dispatched");
    const auto good = RoundtripDecoded(name, Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 9)));
    Check(good.has_value(), "server accepts a valid client after an oversized message");
    server.Stop();
}

// ---- 18.9 / 18.10 stop lifecycle -----------------------------

void StopWhileWaitingForConnection()
{
    RuntimeControlPipeServer server;
    server.Start([](const ctl::ControlRequest&) { return RuntimeControlExecutionResult{}; }, {},
        TestPipeName());
    Check(server.Running(), "server running with no client");
    server.Stop();
    Check(!server.Running(), "server stopped with no client");
}

void StopWhileClientIdle()
{
    RuntimeControlPipeServer server;
    const std::wstring name = TestPipeName();
    server.Start([](const ctl::ControlRequest&) { return RuntimeControlExecutionResult{}; }, {}, name);
    const HANDLE handle = ConnectClient(name);
    Check(handle != INVALID_HANDLE_VALUE, "idle client connects");
    Sleep(30);
    server.Stop();
    Check(!server.Running(), "server stopped while a client was connected and idle");
    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
}

// ---- 18.12 restart on the same endpoint -----------------------

void RestartOnSameEndpoint()
{
    const std::wstring name = TestPipeName();
    {
        RuntimeControlPipeServer server;
        Check(server.Start([](const ctl::ControlRequest& r) { return Exec(OkRuntimeInfoResponse(r)); },
                  {}, name),
            "first server starts");
        server.Stop();
    }
    {
        RuntimeControlPipeServer server;
        Check(server.Start([](const ctl::ControlRequest& r) { return Exec(OkRuntimeInfoResponse(r)); },
                  {}, name),
            "restarted server starts on the same endpoint");
        const auto response = RoundtripDecoded(name,
            Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 1)));
        Check(response && response->status == ctl::ControlStatus::Ok,
            "restarted server serves a valid request");
        server.Stop();
    }
}

// ---- 18.11 dispatch wait + shutdown ordering ------------------

void DispatchWaitShutdownOrdering()
{
    clawhud::RuntimeControlDispatchBridge bridge;
    bridge.Start(std::thread::id{}, [] { return true; },
        [](const ctl::ControlRequest& request)
        {
            ctl::ControlResponse response;
            response.operationId = static_cast<std::uint16_t>(request.operation);
            response.requestId = request.requestId;
            response.status = ctl::ControlStatus::Ok;
            response.snapshot = SampleSnapshot();
            return RuntimeControlExecutionResult{response, false};
        });

    RuntimeControlPipeServer server;
    const std::wstring name = TestPipeName();
    server.Start([&](const ctl::ControlRequest& request) { return bridge.Dispatch(request); }, {}, name);

    std::atomic<bool> clientDone{false};
    std::thread client([&]
    {
        Roundtrip(name, Encode(EmptyRequest(ctl::Operation::GetSettingsSnapshot, 1)));
        clientDone = true;
    });
    Sleep(50);

    bridge.Stop();  // production order: bridge first
    server.Stop();
    client.join();

    Check(!server.Running(), "server joined without deadlock during shutdown ordering");
    Check(clientDone.load(), "client thread released");
}
}

int main()
{
    EndpointDerivation();
    SecurityDescriptor();
    SessionGate();
    ReadOnlyRoundTrips();
    ExternalMutations();
    AuthoritativeAndFailure();
    RequestShutdownOrdering();
    FailedShutdownResponseDoesNotArm();
    UnknownOperationCorrelation();
    InvalidValueCorrelation();
    UncorrelatableMalformed();
    OversizedMessage();
    StopWhileWaitingForConnection();
    StopWhileClientIdle();
    RestartOnSameEndpoint();
    DispatchWaitShutdownOrdering();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "RuntimeControlPipeServerTests: all checks passed\n";
    return EXIT_SUCCESS;
}
