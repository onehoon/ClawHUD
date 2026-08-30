#include "PresentMonSystemTelemetry.h"

#include "PresentMonApi2Client.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace clawhud
{
namespace
{
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
    if (HasStat(metric, PM_STAT_NEWEST_POINT)) return PM_STAT_NEWEST_POINT;
    if (HasStat(metric, PM_STAT_AVG)) return PM_STAT_AVG;
    return std::nullopt;
}
bool SupportedType(PM_DATA_TYPE type)
{ return type == PM_DATA_TYPE_DOUBLE || type == PM_DATA_TYPE_UINT64 || type == PM_DATA_TYPE_UINT32 || type == PM_DATA_TYPE_INT32; }
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
    if (metric.type != PM_METRIC_TYPE_DYNAMIC || !SupportedType(metric.polledType)) return;
    const auto stat = Statistic(metric); if (!stat) return;
    bool available{}; for (const auto& info : metric.devices)
        if (info.deviceId == deviceId && info.availability == PM_METRIC_AVAILABILITY_AVAILABLE) available = true;
    if (!available) return;
    plan.bindings.push_back({ slot, plan.elements.size(), metric.polledType, metric.unit });
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

bool PresentMonSystemTelemetry::Initialize(PresentMonApi2Client& client,
    const PresentMonTelemetryCapabilities& capabilities)
{
    Shutdown(client);
    const auto plan = BuildPresentMonSystemQueryPlan(capabilities);
    if (plan.elements.empty() || client.SetTelemetryPollingPeriod(0, 250) != PM_STATUS_SUCCESS) return false;
    elements_ = plan.elements; bindings_ = plan.bindings;
    if (client.RegisterDynamicQuery(&query_, elements_.data(), elements_.size(), 1000, 0) != PM_STATUS_SUCCESS)
    { query_ = nullptr; elements_.clear(); bindings_.clear(); return false; }
    std::uint64_t end{}; for (const auto& e : elements_) end = std::max(end, e.dataOffset + e.dataSize);
    blob_.resize(static_cast<size_t>(std::max<std::uint64_t>(end, 4096)));
    return true;
}
void PresentMonSystemTelemetry::Shutdown(PresentMonApi2Client& client) noexcept
{
    if (query_) client.FreeDynamicQuery(query_);
    query_ = nullptr; elements_.clear(); bindings_.clear(); blob_.clear();
}
std::optional<PresentMonSystemSnapshot> PresentMonSystemTelemetry::Read(PresentMonApi2Client& client)
{
    if (!query_) return std::nullopt;
    std::uint32_t resultCount = 1;
    const auto status = client.PollDynamicQuery(query_, kSystemTelemetryProcessId, blob_.data(), &resultCount);
    if (!HasPresentMonDynamicQueryResult(status, resultCount)) return std::nullopt;
    PresentMonSystemSnapshot snapshot;
    for (const auto& binding : bindings_)
    {
        const auto& element = elements_[binding.elementIndex];
        switch (binding.slot)
        {
        case SystemMetricSlot::CpuUsage: snapshot.cpuUsagePercent = DecodePresentMonPercentage(blob_.data(), element, binding.type); break;
        case SystemMetricSlot::GpuUsage: snapshot.gpuUsagePercent = DecodePresentMonPercentage(blob_.data(), element, binding.type); break;
        case SystemMetricSlot::GpuFrequency: snapshot.gpuClockMHz = DecodePresentMonFrequencyMHz(blob_.data(), element, binding.type, binding.unit); break;
        case SystemMetricSlot::GpuMemoryUsed: snapshot.gpuMemoryUsedBytes = DecodePresentMonMemoryBytes(blob_.data(), element, binding.type, binding.unit); break;
        }
    }
    return snapshot;
}
}
