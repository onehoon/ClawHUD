#include "PresentMonFrameTelemetry.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace clawhud
{
namespace
{
constexpr std::uint32_t kIndependentDeviceId = 0;
constexpr std::uint32_t kFrameConsumeBatch = 64;

const PresentMonMetricCapability* FindMetric(
    const PresentMonTelemetryCapabilities& capabilities, PM_METRIC metric)
{
    for (const auto& candidate : capabilities.metrics)
        if (candidate.id == metric)
            return &candidate;
    return nullptr;
}

bool SupportsFrameMetric(const PresentMonMetricCapability& metric)
{
    if (metric.type != PM_METRIC_TYPE_FRAME_EVENT &&
        metric.type != PM_METRIC_TYPE_DYNAMIC_FRAME)
        return false;
    for (const auto& device : metric.devices)
        if (device.deviceId == kIndependentDeviceId &&
            device.availability == PM_METRIC_AVAILABILITY_AVAILABLE)
            return true;
    return false;
}

bool HasFrameMetric(
    const PresentMonTelemetryCapabilities& capabilities, PM_METRIC id)
{
    const auto* metric = FindMetric(capabilities, id);
    return metric && SupportsFrameMetric(*metric);
}

PM_QUERY_ELEMENT MakeElement(PM_METRIC metric)
{
    return PM_QUERY_ELEMENT{metric, PM_STAT_NONE, kIndependentDeviceId, 0, 0, 0};
}
}

std::optional<PresentMonFrameQueryPlan> BuildPresentMonFrameQueryPlan(
    const PresentMonTelemetryCapabilities& capabilities)
{
    if (!HasFrameMetric(capabilities, PM_METRIC_BETWEEN_DISPLAY_CHANGE))
        return std::nullopt;

    PresentMonFrameQueryPlan plan;
    plan.betweenDisplayChangeIndex = plan.elements.size();
    plan.elements.push_back(MakeElement(PM_METRIC_BETWEEN_DISPLAY_CHANGE));

    const auto addOptional = [&](PM_METRIC id, std::optional<std::size_t>& index)
    {
        if (!HasFrameMetric(capabilities, id))
            return;
        index = plan.elements.size();
        plan.elements.push_back(MakeElement(id));
    };
    addOptional(PM_METRIC_PROCESS_ID, plan.processIdIndex);
    addOptional(PM_METRIC_SWAP_CHAIN_ADDRESS, plan.swapChainAddressIndex);
    addOptional(PM_METRIC_FRAME_TYPE, plan.frameTypeIndex);
    addOptional(PM_METRIC_PRESENT_MODE, plan.presentModeIndex);

    return plan;
}

std::optional<double> DecodePresentMonFrameDouble(
    std::span<const std::uint8_t> record, const PM_QUERY_ELEMENT& element)
{
    if (element.dataSize != sizeof(double) ||
        element.dataOffset > record.size() ||
        element.dataSize > record.size() - element.dataOffset)
        return std::nullopt;

    double value{};
    std::memcpy(&value, record.data() + element.dataOffset, sizeof(value));
    if (!std::isfinite(value))
        return std::nullopt;
    return value;
}

bool PresentMonDisplayedFrameDetector::Observe(double betweenDisplayChangeMs) noexcept
{
    if (seen_)
        return false;
    if (!std::isfinite(betweenDisplayChangeMs) || betweenDisplayChangeMs <= 0.0)
        return false;
    seen_ = true;
    return true;
}

bool PresentMonFrameTelemetry::Initialize(
    PresentMonApi2Client& client,
    const PresentMonTelemetryCapabilities& capabilities)
{
    Shutdown(client);
    auto plan = BuildPresentMonFrameQueryPlan(capabilities);
    if (!plan)
        return false;

    plan_ = std::move(*plan);
    queryElements_ = plan_.elements;
    std::uint32_t blobSize = 0;
    if (client.RegisterFrameQuery(&query_, queryElements_.data(),
            queryElements_.size(), &blobSize) != PM_STATUS_SUCCESS ||
        query_ == nullptr || blobSize == 0)
    {
        query_ = nullptr;
        plan_ = {};
        queryElements_.clear();
        return false;
    }
    blobSize_ = blobSize;
    ready_ = true;
    return true;
}

void PresentMonFrameTelemetry::Shutdown(PresentMonApi2Client& client) noexcept
{
    if (query_)
        client.FreeFrameQuery(query_);
    query_ = nullptr;
    plan_ = {};
    queryElements_.clear();
    blobSize_ = 0;
    armedProcessId_ = 0;
    detector_.Reset();
    ready_ = false;
}

bool PresentMonFrameTelemetry::BeginVerification(
    PresentMonApi2Client& client, std::uint32_t processId)
{
    if (!ready_ || query_ == nullptr || processId == 0)
        return false;
    armedProcessId_ = processId;
    detector_.Reset();
    // Drop frames queued before this attempt so a previous verification (same
    // numeric PID, earlier generation, or PID reuse) cannot satisfy it.
    client.FlushFrames(processId);
    return true;
}

std::optional<bool> PresentMonFrameTelemetry::PollDisplayedFrame(
    PresentMonApi2Client& client, std::uint32_t processId)
{
    if (!ready_ || query_ == nullptr || processId == 0 ||
        processId != armedProcessId_)
        return std::nullopt;
    if (detector_.DisplayedFrameSeen())
        return true;

    std::vector<std::uint8_t> blob(
        static_cast<std::size_t>(blobSize_) * kFrameConsumeBatch);
    std::uint32_t count = kFrameConsumeBatch;
    const auto status = client.ConsumeFrames(
        query_, processId, blob.data(), &count);
    if (status != PM_STATUS_SUCCESS)
        return std::nullopt;

    const std::span<const std::uint8_t> view(blob);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto offset = static_cast<std::size_t>(i) * blobSize_;
        if (offset + blobSize_ > view.size())
            break;
        const auto record = view.subspan(offset, blobSize_);
        const auto interval = DecodePresentMonFrameDouble(
            record, queryElements_[plan_.betweenDisplayChangeIndex]);
        if (interval && detector_.Observe(*interval))
            return true;
    }
    return false;
}
}
