#include "PresentMonSystemTelemetry.h"

#include "PresentMonApi2Client.h"
#include "RuntimeLogger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace clawhud
{
bool SupportsPresentMonDynamicQuery(PM_METRIC_TYPE type) noexcept
{
    return type == PM_METRIC_TYPE_DYNAMIC || type == PM_METRIC_TYPE_DYNAMIC_FRAME;
}

namespace
{
constexpr double kSystemTelemetryWindowSizeMs = 1000.0;
constexpr double kSystemTelemetryMetricOffsetMs = 1064.0;

const PresentMonMetricCapability* Metric(
    const PresentMonTelemetryCapabilities& capabilities, PM_METRIC id)
{
    for (const auto& metric : capabilities.metrics) if (metric.id == id) return &metric;
    return nullptr;
}
const PresentMonDeviceCapability* Device(
    const PresentMonTelemetryCapabilities& capabilities, PM_DEVICE_TYPE type,
    PM_DEVICE_VENDOR vendor, PM_METRIC metric)
{
    for (const auto& device : capabilities.devices)
        if (device.type == type && device.vendor == vendor)
        {
            const auto* found = Metric(capabilities, metric);
            if (found) for (const auto& info : found->devices)
                if (info.deviceId == device.id &&
                    info.availability == PM_METRIC_AVAILABILITY_AVAILABLE) return &device;
        }
    return nullptr;
}
const PresentMonDeviceCapability* SystemDevice(
    const PresentMonTelemetryCapabilities& capabilities, PM_METRIC metric)
{
    const auto* found = Metric(capabilities, metric);
    if (!found) return nullptr;
    for (const auto& device : capabilities.devices)
        if (device.type == PM_DEVICE_TYPE_SYSTEM)
            for (const auto& info : found->devices)
                if (info.deviceId == device.id && info.availability == PM_METRIC_AVAILABILITY_AVAILABLE)
                    return &device;
    return nullptr;
}
bool HasStat(const PresentMonMetricCapability& metric, PM_STAT stat)
{ for (const auto supported : metric.statistics) if (supported == stat) return true; return false; }
std::optional<PM_STAT> Statistic(const PresentMonMetricCapability& metric)
{
    if (HasStat(metric, PM_STAT_AVG)) return PM_STAT_AVG;
    if (HasStat(metric, PM_STAT_NON_ZERO_AVG)) return PM_STAT_NON_ZERO_AVG;
    if (HasStat(metric, PM_STAT_NEWEST_POINT)) return PM_STAT_NEWEST_POINT;
    if (HasStat(metric, PM_STAT_MID_POINT)) return PM_STAT_MID_POINT;
    if (HasStat(metric, PM_STAT_OLDEST_POINT)) return PM_STAT_OLDEST_POINT;
    return std::nullopt;
}
PM_DATA_TYPE OutputType(PM_STAT stat, PM_DATA_TYPE polledType)
{
    if (stat == PM_STAT_AVG || stat == PM_STAT_NON_ZERO_AVG)
        return PM_DATA_TYPE_DOUBLE;
    return polledType;
}
bool SupportedType(PM_DATA_TYPE type)
{ return type == PM_DATA_TYPE_DOUBLE || type == PM_DATA_TYPE_UINT64 || type == PM_DATA_TYPE_UINT32 || type == PM_DATA_TYPE_INT32; }
std::wstring Status(PM_STATUS status)
{ return std::to_wstring(static_cast<int>(status)); }
std::wstring OptionalDouble(const std::optional<double>& value)
{ return value ? std::to_wstring(*value) : L"n/a"; }
std::wstring OptionalBytes(const std::optional<std::uint64_t>& value)
{ return value ? std::to_wstring(*value) : L"n/a"; }
std::optional<double> Number(const std::uint8_t* blob, const PM_QUERY_ELEMENT& e, PM_DATA_TYPE type)
{
    if (!blob || !SupportedType(type)) return std::nullopt;
    if ((type == PM_DATA_TYPE_DOUBLE || type == PM_DATA_TYPE_UINT64) && e.dataSize < 8) return std::nullopt;
    if ((type == PM_DATA_TYPE_UINT32 || type == PM_DATA_TYPE_INT32) && e.dataSize < 4) return std::nullopt;
    if (type == PM_DATA_TYPE_DOUBLE) { double v{}; std::memcpy(&v, blob + e.dataOffset, 8); return v; }
    if (type == PM_DATA_TYPE_UINT64) { std::uint64_t v{}; std::memcpy(&v, blob + e.dataOffset, 8); return static_cast<double>(v); }
    if (type == PM_DATA_TYPE_UINT32) { std::uint32_t v{}; std::memcpy(&v, blob + e.dataOffset, 4); return static_cast<double>(v); }
    std::int32_t v{}; std::memcpy(&v, blob + e.dataOffset, 4); return static_cast<double>(v);
}
bool Valid(double value) { return std::isfinite(value) && value >= 0.0; }
double UnitScale(PM_UNIT unit)
{
    switch (unit) { case PM_UNIT_KILOBYTES: case PM_UNIT_KILOHERTZ: return 1000.0;
    case PM_UNIT_MEGABYTES: case PM_UNIT_MEGAHERTZ: return 1000000.0;
    case PM_UNIT_GIGABYTES: case PM_UNIT_GIGAHERTZ: return 1000000000.0;
    default: return 1.0; }
}
void AddMetric(PresentMonSystemQueryPlan& plan, const PresentMonMetricCapability& metric,
    std::uint32_t deviceId, SystemMetricSlot slot)
{
    if (!SupportsPresentMonDynamicQuery(metric.type) || !SupportedType(metric.polledType)) return;
    const auto stat = Statistic(metric); if (!stat) return;
    bool available{}; for (const auto& info : metric.devices)
        if (info.deviceId == deviceId && info.availability == PM_METRIC_AVAILABILITY_AVAILABLE) available = true;
    if (!available) return;
    plan.bindings.push_back({ slot, plan.elements.size(), OutputType(*stat, metric.polledType), metric.unit });
    plan.elements.push_back({ metric.id, *stat, deviceId, 0, 0, 0 });
}
}

PresentMonSystemQueryPlan BuildPresentMonSystemQueryPlan(
    const PresentMonTelemetryCapabilities& capabilities)
{
    PresentMonSystemQueryPlan plan;
    const auto add = [&](PM_METRIC metric, PM_DEVICE_TYPE type, PM_DEVICE_VENDOR vendor, SystemMetricSlot slot)
    {
        const auto* metadata = Metric(capabilities, metric); if (!metadata) return;
        const auto* device = type == PM_DEVICE_TYPE_SYSTEM
            ? SystemDevice(capabilities, metric) : Device(capabilities, type, vendor, metric);
        if (device) AddMetric(plan, *metadata, device->id, slot);
    };
    add(PM_METRIC_CPU_UTILIZATION, PM_DEVICE_TYPE_SYSTEM, PM_DEVICE_VENDOR_UNKNOWN, SystemMetricSlot::CpuUsage);
    add(PM_METRIC_GPU_UTILIZATION, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_DEVICE_VENDOR_INTEL, SystemMetricSlot::GpuUsage);
    add(PM_METRIC_GPU_FREQUENCY, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_DEVICE_VENDOR_INTEL, SystemMetricSlot::GpuFrequency);
    add(PM_METRIC_GPU_MEM_USED, PM_DEVICE_TYPE_GRAPHICS_ADAPTER, PM_DEVICE_VENDOR_INTEL, SystemMetricSlot::GpuMemoryUsed);
    return plan;
}

std::optional<double> DecodePresentMonPercentage(const std::uint8_t* blob,
    const PM_QUERY_ELEMENT& element, PM_DATA_TYPE type)
{
    const auto value = Number(blob, element, type); if (!value || !Valid(*value)) return std::nullopt;
    if (*value > 100.0) return *value <= 100.5 ? std::optional<double>(100.0) : std::nullopt;
    return value;
}
std::optional<double> DecodePresentMonFrequencyMHz(const std::uint8_t* blob,
    const PM_QUERY_ELEMENT& element, PM_DATA_TYPE type, PM_UNIT unit)
{
    const auto value = Number(blob, element, type); if (!value || !Valid(*value)) return std::nullopt;
    double result = *value;
    if (unit == PM_UNIT_HERTZ) result /= 1000000.0;
    else if (unit == PM_UNIT_KILOHERTZ) result /= 1000.0;
    else if (unit == PM_UNIT_GIGAHERTZ) result *= 1000.0;
    return std::isfinite(result) && result >= 0.0 ? std::optional<double>(result) : std::nullopt;
}
std::optional<std::uint64_t> DecodePresentMonMemoryBytes(const std::uint8_t* blob,
    const PM_QUERY_ELEMENT& element, PM_DATA_TYPE type, PM_UNIT unit)
{
    const auto value = Number(blob, element, type); if (!value || !Valid(*value)) return std::nullopt;
    const double result = *value * UnitScale(unit);
    if (!std::isfinite(result) || result > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) return std::nullopt;
    return static_cast<std::uint64_t>(result);
}
bool HasPresentMonDynamicQueryResult(PM_STATUS status, std::uint32_t resultCount) noexcept
{ return status == PM_STATUS_SUCCESS && resultCount != 0; }
std::optional<PresentMonSystemSnapshot> DecodePresentMonSystemSnapshot(
    PM_STATUS status, std::uint32_t resultCount, const std::uint8_t* blob,
    const std::vector<PM_QUERY_ELEMENT>& elements,
    const std::vector<SystemMetricBinding>& bindings)
{
    if (!HasPresentMonDynamicQueryResult(status, resultCount) || !blob) return std::nullopt;
    PresentMonSystemSnapshot snapshot;
    for (const auto& binding : bindings)
    {
        const auto& element = elements[binding.elementIndex];
        switch (binding.slot)
        {
        case SystemMetricSlot::CpuUsage: snapshot.cpuUsagePercent = DecodePresentMonPercentage(blob, element, binding.type); break;
        case SystemMetricSlot::GpuUsage: snapshot.gpuUsagePercent = DecodePresentMonPercentage(blob, element, binding.type); break;
        case SystemMetricSlot::GpuFrequency: snapshot.gpuClockMHz = DecodePresentMonFrequencyMHz(blob, element, binding.type, binding.unit); break;
        case SystemMetricSlot::GpuMemoryUsed: snapshot.gpuMemoryUsedBytes = DecodePresentMonMemoryBytes(blob, element, binding.type, binding.unit); break;
        }
    }
    return snapshot;
}

bool PresentMonSystemTelemetry::Initialize(PresentMonApi2Client& client,
    const PresentMonTelemetryCapabilities& capabilities)
{
    Shutdown(client);
    const auto plan = BuildPresentMonSystemQueryPlan(capabilities);
    const auto bound = [&](SystemMetricSlot slot)
    {
        return std::any_of(plan.bindings.begin(), plan.bindings.end(),
            [slot](const auto& binding) { return binding.slot == slot; });
    };
    RuntimeLogger::Log(RuntimeLogLevel::Info,
        L"[PresentMonSystem] query-plan elements=" +
        std::to_wstring(plan.elements.size()) + L" cpu=" +
        std::to_wstring(bound(SystemMetricSlot::CpuUsage)) + L" gpuUsage=" +
        std::to_wstring(bound(SystemMetricSlot::GpuUsage)) + L" gpuClock=" +
        std::to_wstring(bound(SystemMetricSlot::GpuFrequency)) + L" vram=" +
        std::to_wstring(bound(SystemMetricSlot::GpuMemoryUsed)));
    if (plan.elements.empty())
    {
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"[PresentMonSystem] initialized ready=0 stage=query-plan");
        return false;
    }
    const auto pollingStatus = client.SetTelemetryPollingPeriod(0, 250);
    RuntimeLogger::Log(RuntimeLogLevel::Info,
        L"[PresentMonSystem] polling-period status=" + Status(pollingStatus));
    if (pollingStatus != PM_STATUS_SUCCESS)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"[PresentMonSystem] initialized ready=0 stage=polling-period status=" +
            Status(pollingStatus));
        return false;
    }
    elements_ = plan.elements; bindings_ = plan.bindings;
    const auto registerStatus = client.RegisterDynamicQuery(
        &query_, elements_.data(), elements_.size(),
        kSystemTelemetryWindowSizeMs, kSystemTelemetryMetricOffsetMs);
    RuntimeLogger::Log(RuntimeLogLevel::Info,
        L"[PresentMonSystem] register-query status=" + Status(registerStatus) +
        L" elements=" + std::to_wstring(elements_.size()) +
        L" windowMs=" + std::to_wstring(kSystemTelemetryWindowSizeMs) +
        L" offsetMs=" + std::to_wstring(kSystemTelemetryMetricOffsetMs));
    if (registerStatus != PM_STATUS_SUCCESS)
    {
        query_ = nullptr; elements_.clear(); bindings_.clear();
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"[PresentMonSystem] initialized ready=0 stage=register-query status=" +
            Status(registerStatus));
        return false;
    }
    std::uint64_t end{}; for (const auto& e : elements_) end = std::max(end, e.dataOffset + e.dataSize);
    blob_.resize(static_cast<size_t>(std::max<std::uint64_t>(end, 4096)));
    RuntimeLogger::Log(RuntimeLogLevel::Info,
        L"[PresentMonSystem] initialized ready=1");
    return true;
}
void PresentMonSystemTelemetry::Shutdown(PresentMonApi2Client& client) noexcept
{
    if (query_) client.FreeDynamicQuery(query_);
    query_ = nullptr; elements_.clear(); bindings_.clear(); blob_.clear();
    pollDiagnosticsInitialized_ = false;
    lastPollStatus_ = {};
    lastPollResultCount_ = 0;
    firstSampleLogged_ = false;
}
std::optional<PresentMonSystemSnapshot> PresentMonSystemTelemetry::Read(PresentMonApi2Client& client)
{
    if (!query_) return std::nullopt;
    std::uint32_t resultCount = 1;
    const auto status = client.PollDynamicQuery(query_, kSystemTelemetryProcessId, blob_.data(), &resultCount);
    if (!pollDiagnosticsInitialized_ || status != lastPollStatus_ ||
        resultCount != lastPollResultCount_)
    {
        RuntimeLogger::Log(status == PM_STATUS_SUCCESS && resultCount != 0
            ? RuntimeLogLevel::Debug : RuntimeLogLevel::Warn,
            L"[PresentMonSystem] poll pid=0 status=" + Status(status) +
            L" resultCount=" + std::to_wstring(resultCount));
        pollDiagnosticsInitialized_ = true;
        lastPollStatus_ = status;
        lastPollResultCount_ = resultCount;
    }
    const auto snapshot = DecodePresentMonSystemSnapshot(
        status, resultCount, blob_.data(), elements_, bindings_);
    if (snapshot && !firstSampleLogged_)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Info,
            L"[PresentMonSystem] first-sample cpu=" +
            OptionalDouble(snapshot->cpuUsagePercent) + L" gpu=" +
            OptionalDouble(snapshot->gpuUsagePercent) + L" clockMHz=" +
            OptionalDouble(snapshot->gpuClockMHz) + L" vramBytes=" +
            OptionalBytes(snapshot->gpuMemoryUsedBytes));
        firstSampleLogged_ = true;
    }
    return snapshot;
}
}
