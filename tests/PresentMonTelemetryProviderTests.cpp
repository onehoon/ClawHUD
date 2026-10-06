#include "PresentMonProcessTelemetry.h"
#include "PresentMonTelemetryProvider.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

// Capabilities with DISPLAYED_FPS (+AVG). Optionally add PRESENTED_FPS and
// SWAP_CHAIN_ADDRESS.
PresentMonTelemetryCapabilities Capabilities(
    bool withPresented = true,
    bool withSwapChainAddress = true,
    PM_METRIC_TYPE displayedType = PM_METRIC_TYPE_DYNAMIC,
    PM_DATA_TYPE displayedPolled = PM_DATA_TYPE_DOUBLE,
    PM_METRIC_AVAILABILITY displayedAvail = PM_METRIC_AVAILABILITY_AVAILABLE,
    std::vector<PM_STAT> displayedStats = {PM_STAT_AVG, PM_STAT_NEWEST_POINT})
{
    PresentMonTelemetryCapabilities result;
    result.devices.push_back({0, PM_DEVICE_TYPE_INDEPENDENT,
        PM_DEVICE_VENDOR_UNKNOWN, "Independent"});

    PresentMonMetricCapability displayed{};
    displayed.id = PM_METRIC_DISPLAYED_FPS;
    displayed.type = displayedType;
    displayed.polledType = displayedPolled;
    displayed.statistics = std::move(displayedStats);
    displayed.devices.push_back({0, displayedAvail, 1});
    result.metrics.push_back(std::move(displayed));

    if (withPresented)
    {
        PresentMonMetricCapability presented{};
        presented.id = PM_METRIC_PRESENTED_FPS;
        presented.type = PM_METRIC_TYPE_DYNAMIC;
        presented.polledType = PM_DATA_TYPE_DOUBLE;
        presented.statistics = {PM_STAT_AVG};
        presented.devices.push_back({0, PM_METRIC_AVAILABILITY_AVAILABLE, 1});
        result.metrics.push_back(std::move(presented));
    }

    if (withSwapChainAddress)
    {
        PresentMonMetricCapability address{};
        address.id = PM_METRIC_SWAP_CHAIN_ADDRESS;
        address.type = PM_METRIC_TYPE_DYNAMIC;
        address.polledType = PM_DATA_TYPE_UINT64;
        address.statistics = {PM_STAT_NEWEST_POINT};
        address.devices.push_back({0, PM_METRIC_AVAILABILITY_AVAILABLE, 1});
        result.metrics.push_back(std::move(address));
    }

    return result;
}

class FakeClient final : public PresentMonApi2Client
{
public:
    PM_STATUS startStatus{PM_STATUS_SUCCESS};
    PM_STATUS pollStatus{PM_STATUS_SUCCESS};
    std::uint32_t pollResultCount{1};

    // Per-metric values written on the next poll.
    std::optional<double> displayed{60.0};
    std::optional<double> presented{58.0};
    std::optional<std::uint64_t> swapChainAddress{0x1000};

    std::vector<std::string> calls;
    std::vector<std::uint32_t> started;
    std::vector<std::uint32_t> stopped;
    std::vector<PM_METRIC> lastRegisteredMetrics;
    double lastWindowMs{};
    double lastOffsetMs{};
    std::uint32_t lastPollSwapChainRequest{};
    int registerCount{};
    int freeCount{};
    int pollCount{};

    std::vector<PM_QUERY_ELEMENT> elements_;

    PM_STATUS StartTrackingProcess(std::uint32_t pid) override
    {
        calls.push_back("start");
        started.push_back(pid);
        return startStatus;
    }

    PM_STATUS StopTrackingProcess(std::uint32_t pid) override
    {
        calls.push_back("stop");
        stopped.push_back(pid);
        return PM_STATUS_SUCCESS;
    }

    PM_STATUS RegisterDynamicQuery(PM_DYNAMIC_QUERY_HANDLE* query,
        PM_QUERY_ELEMENT* elements, std::uint64_t elementCount,
        double windowMs, double offsetMs) override
    {
        ++registerCount;
        calls.push_back("register");
        lastWindowMs = windowMs;
        lastOffsetMs = offsetMs;
        lastRegisteredMetrics.clear();
        elements_.clear();
        for (std::uint64_t i = 0; i < elementCount; ++i)
        {
            elements[i].dataOffset = i * sizeof(std::uint64_t);
            elements[i].dataSize = sizeof(std::uint64_t);
            lastRegisteredMetrics.push_back(elements[i].metric);
            elements_.push_back(elements[i]);
        }
        *query = reinterpret_cast<PM_DYNAMIC_QUERY_HANDLE>(this);
        return PM_STATUS_SUCCESS;
    }

    PM_STATUS FreeDynamicQuery(PM_DYNAMIC_QUERY_HANDLE) override
    {
        ++freeCount;
        calls.push_back("free");
        return PM_STATUS_SUCCESS;
    }

    PM_STATUS PollDynamicQuery(PM_DYNAMIC_QUERY_HANDLE, std::uint32_t,
        std::uint8_t* blob, std::uint32_t* count) override
    {
        ++pollCount;
        lastPollSwapChainRequest = *count;
        if (pollStatus != PM_STATUS_SUCCESS)
            return pollStatus;
        for (const auto& element : elements_)
        {
            if (element.metric == PM_METRIC_DISPLAYED_FPS && displayed)
                std::memcpy(blob + element.dataOffset, &*displayed, sizeof(double));
            else if (element.metric == PM_METRIC_PRESENTED_FPS && presented)
                std::memcpy(blob + element.dataOffset, &*presented, sizeof(double));
            else if (element.metric == PM_METRIC_SWAP_CHAIN_ADDRESS && swapChainAddress)
                std::memcpy(blob + element.dataOffset, &*swapChainAddress,
                    sizeof(std::uint64_t));
        }
        *count = pollResultCount;
        return PM_STATUS_SUCCESS;
    }
};

void CheckQueryPlanning(bool& ok)
{
    const auto plan = BuildPresentMonProcessQueryPlan(Capabilities());
    ok &= Check(plan.has_value(), "displayed FPS + AVG yields a query plan");
    ok &= Check(plan && plan->elements.at(plan->displayedIndex).metric ==
            PM_METRIC_DISPLAYED_FPS &&
        plan->elements.at(plan->displayedIndex).stat == PM_STAT_AVG,
        "displayed element is DISPLAYED_FPS + AVG on the independent device");
    ok &= Check(plan && plan->presentedIndex &&
        plan->elements.at(*plan->presentedIndex).metric == PM_METRIC_PRESENTED_FPS &&
        plan->elements.at(*plan->presentedIndex).stat == PM_STAT_AVG,
        "presented FPS is added to the same query as AVG when supported");
    ok &= Check(plan && plan->swapChainAddressIndex &&
        plan->elements.at(*plan->swapChainAddressIndex).metric ==
            PM_METRIC_SWAP_CHAIN_ADDRESS,
        "swap chain address is added to the same query when supported");

    const auto noExtras = BuildPresentMonProcessQueryPlan(Capabilities(false, false));
    ok &= Check(noExtras && !noExtras->presentedIndex &&
        !noExtras->swapChainAddressIndex && noExtras->elements.size() == 1,
        "displayed FPS alone still yields a usable plan");

    auto newestOnly = Capabilities();
    newestOnly.metrics[0].statistics = {PM_STAT_NEWEST_POINT};
    ok &= Check(!BuildPresentMonProcessQueryPlan(newestOnly),
        "displayed FPS without AVG is unavailable");
    ok &= Check(!BuildPresentMonProcessQueryPlan(
        Capabilities(true, true, PM_METRIC_TYPE_STATIC)),
        "static displayed metric is rejected");
    ok &= Check(!BuildPresentMonProcessQueryPlan(Capabilities(
        true, true, PM_METRIC_TYPE_DYNAMIC, PM_DATA_TYPE_UINT64)),
        "non-double displayed polled type is rejected");
    ok &= Check(!BuildPresentMonProcessQueryPlan(Capabilities(
        true, true, PM_METRIC_TYPE_DYNAMIC, PM_DATA_TYPE_DOUBLE,
        PM_METRIC_AVAILABILITY_UNAVAILABLE)),
        "unavailable displayed metric is rejected");

    PresentMonTelemetryProvider provider;
    ok &= Check(!provider.Ready() && !provider.ProcessReady() &&
        !provider.SystemReady(),
        "process and system readiness are independent on an uninitialized provider");
}

void CheckDecoding(bool& ok)
{
    PM_QUERY_ELEMENT element{PM_METRIC_DISPLAYED_FPS, PM_STAT_AVG, 0, 0, 8,
        sizeof(double)};
    std::vector<std::uint8_t> blob(32);
    const double value = 123.5;
    std::memcpy(blob.data() + 8, &value, sizeof(value));
    ok &= Check(DecodePresentMonFps(blob, element) == value,
        "FPS decodes from the registered data offset");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::memcpy(blob.data() + 8, &nan, sizeof(nan));
    ok &= Check(!DecodePresentMonFps(blob, element), "NaN FPS rejected");
    const double infinity = std::numeric_limits<double>::infinity();
    std::memcpy(blob.data() + 8, &infinity, sizeof(infinity));
    ok &= Check(!DecodePresentMonFps(blob, element), "infinite FPS rejected");
    const double negative = -1.0;
    std::memcpy(blob.data() + 8, &negative, sizeof(negative));
    ok &= Check(!DecodePresentMonFps(blob, element), "negative FPS rejected");
    const double zero = 0.0;
    std::memcpy(blob.data() + 8, &zero, sizeof(zero));
    ok &= Check(!DecodePresentMonFps(blob, element), "zero FPS is not a valid result");

    PM_QUERY_ELEMENT addressElement{PM_METRIC_SWAP_CHAIN_ADDRESS,
        PM_STAT_NEWEST_POINT, 0, 0, 0, sizeof(std::uint64_t)};
    const std::uint64_t address = 0x7FF0ABCD1234ULL;
    std::memcpy(blob.data(), &address, sizeof(address));
    ok &= Check(DecodePresentMonSwapChainAddress(blob, addressElement) == address,
        "swap chain address decodes");
    const std::uint64_t nullAddress = 0;
    std::memcpy(blob.data(), &nullAddress, sizeof(nullAddress));
    ok &= Check(!DecodePresentMonSwapChainAddress(blob, addressElement),
        "null swap chain address is unavailable");
}

void CheckProcessLifecycle(bool& ok)
{
    FakeClient client;
    PresentMonProcessTelemetry telemetry;
    ok &= Check(telemetry.Initialize(client, Capabilities()) &&
        telemetry.Ready() && client.registerCount == 0,
        "Initialize validates capabilities only; no query is registered yet");

    ok &= Check(!telemetry.Read(client, 0) && client.started.empty(),
        "PID zero does not start tracking");

    client.displayed = 99.0;
    client.presented = 98.0;
    client.swapChainAddress = 0xABCD;
    auto snapshot = telemetry.Read(client, 1234);
    ok &= Check(snapshot && snapshot->processId == 1234 &&
        snapshot->displayedFps == 99.0 && snapshot->presentedFps == 98.0 &&
        snapshot->swapChainAddress == 0xABCDULL &&
        client.started.size() == 1 && client.registerCount == 1,
        "first PID starts tracking, registers the query, returns both rates");
    ok &= Check(kPresentMonFpsWindowMs == 1000.0 &&
        kPresentMonFpsOffsetMs == 150.0 &&
        kPresentMonEtwFlushPeriodMs == 8,
        "FPS timing matches the PresentMon v2.6.0 UI defaults");
    ok &= Check(client.lastWindowMs == kPresentMonFpsWindowMs &&
        client.lastOffsetMs == kPresentMonFpsOffsetMs,
        "registered query uses the configured v2.6.0 FPS timing");
    ok &= Check(client.lastPollSwapChainRequest == 1,
        "poll requests exactly one swap-chain result");

    telemetry.Read(client, 1234);
    ok &= Check(client.started.size() == 1 && client.registerCount == 1 &&
        client.pollCount == 2,
        "reading the same PID does not recreate the query or tracking");

    // PID transition: old query freed and old PID stopped before the new target.
    snapshot = telemetry.Read(client, 5678);
    ok &= Check(snapshot && snapshot->processId == 5678 &&
        client.freeCount == 1 && client.stopped.size() == 1 &&
        client.stopped[0] == 1234 && client.started.size() == 2 &&
        client.registerCount == 2,
        "PID transition frees the old query and stops the old PID before retarget");
    const auto freeIndex = std::find(client.calls.begin(), client.calls.end(), "free")
        - client.calls.begin();
    const auto secondStart = [&]
    {
        int seen = 0;
        for (std::size_t i = 0; i < client.calls.size(); ++i)
            if (client.calls[i] == "start" && ++seen == 2) return static_cast<long>(i);
        return -1L;
    }();
    ok &= Check(freeIndex < secondStart,
        "old frame-query state is destroyed before polling the new target");

    // Displayed FPS authority: HUD-facing value is displayed even when presented differs.
    client.displayed = 99.0;
    client.presented = 52.0;
    snapshot = telemetry.Read(client, 5678);
    ok &= Check(snapshot && snapshot->displayedFps == 99.0 &&
        snapshot->presentedFps == 52.0,
        "displayed and presented are reported independently");

    // Metric independence: presented unavailable, displayed still usable.
    client.presented.reset();
    snapshot = telemetry.Read(client, 5678);
    ok &= Check(snapshot && snapshot->displayedFps == 99.0 &&
        !snapshot->presentedFps,
        "process telemetry stays usable when presented FPS is unavailable");

    // Invalid displayed values become unavailable.
    client.presented = 60.0;
    client.displayed = 0.0;
    snapshot = telemetry.Read(client, 5678);
    ok &= Check(snapshot && !snapshot->displayedFps && snapshot->presentedFps == 60.0,
        "zero displayed FPS is unavailable but the snapshot still carries presented");
    client.displayed = -5.0;
    ok &= Check(!telemetry.Read(client, 5678)->displayedFps,
        "negative displayed FPS is unavailable");
    client.displayed = std::numeric_limits<double>::quiet_NaN();
    ok &= Check(!telemetry.Read(client, 5678)->displayedFps,
        "NaN displayed FPS is unavailable");
    client.displayed = std::numeric_limits<double>::infinity();
    ok &= Check(!telemetry.Read(client, 5678)->displayedFps,
        "infinite displayed FPS is unavailable");
    client.displayed = 120.0;

    // Invalid PID poll releases the target.
    client.pollStatus = PM_STATUS_INVALID_PID;
    ok &= Check(!telemetry.Read(client, 5678) &&
        telemetry.TrackedProcessId() == 0 && client.freeCount == 2,
        "invalid process poll frees the query and clears the tracked PID");
    client.pollStatus = PM_STATUS_SUCCESS;

    // Read(0) tears down the target without shutting anything else down.
    telemetry.Read(client, 4321);
    const int freesBeforeClear = client.freeCount;
    const std::size_t stopsBeforeClear = client.stopped.size();
    ok &= Check(!telemetry.Read(client, 0) &&
        telemetry.TrackedProcessId() == 0 &&
        client.freeCount == freesBeforeClear + 1 &&
        client.stopped.size() == stopsBeforeClear + 1 &&
        client.stopped.back() == 4321 && telemetry.Ready(),
        "PID zero frees the query, stops the PID, keeps the provider ready");

    telemetry.Shutdown(client);
    ok &= Check(!telemetry.Ready(), "shutdown clears readiness");
    telemetry.Shutdown(client);
    ok &= Check(!telemetry.Ready(), "shutdown is idempotent");
}

void CheckStaleValueProtection(bool& ok)
{
    FakeClient client;
    PresentMonProcessTelemetry telemetry;
    telemetry.Initialize(client, Capabilities());

    client.displayed = 175.0;
    auto a = telemetry.Read(client, 100);
    ok &= Check(a && a->processId == 100 && a->displayedFps == 175.0,
        "PID A produces its own value");
    const int registersAfterA = client.registerCount;

    client.displayed = 99.0;
    auto b = telemetry.Read(client, 200);
    ok &= Check(b && b->processId == 200 && b->displayedFps == 99.0 &&
        client.registerCount == registersAfterA + 1,
        "PID B gets a freshly registered query and its own value, never PID A's");
    ok &= Check(telemetry.TrackedProcessId() == 200,
        "tracking follows the new PID");
}

// R5: ProductionTelemetryController::SetInGameForegroundProcess explicitly
// releases the target (Read(0)) whenever the InGameOnly target semantically
// changes, even when Windows reuses the same numeric PID for a new process
// generation - Read()/RetargetProcess only rebuild the query when the numeric
// PID itself changes, so without that explicit release a PID-reuse transition
// would otherwise keep the old generation's query/tracking alive.
void CheckPidReuseAfterExplicitRelease(bool& ok)
{
    FakeClient client;
    PresentMonProcessTelemetry telemetry;
    telemetry.Initialize(client, Capabilities());

    client.displayed = 144.0;
    auto generationA = telemetry.Read(client, 5000);
    ok &= Check(generationA && generationA->displayedFps == 144.0,
        "PID 5000 generation A gets a fresh query and its own value");
    const int registersAfterA = client.registerCount;
    const std::size_t stopsAfterA = client.stopped.size();

    // The In-Game Only target semantically changed (PID 5000 reused for a new
    // process generation): the caller explicitly releases the target first.
    ok &= Check(!telemetry.Read(client, 0) && telemetry.TrackedProcessId() == 0 &&
        client.stopped.size() == stopsAfterA + 1 && client.stopped.back() == 5000,
        "explicit release stops tracking the old generation's PID");

    client.displayed = 60.0;
    auto generationB = telemetry.Read(client, 5000);
    ok &= Check(generationB && generationB->displayedFps == 60.0 &&
        client.registerCount == registersAfterA + 1,
        "the same numeric PID after an explicit release gets a freshly "
        "registered query, never generation A's stale query/value");
}

// R5: ProductionTelemetryController::SetVisibilityMode also releases the target
// (Read(0)) on a visibility-authority change. A game target change observed
// while Always was active never released the query, so Always PID 5000/gen A ->
// current game PID 5000/gen B -> switch to InGameOnly would otherwise let the
// first InGameOnly sample reuse generation A's PID-5000 query. This walks that
// exact sequence at the telemetry seam.
void CheckModeSwitchReleasesPidReusedTarget(bool& ok)
{
    FakeClient client;
    PresentMonProcessTelemetry telemetry;
    telemetry.Initialize(client, Capabilities());

    // Always mode has been sampling PID 5000 / generation A.
    client.displayed = 240.0;
    telemetry.Read(client, 5000);
    const int registersAfterAlways = client.registerCount;

    // Windows reused PID 5000 for generation B while Always was active (no
    // release happened). Switching visibility mode now releases the target.
    ok &= Check(!telemetry.Read(client, 0) && telemetry.TrackedProcessId() == 0,
        "a visibility-mode change releases the prior authority's PID-bound query");

    // First InGameOnly sample: same numeric PID, new process generation.
    client.displayed = 72.0;
    auto firstInGameOnly = telemetry.Read(client, 5000);
    ok &= Check(firstInGameOnly && firstInGameOnly->displayedFps == 72.0 &&
        client.registerCount == registersAfterAlways + 1,
        "the first InGameOnly sample rebuilds the PID-5000 query for generation B");
}

void CheckSharedTracking(bool& ok)
{
    ProcessTrackingRefCounts refs;
    ok &= Check(refs.Acquire(100),
        "the first consumer of a PID fires the start endpoint");
    ok &= Check(!refs.Acquire(100) && refs.Count(100) == 2,
        "a second consumer of the same PID does not re-fire the start endpoint");
    ok &= Check(!refs.Release(100) && refs.Count(100) == 1,
        "releasing one of two holders keeps tracking alive for the other");
    ok &= Check(refs.Release(100) && refs.Count(100) == 0,
        "releasing the last holder fires the stop endpoint");
    ok &= Check(!refs.Release(100),
        "releasing an untracked PID fires nothing");

    ok &= Check(refs.Acquire(7) && refs.Count(7) == 1, "acquire a fresh PID");
    refs.AbortAcquire(7);
    ok &= Check(refs.Count(7) == 0,
        "AbortAcquire unwinds a first acquire whose endpoint start failed");
    refs.Acquire(9);
    refs.Acquire(9);
    refs.AbortAcquire(9);
    ok &= Check(refs.Count(9) == 1,
        "AbortAcquire on a shared PID only drops the failed holder");

    PresentMonTelemetryProvider provider;
    auto lease = provider.AcquireProcess(1234);
    ok &= Check(!lease && lease.ProcessId() == 0,
        "AcquireProcess on an unready provider yields an empty lease");
    PresentMonProcessLease moved = std::move(lease);
    ok &= Check(!moved, "moving an empty lease is safe");
    moved.Release();
    ok &= Check(!provider.PollGameRenderDisplayedFrame(1234) &&
        !provider.FrameReady(),
        "an unready provider reports no game-render frame evidence");
}

void CheckSystemTelemetry(bool& ok)
{
    ok &= Check(SupportsPresentMonDynamicQuery(PM_METRIC_TYPE_DYNAMIC),
        "dynamic metrics are accepted for system queries");
    ok &= Check(SupportsPresentMonDynamicQuery(PM_METRIC_TYPE_DYNAMIC_FRAME),
        "dynamic-frame metrics are accepted for system queries");
    ok &= Check(!SupportsPresentMonDynamicQuery(PM_METRIC_TYPE_STATIC),
        "static metrics are rejected for system queries");
    ok &= Check(!SupportsPresentMonDynamicQuery(PM_METRIC_TYPE_FRAME_EVENT),
        "frame-event metrics are rejected for system queries");

    const char name[] = "Intel Graphics";
    PM_INTROSPECTION_STRING deviceName{name};
    PM_INTROSPECTION_DEVICE devices[] = {
        {1, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_DEVICE_VENDOR_INTEL, &deviceName, nullptr},
        {2, PM_DEVICE_TYPE_SYSTEM, PM_DEVICE_VENDOR_UNKNOWN, nullptr, nullptr}};
    std::array<const void*, 2> deviceEntries{&devices[0], &devices[1]};
    PM_INTROSPECTION_OBJARRAY deviceArray{deviceEntries.data(), deviceEntries.size()};
    PM_INTROSPECTION_DATA_TYPE_INFO doubleType{PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_VOID, PM_ENUM_METRIC};
    PM_INTROSPECTION_DATA_TYPE_INFO uint64Type{PM_DATA_TYPE_UINT64, PM_DATA_TYPE_VOID, PM_ENUM_METRIC};
    PM_INTROSPECTION_STAT_INFO stats[] = {{PM_STAT_AVG}, {PM_STAT_NEWEST_POINT}};
    std::array<const void*, 2> statEntries{&stats[0], &stats[1]};
    PM_INTROSPECTION_OBJARRAY statArray{statEntries.data(), statEntries.size()};
    PM_INTROSPECTION_DEVICE_METRIC_INFO metricDevices[] = {
        {1, PM_METRIC_AVAILABILITY_AVAILABLE, 1}, {2, PM_METRIC_AVAILABILITY_AVAILABLE, 1}};
    std::array<const void*, 2> metricDeviceEntries{&metricDevices[0], &metricDevices[1]};
    PM_INTROSPECTION_OBJARRAY metricDeviceArray{metricDeviceEntries.data(), metricDeviceEntries.size()};
    PM_INTROSPECTION_DEVICE_METRIC_INFO cpuFrequencyDevice{2,
        PM_METRIC_AVAILABILITY_AVAILABLE, 1};
    std::array<const void*, 1> cpuFrequencyDeviceEntries{&cpuFrequencyDevice};
    PM_INTROSPECTION_OBJARRAY cpuFrequencyDeviceArray{
        cpuFrequencyDeviceEntries.data(), cpuFrequencyDeviceEntries.size()};
    PM_INTROSPECTION_DEVICE_METRIC_INFO gpuPowerDevice{1,
        PM_METRIC_AVAILABILITY_AVAILABLE, 1};
    std::array<const void*, 1> gpuPowerDeviceEntries{&gpuPowerDevice};
    PM_INTROSPECTION_OBJARRAY gpuPowerDeviceArray{
        gpuPowerDeviceEntries.data(), gpuPowerDeviceEntries.size()};
    PM_INTROSPECTION_METRIC metrics[] = {
        {PM_METRIC_CPU_UTILIZATION, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_PERCENT, PM_UNIT_PERCENT,
            &doubleType, &statArray, &metricDeviceArray},
        {PM_METRIC_CPU_FREQUENCY, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_HERTZ, PM_UNIT_MEGAHERTZ,
            &doubleType, &statArray, &cpuFrequencyDeviceArray},
        {PM_METRIC_GPU_UTILIZATION, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_PERCENT, PM_UNIT_PERCENT,
            &doubleType, &statArray, &metricDeviceArray},
        {PM_METRIC_GPU_FREQUENCY, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_HERTZ, PM_UNIT_MEGAHERTZ,
            &doubleType, &statArray, &metricDeviceArray},
        {PM_METRIC_GPU_POWER, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MILLIWATTS, PM_UNIT_WATTS,
            &doubleType, &statArray, &gpuPowerDeviceArray},
        {PM_METRIC_GPU_MEM_USED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BYTES, PM_UNIT_BYTES,
            &uint64Type, &statArray, &metricDeviceArray}};
    std::array<const void*, 6> metricEntries{
        &metrics[0], &metrics[1], &metrics[2], &metrics[3], &metrics[4], &metrics[5]};
    PM_INTROSPECTION_OBJARRAY metricArray{metricEntries.data(), metricEntries.size()};
    PM_INTROSPECTION_ROOT root{&metricArray, nullptr, &deviceArray, nullptr};
    const auto capabilities = BuildPresentMonTelemetryCapabilities(&root);
    const auto plan = BuildPresentMonSystemQueryPlan(capabilities);
    ok &= Check(plan.elements.size() == 6 && plan.bindings.size() == 6,
        "system metrics share one query");
    ok &= Check(plan.bindings[0].slot == SystemMetricSlot::CpuUsage &&
        plan.bindings[1].slot == SystemMetricSlot::CpuFrequency &&
        plan.bindings[2].slot == SystemMetricSlot::GpuUsage &&
        plan.bindings[3].slot == SystemMetricSlot::GpuFrequency &&
        plan.bindings[4].slot == SystemMetricSlot::GpuMemoryUsed &&
        plan.bindings[5].slot == SystemMetricSlot::GpuPower,
        "all six intended system metric slots are planned");
    ok &= Check(plan.elements[0].deviceId == 2 && plan.elements[1].deviceId == 2 &&
        plan.elements[2].deviceId == 1,
        "system and Intel device selection is capability driven");
    ok &= Check(std::all_of(plan.elements.begin(), plan.elements.end(),
        [](const auto& element) { return element.stat == PM_STAT_AVG; }),
        "system telemetry follows the official no-target AVG statistic preference");
    ok &= Check(plan.bindings[1].type == PM_DATA_TYPE_DOUBLE &&
        plan.bindings[1].unit == PM_UNIT_HERTZ &&
        plan.bindings[3].type == PM_DATA_TYPE_DOUBLE &&
        plan.bindings[3].unit == PM_UNIT_HERTZ,
        "AVG telemetry uses the official dynamic-query double output type");
    ok &= Check(plan.elements[5].metric == PM_METRIC_GPU_POWER &&
        plan.elements[5].deviceId == 1 && plan.bindings[5].unit == PM_UNIT_MILLIWATTS,
        "GPU power query uses the selected Intel graphics adapter and introspected units");

    auto unavailableGpuPower = capabilities;
    const auto gpuPowerMetric = std::find_if(unavailableGpuPower.metrics.begin(),
        unavailableGpuPower.metrics.end(), [](const auto& metric)
        { return metric.id == PM_METRIC_GPU_POWER; });
    if (gpuPowerMetric != unavailableGpuPower.metrics.end())
        gpuPowerMetric->devices.front().availability = PM_METRIC_AVAILABILITY_UNAVAILABLE;
    const auto planWithoutGpuPower = BuildPresentMonSystemQueryPlan(unavailableGpuPower);
    ok &= Check(planWithoutGpuPower.bindings.size() == 5 &&
        std::none_of(planWithoutGpuPower.bindings.begin(), planWithoutGpuPower.bindings.end(),
            [](const auto& binding) { return binding.slot == SystemMetricSlot::GpuPower; }) &&
        std::any_of(planWithoutGpuPower.bindings.begin(), planWithoutGpuPower.bindings.end(),
            [](const auto& binding) { return binding.slot == SystemMetricSlot::CpuUsage; }) &&
        std::any_of(planWithoutGpuPower.bindings.begin(), planWithoutGpuPower.bindings.end(),
            [](const auto& binding) { return binding.slot == SystemMetricSlot::CpuFrequency; }) &&
        std::any_of(planWithoutGpuPower.bindings.begin(), planWithoutGpuPower.bindings.end(),
            [](const auto& binding) { return binding.slot == SystemMetricSlot::GpuUsage; }) &&
        std::any_of(planWithoutGpuPower.bindings.begin(), planWithoutGpuPower.bindings.end(),
            [](const auto& binding) { return binding.slot == SystemMetricSlot::GpuFrequency; }) &&
        std::any_of(planWithoutGpuPower.bindings.begin(), planWithoutGpuPower.bindings.end(),
            [](const auto& binding) { return binding.slot == SystemMetricSlot::GpuMemoryUsed; }),
        "unavailable GPU power leaves the other system metrics in the query");

    auto missingGpuPower = capabilities;
    std::erase_if(missingGpuPower.metrics, [](const auto& metric)
        { return metric.id == PM_METRIC_GPU_POWER; });
    ok &= Check(BuildPresentMonSystemQueryPlan(missingGpuPower).bindings.size() == 5,
        "an unexported GPU power metric does not change the other query bindings");

    auto unsupportedGpuPower = capabilities;
    const auto unsupportedPower = std::find_if(unsupportedGpuPower.metrics.begin(),
        unsupportedGpuPower.metrics.end(), [](const auto& metric)
        { return metric.id == PM_METRIC_GPU_POWER; });
    if (unsupportedPower != unsupportedGpuPower.metrics.end())
        unsupportedPower->type = PM_METRIC_TYPE_STATIC;
    ok &= Check(BuildPresentMonSystemQueryPlan(unsupportedGpuPower).bindings.size() == 5,
        "unsupported GPU power metric type is omitted without affecting other metrics");

    auto unavailableCpuFrequency = capabilities;
    const auto cpuFrequencyMetric = std::find_if(unavailableCpuFrequency.metrics.begin(),
        unavailableCpuFrequency.metrics.end(), [](const auto& metric)
        { return metric.id == PM_METRIC_CPU_FREQUENCY; });
    if (cpuFrequencyMetric != unavailableCpuFrequency.metrics.end())
        cpuFrequencyMetric->devices.front().availability = PM_METRIC_AVAILABILITY_UNAVAILABLE;
    const auto planWithoutCpuFrequency = BuildPresentMonSystemQueryPlan(unavailableCpuFrequency);
    ok &= Check(planWithoutCpuFrequency.bindings.size() == 5 &&
        std::none_of(planWithoutCpuFrequency.bindings.begin(), planWithoutCpuFrequency.bindings.end(),
            [](const auto& binding) { return binding.slot == SystemMetricSlot::CpuFrequency; }) &&
        std::any_of(planWithoutCpuFrequency.bindings.begin(), planWithoutCpuFrequency.bindings.end(),
            [](const auto& binding) { return binding.slot == SystemMetricSlot::CpuUsage; }) &&
        std::any_of(planWithoutCpuFrequency.bindings.begin(), planWithoutCpuFrequency.bindings.end(),
            [](const auto& binding) { return binding.slot == SystemMetricSlot::GpuFrequency; }),
        "unsupported CPU frequency omits only that metric binding");

    auto missingCpuFrequency = capabilities;
    std::erase_if(missingCpuFrequency.metrics, [](const auto& metric)
        { return metric.id == PM_METRIC_CPU_FREQUENCY; });
    ok &= Check(BuildPresentMonSystemQueryPlan(missingCpuFrequency).bindings.size() == 5,
        "absent CPU frequency capability leaves the other system metrics planned");

    const auto decodeFrequency = [](double value, PM_UNIT unit)
    {
        std::array<std::uint8_t, sizeof(double)> frequencyBlob{};
        std::memcpy(frequencyBlob.data(), &value, sizeof(value));
        const PM_QUERY_ELEMENT frequencyElement{PM_METRIC_CPU_FREQUENCY,
            PM_STAT_AVG, 2, 0, 0, sizeof(double)};
        return DecodePresentMonFrequencyMHz(frequencyBlob.data(), frequencyElement,
            PM_DATA_TYPE_DOUBLE, unit);
    };
    ok &= Check(decodeFrequency(4200000000.0, PM_UNIT_HERTZ) == 4200.0 &&
        decodeFrequency(4200000.0, PM_UNIT_KILOHERTZ) == 4200.0 &&
        decodeFrequency(4200.0, PM_UNIT_MEGAHERTZ) == 4200.0 &&
        std::abs(decodeFrequency(4.2, PM_UNIT_GIGAHERTZ).value_or(-1.0) - 4200.0) < 0.01,
        "CPU frequency converts introspected Hz, kHz, MHz and GHz units to MHz");
    ok &= Check(!decodeFrequency(std::numeric_limits<double>::quiet_NaN(), PM_UNIT_HERTZ) &&
        !decodeFrequency(std::numeric_limits<double>::infinity(), PM_UNIT_HERTZ) &&
        !decodeFrequency(-1.0, PM_UNIT_HERTZ) &&
        !decodeFrequency(4200.0, PM_UNIT_PERCENT),
        "CPU frequency rejects non-finite, negative and unsupported-unit values");

    const auto decodePower = [](double value, PM_UNIT unit,
        std::uint32_t dataSize = sizeof(double))
    {
        std::array<std::uint8_t, sizeof(double)> powerBlob{};
        std::memcpy(powerBlob.data(), &value, sizeof(value));
        const PM_QUERY_ELEMENT powerElement{PM_METRIC_GPU_POWER,
            PM_STAT_AVG, 1, 0, 0, dataSize};
        return DecodePresentMonPowerWatts(powerBlob.data(), powerElement,
            PM_DATA_TYPE_DOUBLE, unit);
    };
    ok &= Check(decodePower(13000.0, PM_UNIT_MILLIWATTS) == 13.0 &&
        decodePower(13.0, PM_UNIT_WATTS) == 13.0 &&
        decodePower(0.013, PM_UNIT_KILOWATTS) == 13.0,
        "GPU power converts mW, W and kW to watts");
    ok &= Check(!decodePower(13.0, PM_UNIT_PERCENT) &&
        !decodePower(-1.0, PM_UNIT_WATTS) &&
        !decodePower(std::numeric_limits<double>::quiet_NaN(), PM_UNIT_WATTS) &&
        !decodePower(std::numeric_limits<double>::infinity(), PM_UNIT_WATTS) &&
        !decodePower(13.0, PM_UNIT_WATTS, sizeof(float)),
        "GPU power rejects unsupported units, invalid values and undersized data");

    std::array<std::uint8_t, sizeof(double)> cpuClockBlob{};
    const double cpuClockHz = 4200000000.0;
    std::memcpy(cpuClockBlob.data(), &cpuClockHz, sizeof(cpuClockHz));
    const PM_QUERY_ELEMENT cpuClockElement{PM_METRIC_CPU_FREQUENCY,
        PM_STAT_AVG, 2, 0, 0, sizeof(double)};
    const auto cpuFrequencySnapshot = DecodePresentMonSystemSnapshot(PM_STATUS_SUCCESS, 1,
        cpuClockBlob.data(), {cpuClockElement},
        {{SystemMetricSlot::CpuFrequency, 0, PM_DATA_TYPE_DOUBLE, PM_UNIT_HERTZ}});
    ok &= Check(cpuFrequencySnapshot && cpuFrequencySnapshot->cpuClockMHz == 4200.0,
        "CPU frequency binding decodes into the system snapshot");
    const auto gpuFrequencySnapshot = DecodePresentMonSystemSnapshot(PM_STATUS_SUCCESS, 1,
        cpuClockBlob.data(), {cpuClockElement},
        {{SystemMetricSlot::GpuFrequency, 0, PM_DATA_TYPE_DOUBLE, PM_UNIT_HERTZ}});
    ok &= Check(gpuFrequencySnapshot && gpuFrequencySnapshot->gpuClockMHz == 4200.0,
        "existing GPU frequency binding still converts hertz to MHz");
    const double gpuPowerMilliwatts = 12600.0;
    std::memcpy(cpuClockBlob.data(), &gpuPowerMilliwatts, sizeof(gpuPowerMilliwatts));
    const PM_QUERY_ELEMENT gpuPowerElement{PM_METRIC_GPU_POWER,
        PM_STAT_AVG, 1, 0, 0, sizeof(double)};
    const auto gpuPowerSnapshot = DecodePresentMonSystemSnapshot(PM_STATUS_SUCCESS, 1,
        cpuClockBlob.data(), {gpuPowerElement},
        {{SystemMetricSlot::GpuPower, 0, PM_DATA_TYPE_DOUBLE, PM_UNIT_MILLIWATTS}});
    ok &= Check(gpuPowerSnapshot && gpuPowerSnapshot->gpuPowerW == 12.6,
        "GPU power binding decodes into the system snapshot without rounding");
    const auto malformedPowerSnapshot = DecodePresentMonSystemSnapshot(PM_STATUS_SUCCESS, 1,
        cpuClockBlob.data(), {gpuPowerElement},
        {{SystemMetricSlot::GpuPower, 4, PM_DATA_TYPE_DOUBLE, PM_UNIT_MILLIWATTS}});
    ok &= Check(malformedPowerSnapshot && !malformedPowerSnapshot->gpuPowerW,
        "malformed GPU power binding remains unavailable instead of reading out of range");

    std::array<std::uint8_t, 32> blob{};
    PM_QUERY_ELEMENT element{PM_METRIC_GPU_UTILIZATION, PM_STAT_NEWEST_POINT, 1, 0, 3, sizeof(double)};
    double usage = 95.0;
    std::memcpy(blob.data() + 3, &usage, sizeof(usage));
    std::vector<PM_QUERY_ELEMENT> elements{element};
    std::vector<SystemMetricBinding> bindings{{SystemMetricSlot::GpuUsage, 0,
        PM_DATA_TYPE_DOUBLE, PM_UNIT_PERCENT}};
    ok &= Check(DecodePresentMonSystemSnapshot(PM_STATUS_SUCCESS, 1, blob.data(), elements, bindings)
            ->gpuUsagePercent == 95.0,
        "current system result decodes");
    ok &= Check(!DecodePresentMonSystemSnapshot(PM_STATUS_SUCCESS, 0, blob.data(), elements, bindings),
        "empty successful system result does not decode stale buffer");
}
}

int main()
{
    bool ok = true;
    CheckQueryPlanning(ok);
    CheckDecoding(ok);
    CheckProcessLifecycle(ok);
    CheckStaleValueProtection(ok);
    CheckPidReuseAfterExplicitRelease(ok);
    CheckModeSwitchReleasesPidReusedTarget(ok);
    CheckSharedTracking(ok);
    CheckSystemTelemetry(ok);
    return ok ? 0 : 1;
}
