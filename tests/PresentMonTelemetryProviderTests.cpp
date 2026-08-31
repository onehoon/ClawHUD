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
    ok &= Check(client.lastWindowMs == 1000.0 && client.lastOffsetMs == 80.0,
        "query uses the official 1000 ms window and 80 ms offset");
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
    PM_INTROSPECTION_METRIC metrics[] = {
        {PM_METRIC_CPU_UTILIZATION, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_PERCENT, PM_UNIT_PERCENT,
            &doubleType, &statArray, &metricDeviceArray},
        {PM_METRIC_GPU_UTILIZATION, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_PERCENT, PM_UNIT_PERCENT,
            &doubleType, &statArray, &metricDeviceArray},
        {PM_METRIC_GPU_FREQUENCY, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_HERTZ, PM_UNIT_MEGAHERTZ,
            &doubleType, &statArray, &metricDeviceArray},
        {PM_METRIC_GPU_MEM_USED, PM_METRIC_TYPE_DYNAMIC_FRAME, PM_UNIT_BYTES, PM_UNIT_BYTES,
            &uint64Type, &statArray, &metricDeviceArray}};
    std::array<const void*, 4> metricEntries{&metrics[0], &metrics[1], &metrics[2], &metrics[3]};
    PM_INTROSPECTION_OBJARRAY metricArray{metricEntries.data(), metricEntries.size()};
    PM_INTROSPECTION_ROOT root{&metricArray, nullptr, &deviceArray, nullptr};
    const auto capabilities = BuildPresentMonTelemetryCapabilities(&root);
    const auto plan = BuildPresentMonSystemQueryPlan(capabilities);
    ok &= Check(plan.elements.size() == 4 && plan.bindings.size() == 4,
        "system metrics share one query");
    ok &= Check(plan.bindings[0].slot == SystemMetricSlot::CpuUsage &&
        plan.bindings[1].slot == SystemMetricSlot::GpuUsage &&
        plan.bindings[2].slot == SystemMetricSlot::GpuFrequency &&
        plan.bindings[3].slot == SystemMetricSlot::GpuMemoryUsed,
        "all four intended system metric slots are planned");
    ok &= Check(plan.elements[0].deviceId == 2 && plan.elements[1].deviceId == 1,
        "system and Intel device selection is capability driven");
    ok &= Check(std::all_of(plan.elements.begin(), plan.elements.end(),
        [](const auto& element) { return element.stat == PM_STAT_AVG; }),
        "system telemetry follows the official no-target AVG statistic preference");
    ok &= Check(plan.bindings[3].type == PM_DATA_TYPE_DOUBLE,
        "AVG telemetry uses the official dynamic-query double output type");

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
    CheckSharedTracking(ok);
    CheckSystemTelemetry(ok);
    return ok ? 0 : 1;
}
