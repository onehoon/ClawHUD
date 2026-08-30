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
constexpr std::uint64_t kDynamicQueryBlobAlignment = 16;

const PresentMonMetricCapability* FindMetric(
    const PresentMonTelemetryCapabilities& capabilities, PM_METRIC metric)
{
    for (const auto& candidate : capabilities.metrics)
        if (candidate.id == metric)
            return &candidate;
    return nullptr;
}

bool HasAvailableIndependentMetric(const PresentMonMetricCapability& metric)
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

bool HasStat(const PresentMonMetricCapability& metric, PM_STAT stat)
{
    return std::find(metric.statistics.begin(), metric.statistics.end(), stat) !=
        metric.statistics.end();
}

bool SupportsAvgFpsMetric(
    const PresentMonTelemetryCapabilities& capabilities, PM_METRIC id)
{
    const auto* metric = FindMetric(capabilities, id);
    return metric && metric->type == PM_METRIC_TYPE_DYNAMIC &&
        metric->polledType == PM_DATA_TYPE_DOUBLE &&
        HasAvailableIndependentMetric(*metric) && HasStat(*metric, PM_STAT_AVG);
}

PM_QUERY_ELEMENT MakeElement(PM_METRIC metric, PM_STAT stat)
{
    return PM_QUERY_ELEMENT{metric, stat, kIndependentDeviceId, 0, 0, 0};
}
}

std::optional<PresentMonProcessQueryPlan> BuildPresentMonProcessQueryPlan(
    const PresentMonTelemetryCapabilities& capabilities)
{
    if (!SupportsAvgFpsMetric(capabilities, PM_METRIC_DISPLAYED_FPS))
        return std::nullopt;

    PresentMonProcessQueryPlan plan;
    plan.displayedIndex = plan.elements.size();
    plan.elements.push_back(MakeElement(PM_METRIC_DISPLAYED_FPS, PM_STAT_AVG));

    if (SupportsAvgFpsMetric(capabilities, PM_METRIC_PRESENTED_FPS))
    {
        plan.presentedIndex = plan.elements.size();
        plan.elements.push_back(MakeElement(PM_METRIC_PRESENTED_FPS, PM_STAT_AVG));
    }

    if (const auto* address = FindMetric(capabilities, PM_METRIC_SWAP_CHAIN_ADDRESS);
        address && address->type == PM_METRIC_TYPE_DYNAMIC &&
        address->polledType == PM_DATA_TYPE_UINT64 &&
        HasAvailableIndependentMetric(*address))
    {
        const PM_STAT stat = HasStat(*address, PM_STAT_NEWEST_POINT)
            ? PM_STAT_NEWEST_POINT
            : (HasStat(*address, PM_STAT_MID_POINT) ? PM_STAT_MID_POINT
                                                    : PM_STAT_NONE);
        if (stat != PM_STAT_NONE)
        {
            plan.swapChainAddressIndex = plan.elements.size();
            plan.elements.push_back(
                MakeElement(PM_METRIC_SWAP_CHAIN_ADDRESS, stat));
        }
    }

    return plan;
}

std::optional<double> DecodePresentMonFps(
    std::span<const std::uint8_t> blob, const PM_QUERY_ELEMENT& element)
{
    if (element.dataSize != sizeof(double) ||
        element.dataOffset > blob.size() ||
        element.dataSize > blob.size() - element.dataOffset)
        return std::nullopt;

    double value{};
    std::memcpy(&value, blob.data() + element.dataOffset, sizeof(value));
    if (!std::isfinite(value) || value <= 0.0)
        return std::nullopt;
    return value;
}

std::optional<std::uint64_t> DecodePresentMonSwapChainAddress(
    std::span<const std::uint8_t> blob, const PM_QUERY_ELEMENT& element)
{
    if (element.dataSize != sizeof(std::uint64_t) ||
        element.dataOffset > blob.size() ||
        element.dataSize > blob.size() - element.dataOffset)
        return std::nullopt;

    std::uint64_t value{};
    std::memcpy(&value, blob.data() + element.dataOffset, sizeof(value));
    if (value == 0)
        return std::nullopt;
    return value;
}

bool PresentMonProcessTelemetry::Initialize(
    PresentMonApi2Client& client,
    const PresentMonTelemetryCapabilities& capabilities)
{
    Shutdown(client);
    auto plan = BuildPresentMonProcessQueryPlan(capabilities);
    if (!plan)
        return false;
    plan_ = std::move(*plan);
    ready_ = true;
    return true;
}

void PresentMonProcessTelemetry::Shutdown(PresentMonApi2Client& client) noexcept
{
    ReleaseTarget(client);
    plan_ = {};
    ready_ = false;
}

void PresentMonProcessTelemetry::ReleaseTarget(
    PresentMonApi2Client& client) noexcept
{
    if (query_)
        client.FreeDynamicQuery(query_);
    query_ = nullptr;
    queryElements_.clear();
    blobSize_ = 0;
    if (ownsTracking_)
        client.StopTrackingProcess(trackedProcessId_);
    trackedProcessId_ = 0;
    ownsTracking_ = false;
}

bool PresentMonProcessTelemetry::RetargetProcess(
    PresentMonApi2Client& client, std::uint32_t processId)
{
    ReleaseTarget(client);

    if (client.StartTrackingProcess(processId) != PM_STATUS_SUCCESS)
    {
        ReleaseTarget(client);
        return false;
    }
    trackedProcessId_ = processId;
    ownsTracking_ = true;

    queryElements_ = plan_.elements;
    if (client.RegisterDynamicQuery(&query_, queryElements_.data(),
            queryElements_.size(), kPresentMonFpsWindowMs,
            kPresentMonFpsOffsetMs) != PM_STATUS_SUCCESS ||
        query_ == nullptr)
    {
        query_ = nullptr;
        ReleaseTarget(client);
        return false;
    }

    std::uint64_t end = 0;
    for (const auto& element : queryElements_)
    {
        if (element.dataOffset >
            std::numeric_limits<std::uint64_t>::max() - element.dataSize)
        {
            ReleaseTarget(client);
            return false;
        }
        end = std::max(end, element.dataOffset + element.dataSize);
    }
    constexpr auto alignment = kDynamicQueryBlobAlignment;
    if (end > std::numeric_limits<std::uint64_t>::max() - (alignment - 1))
    {
        ReleaseTarget(client);
        return false;
    }
    blobSize_ = (end + (alignment - 1)) & ~(alignment - 1);
    if (blobSize_ == 0)
        blobSize_ = alignment;
    return true;
}

std::optional<PresentMonProcessSnapshot> PresentMonProcessTelemetry::Read(
    PresentMonApi2Client& client, std::uint32_t processId)
{
    if (!ready_)
        return std::nullopt;
    if (processId == 0)
    {
        ReleaseTarget(client);
        return std::nullopt;
    }
    if (query_ == nullptr || trackedProcessId_ != processId)
    {
        if (!RetargetProcess(client, processId))
            return std::nullopt;
    }

    std::vector<std::uint8_t> blob(static_cast<std::size_t>(blobSize_));
    std::uint32_t swapChainCount = 1;
    const auto status = client.PollDynamicQuery(
        query_, processId, blob.data(), &swapChainCount);
    if (status == PM_STATUS_INVALID_PID)
    {
        ReleaseTarget(client);
        return std::nullopt;
    }
    if (status != PM_STATUS_SUCCESS || swapChainCount == 0)
        return std::nullopt;

    const std::span<const std::uint8_t> view(blob);
    PresentMonProcessSnapshot snapshot;
    snapshot.processId = processId;
    snapshot.displayedFps =
        DecodePresentMonFps(view, queryElements_[plan_.displayedIndex]);
    if (plan_.presentedIndex)
        snapshot.presentedFps =
            DecodePresentMonFps(view, queryElements_[*plan_.presentedIndex]);
    if (plan_.swapChainAddressIndex)
        snapshot.swapChainAddress = DecodePresentMonSwapChainAddress(
            view, queryElements_[*plan_.swapChainAddressIndex]);
    return snapshot;
}
}
