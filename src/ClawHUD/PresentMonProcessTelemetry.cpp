#include "PresentMonProcessTelemetry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace clawhud
{
namespace
{
constexpr std::uint32_t kIndependentDeviceId = 0;
constexpr double kFpsWindowMs = 1000.0;
constexpr double kFpsOffsetMs = 0.0;
constexpr std::uint32_t kInitialSwapChainCapacity = 4;
constexpr std::uint32_t kMaximumSwapChainCapacity = 64;

const PresentMonMetricCapability* FindMetric(
    const PresentMonTelemetryCapabilities& capabilities, PM_METRIC metric)
{
    for (const auto& candidate : capabilities.metrics)
        if (candidate.id == metric)
            return &candidate;
    return nullptr;
}

bool HasAvailableIndependentMetric(
    const PresentMonMetricCapability& metric)
{
    for (const auto& device : metric.devices)
    {
        if (device.deviceId == kIndependentDeviceId &&
            device.availability == PM_METRIC_AVAILABILITY_AVAILABLE &&
            device.arraySize > 0)
            return true;
    }
    return false;
}
}

std::optional<PresentMonProcessQueryPlan> BuildPresentMonProcessQueryPlan(
    const PresentMonTelemetryCapabilities& capabilities)
{
    const auto* metric = FindMetric(capabilities, PM_METRIC_DISPLAYED_FPS);
    if (!metric || metric->type != PM_METRIC_TYPE_DYNAMIC ||
        metric->polledType != PM_DATA_TYPE_DOUBLE ||
        !HasAvailableIndependentMetric(*metric))
        return std::nullopt;

    PM_STAT statistic = PM_STAT_NONE;
    if (std::find(metric->statistics.begin(), metric->statistics.end(),
            PM_STAT_AVG) != metric->statistics.end())
        statistic = PM_STAT_AVG;
    else if (std::find(metric->statistics.begin(), metric->statistics.end(),
            PM_STAT_NEWEST_POINT) != metric->statistics.end())
        statistic = PM_STAT_NEWEST_POINT;
    else
        return std::nullopt;

    return PresentMonProcessQueryPlan{
        PM_QUERY_ELEMENT{
            PM_METRIC_DISPLAYED_FPS, statistic, kIndependentDeviceId, 0, 0, 0}};
}

std::optional<double> DecodePresentMonDisplayedFps(
    std::span<const std::uint8_t> blob, const PM_QUERY_ELEMENT& element)
{
    if (element.dataSize != sizeof(double) ||
        element.dataOffset > blob.size() ||
        element.dataSize > blob.size() - element.dataOffset)
        return std::nullopt;

    double value{};
    std::memcpy(&value, blob.data() + element.dataOffset, sizeof(value));
    if (!std::isfinite(value) || value < 0.0)
        return std::nullopt;
    return value;
}

std::optional<double> SelectPresentMonDisplayedFps(
    std::span<const std::optional<double>> values) noexcept
{
    std::optional<double> selected;
    for (const auto& value : values)
    {
        if (value && std::isfinite(*value) && *value >= 0.0 &&
            (!selected || *value > *selected))
            selected = *value;
    }
    return selected;
}

bool PresentMonProcessTelemetry::Initialize(
    PresentMonApi2Client& client,
    const PresentMonTelemetryCapabilities& capabilities)
{
    Shutdown(client);
    const auto plan = BuildPresentMonProcessQueryPlan(capabilities);
    if (!plan)
        return false;

    fpsElement_ = plan->element;
    if (client.RegisterDynamicQuery(&query_, &fpsElement_, 1,
            kFpsWindowMs, kFpsOffsetMs) != PM_STATUS_SUCCESS ||
        query_ == nullptr || fpsElement_.dataSize != sizeof(double))
    {
        if (query_)
            client.FreeDynamicQuery(query_);
        query_ = nullptr;
        fpsElement_ = {};
        return false;
    }

    if (fpsElement_.dataOffset > std::numeric_limits<std::uint64_t>::max() -
        fpsElement_.dataSize)
    {
        client.FreeDynamicQuery(query_);
        query_ = nullptr;
        fpsElement_ = {};
        return false;
    }
    blobSize_ = fpsElement_.dataOffset + fpsElement_.dataSize;
    ready_ = true;
    return true;
}

void PresentMonProcessTelemetry::Shutdown(PresentMonApi2Client& client) noexcept
{
    if (ownsTracking_)
        client.StopTrackingProcess(trackedProcessId_);
    ClearTracking();
    if (query_)
        client.FreeDynamicQuery(query_);
    query_ = nullptr;
    fpsElement_ = {};
    blobSize_ = 0;
    ready_ = false;
}

bool PresentMonProcessTelemetry::SwitchProcess(
    PresentMonApi2Client& client, std::uint32_t processId)
{
    if (ownsTracking_ && trackedProcessId_ == processId)
        return true;

    if (ownsTracking_)
    {
        client.StopTrackingProcess(trackedProcessId_);
        ClearTracking();
    }

    if (client.StartTrackingProcess(processId) != PM_STATUS_SUCCESS)
    {
        ClearTracking();
        return false;
    }
    trackedProcessId_ = processId;
    ownsTracking_ = true;
    return true;
}

void PresentMonProcessTelemetry::ClearTracking() noexcept
{
    trackedProcessId_ = 0;
    ownsTracking_ = false;
}

std::optional<PresentMonProcessSnapshot> PresentMonProcessTelemetry::Read(
    PresentMonApi2Client& client, std::uint32_t processId)
{
    if (!ready_ || processId == 0 || !SwitchProcess(client, processId))
        return std::nullopt;

    std::uint32_t capacity = kInitialSwapChainCapacity;
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        if (blobSize_ > std::numeric_limits<std::size_t>::max() / capacity)
            return std::nullopt;
        std::vector<std::uint8_t> blob(static_cast<std::size_t>(blobSize_) * capacity);
        std::uint32_t count = capacity;
        const auto status = client.PollDynamicQuery(
            query_, processId, blob.data(), &count);
        if (status == PM_STATUS_INSUFFICIENT_BUFFER)
        {
            const auto requested = count > capacity ? count : capacity * 2;
            if (requested <= capacity || requested > kMaximumSwapChainCapacity)
                return std::nullopt;
            capacity = requested;
            continue;
        }
        if (status == PM_STATUS_INVALID_PID)
        {
            ClearTracking();
            return std::nullopt;
        }
        if (status != PM_STATUS_SUCCESS || count > capacity)
            return std::nullopt;

        std::vector<std::optional<double>> values;
        values.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto offset = static_cast<std::size_t>(blobSize_) * i;
            values.push_back(DecodePresentMonDisplayedFps(
                std::span<const std::uint8_t>(blob).subspan(offset,
                    static_cast<std::size_t>(blobSize_)), fpsElement_));
        }
        return PresentMonProcessSnapshot{
            processId, SelectPresentMonDisplayedFps(values)};
    }
    return std::nullopt;
}
}
