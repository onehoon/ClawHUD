#include "PresentMonProcessTelemetry.h"
#include "PresentMonTelemetryProvider.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
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

PresentMonTelemetryCapabilities Capabilities(
    PM_METRIC_TYPE type = PM_METRIC_TYPE_DYNAMIC,
    PM_DATA_TYPE polledType = PM_DATA_TYPE_DOUBLE,
    PM_METRIC_AVAILABILITY availability = PM_METRIC_AVAILABILITY_AVAILABLE,
    std::vector<PM_STAT> statistics = {PM_STAT_AVG, PM_STAT_NEWEST_POINT})
{
    PresentMonTelemetryCapabilities result;
    result.devices.push_back({0, PM_DEVICE_TYPE_INDEPENDENT,
        PM_DEVICE_VENDOR_UNKNOWN, "Independent"});
    PresentMonMetricCapability metric{};
    metric.id = PM_METRIC_DISPLAYED_FPS;
    metric.type = type;
    metric.polledType = polledType;
    metric.statistics = std::move(statistics);
    metric.devices.push_back({0, availability, 1});
    result.metrics.push_back(std::move(metric));
    return result;
}

class FakeClient final : public PresentMonApi2Client
{
public:
    PM_STATUS startStatus{PM_STATUS_SUCCESS};
    PM_STATUS pollStatus{PM_STATUS_SUCCESS};
    std::vector<std::optional<double>> values{60.0};
    std::vector<std::string> calls;
    std::vector<std::uint32_t> started;
    std::vector<std::uint32_t> stopped;
    int registerCount{};
    int freeCount{};
    int pollCount{};

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
        PM_QUERY_ELEMENT* element, std::uint64_t, double, double) override
    {
        ++registerCount;
        *query = reinterpret_cast<PM_DYNAMIC_QUERY_HANDLE>(this);
        element->dataOffset = 0;
        element->dataSize = sizeof(double);
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
        if (pollStatus != PM_STATUS_SUCCESS)
            return pollStatus;
        const auto populated = static_cast<std::uint32_t>(
            std::min<std::size_t>(*count, values.size()));
        for (std::uint32_t i = 0; i < populated; ++i)
        {
            if (values[i])
                std::memcpy(blob + i * 16, &*values[i], sizeof(double));
        }
        *count = populated;
        return PM_STATUS_SUCCESS;
    }
};

void CheckQueryPlanning(bool& ok)
{
    const auto plan = BuildPresentMonProcessQueryPlan(Capabilities());
    ok &= Check(plan && plan->element.metric == PM_METRIC_DISPLAYED_FPS &&
        plan->element.stat == PM_STAT_AVG && plan->element.deviceId == 0,
        "Displayed FPS capability selects an AVG independent-device query");

    auto newestOnly = Capabilities();
    newestOnly.metrics[0].statistics = {PM_STAT_NEWEST_POINT};
    ok &= Check(!BuildPresentMonProcessQueryPlan(newestOnly),
        "Displayed FPS without AVG is unavailable");
    ok &= Check(!BuildPresentMonProcessQueryPlan(
        Capabilities(PM_METRIC_TYPE_STATIC)), "static metric is rejected");
    ok &= Check(!BuildPresentMonProcessQueryPlan(
        Capabilities(PM_METRIC_TYPE_DYNAMIC, PM_DATA_TYPE_UINT64)),
        "non-double polled type is rejected");
    ok &= Check(!BuildPresentMonProcessQueryPlan(Capabilities(
        PM_METRIC_TYPE_DYNAMIC, PM_DATA_TYPE_DOUBLE,
        PM_METRIC_AVAILABILITY_UNAVAILABLE)), "unavailable metric is rejected");
    ok &= Check(!BuildPresentMonProcessQueryPlan(Capabilities(
        PM_METRIC_TYPE_DYNAMIC, PM_DATA_TYPE_DOUBLE,
        PM_METRIC_AVAILABILITY_AVAILABLE, {PM_STAT_NONE})),
        "unsupported statistic is rejected");

    PresentMonTelemetryProvider provider;
    ok &= Check(!provider.Ready() && !provider.ProcessReady(),
        "process readiness is independent on an uninitialized provider");
}

void CheckDecoding(bool& ok)
{
    PM_QUERY_ELEMENT element{PM_METRIC_DISPLAYED_FPS, PM_STAT_AVG, 0, 0, 8,
        sizeof(double)};
    std::vector<std::uint8_t> blob(32);
    const double value = 123.5;
    std::memcpy(blob.data() + 8, &value, sizeof(value));
    ok &= Check(DecodePresentMonDisplayedFps(blob, element) == value,
        "Displayed FPS decodes from the registered data offset");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::memcpy(blob.data() + 8, &nan, sizeof(nan));
    ok &= Check(!DecodePresentMonDisplayedFps(blob, element), "NaN FPS rejected");
    const double infinity = std::numeric_limits<double>::infinity();
    std::memcpy(blob.data() + 8, &infinity, sizeof(infinity));
    ok &= Check(!DecodePresentMonDisplayedFps(blob, element), "infinite FPS rejected");
    const double negative = -1.0;
    std::memcpy(blob.data() + 8, &negative, sizeof(negative));
    ok &= Check(!DecodePresentMonDisplayedFps(blob, element), "negative FPS rejected");

    const std::vector<std::optional<double>> values{
        std::nullopt, 60.0, 120.0, -1.0,
        std::numeric_limits<double>::infinity()};
    ok &= Check(SelectPresentMonDisplayedFps(values) == 120.0,
        "multiple swap chains select the highest valid FPS");
    const std::vector<std::optional<double>> emptyValues;
    ok &= Check(!SelectPresentMonDisplayedFps(emptyValues),
        "zero swap chains produce no FPS");
}

void CheckProcessLifecycle(bool& ok)
{
    FakeClient client;
    PresentMonProcessTelemetry telemetry;
    ok &= Check(telemetry.Initialize(client, Capabilities()) && telemetry.Ready() &&
        client.registerCount == 1, "process query initializes once");

    ok &= Check(!telemetry.Read(client, 0) && client.started.empty(),
        "PID zero does not start tracking");
    auto snapshot = telemetry.Read(client, 1234);
    ok &= Check(snapshot && snapshot->processId == 1234 &&
        snapshot->displayedFps && *snapshot->displayedFps == 60.0 &&
        client.started.size() == 1, "first PID returns its displayed FPS");
    telemetry.Read(client, 1234);
    ok &= Check(client.started.size() == 1 && client.pollCount == 2,
        "same PID reuses existing tracking");

    ok &= Check(!telemetry.Read(client, 0) && telemetry.TrackedProcessId() == 0 &&
        client.stopped.size() == 1,
        "PID zero clears an active tracking target");
    snapshot = telemetry.Read(client, 1234);
    ok &= Check(snapshot && client.started.size() == 2,
        "a target can be re-entered after an explicit clear");

    client.values = {90.0};
    snapshot = telemetry.Read(client, 5678);
    ok &= Check(snapshot && snapshot->processId == 5678 &&
        client.started.size() == 3 && client.stopped.size() == 2 &&
        client.calls.size() >= 5 && client.calls[3] == "stop" &&
        client.calls[4] == "start",
        "PID switch stops the old target before starting the new target");

    client.values = {60.0, 70.0, 80.0, 90.0, 144.0};
    snapshot = telemetry.Read(client, 5678);
    ok &= Check(snapshot && snapshot->displayedFps &&
        *snapshot->displayedFps == 144.0,
        "poll capacity includes swap chains beyond the initial four slots");

    client.pollStatus = PM_STATUS_INVALID_PID;
    ok &= Check(!telemetry.Read(client, 5678) &&
        telemetry.TrackedProcessId() == 0,
        "invalid process poll clears the tracked PID");
    client.pollStatus = PM_STATUS_SUCCESS;
    telemetry.Read(client, 5678);
    ok &= Check(client.started.size() == 4,
        "cleared invalid PID can be requested again");

    telemetry.Shutdown(client);
    ok &= Check(!telemetry.Ready() && client.stopped.size() == 3 &&
        client.freeCount == 1, "shutdown stops tracking and frees the query");
    telemetry.Shutdown(client);
    ok &= Check(client.stopped.size() == 3 && client.freeCount == 1,
        "shutdown is idempotent");

    FakeClient invalidStart;
    invalidStart.startStatus = PM_STATUS_INVALID_PID;
    PresentMonProcessTelemetry invalidTelemetry;
    invalidTelemetry.Initialize(invalidStart, Capabilities());
    ok &= Check(!invalidTelemetry.Read(invalidStart, 9999) &&
        invalidTelemetry.TrackedProcessId() == 0,
        "invalid process start returns no snapshot without retrying");
}

void CheckSystemTelemetry(bool& ok)
{
    const char name[] = "Intel Graphics";
    PM_INTROSPECTION_STRING deviceName{name};
    PM_INTROSPECTION_DEVICE devices[] = {
        {1, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_DEVICE_VENDOR_INTEL, &deviceName, nullptr},
        {2, PM_DEVICE_TYPE_SYSTEM, PM_DEVICE_VENDOR_UNKNOWN, nullptr, nullptr}};
    std::array<const void*, 2> deviceEntries{&devices[0], &devices[1]};
    PM_INTROSPECTION_OBJARRAY deviceArray{deviceEntries.data(), deviceEntries.size()};
    PM_INTROSPECTION_DATA_TYPE_INFO doubleType{PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_VOID, PM_ENUM_METRIC};
    PM_INTROSPECTION_STAT_INFO stat{PM_STAT_NEWEST_POINT};
    std::array<const void*, 1> statEntries{&stat};
    PM_INTROSPECTION_OBJARRAY statArray{statEntries.data(), statEntries.size()};
    PM_INTROSPECTION_DEVICE_METRIC_INFO metricDevices[] = {
        {1, PM_METRIC_AVAILABILITY_AVAILABLE, 1}, {2, PM_METRIC_AVAILABILITY_AVAILABLE, 1}};
    std::array<const void*, 2> metricDeviceEntries{&metricDevices[0], &metricDevices[1]};
    PM_INTROSPECTION_OBJARRAY metricDeviceArray{metricDeviceEntries.data(), metricDeviceEntries.size()};
    PM_INTROSPECTION_METRIC metrics[] = {
        {PM_METRIC_CPU_UTILIZATION, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_PERCENT, PM_UNIT_PERCENT,
            &doubleType, &statArray, &metricDeviceArray},
        {PM_METRIC_GPU_UTILIZATION, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_PERCENT, PM_UNIT_PERCENT,
            &doubleType, &statArray, &metricDeviceArray},
        {PM_METRIC_GPU_FREQUENCY, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_HERTZ, PM_UNIT_MEGAHERTZ,
            &doubleType, &statArray, &metricDeviceArray},
        {PM_METRIC_GPU_MEM_USED, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_BYTES, PM_UNIT_BYTES,
            &doubleType, &statArray, &metricDeviceArray}};
    std::array<const void*, 4> metricEntries{&metrics[0], &metrics[1], &metrics[2], &metrics[3]};
    PM_INTROSPECTION_OBJARRAY metricArray{metricEntries.data(), metricEntries.size()};
    PM_INTROSPECTION_ROOT root{&metricArray, nullptr, &deviceArray, nullptr};
    const auto capabilities = BuildPresentMonTelemetryCapabilities(&root);
    const auto plan = BuildPresentMonSystemQueryPlan(capabilities);
    ok &= Check(plan.elements.size() == 4 && plan.bindings.size() == 4,
        "system metrics share one query");
    ok &= Check(plan.elements[0].deviceId == 2 && plan.elements[1].deviceId == 1,
        "system and Intel device selection is capability driven");

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
    CheckSystemTelemetry(ok);
    return ok ? 0 : 1;
}
