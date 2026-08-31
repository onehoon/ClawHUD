#include "PresentMonDebugFrameTelemetry.h"

#include <cmath>
#include <cstring>

namespace clawhud
{
namespace
{
constexpr std::uint32_t kIndependentDeviceId = 0;
constexpr std::uint32_t kDebugFrameConsumeBatch = 64;

const PresentMonMetricCapability* FindMetric(
    const PresentMonTelemetryCapabilities& capabilities, PM_METRIC metric)
{
    for (const auto& candidate : capabilities.metrics)
        if (candidate.id == metric)
            return &candidate;
    return nullptr;
}

bool SupportsFrameMetric(
    const PresentMonTelemetryCapabilities& capabilities, PM_METRIC id)
{
    const auto* metric = FindMetric(capabilities, id);
    if (!metric || (metric->type != PM_METRIC_TYPE_FRAME_EVENT &&
        metric->type != PM_METRIC_TYPE_DYNAMIC_FRAME))
        return false;
    for (const auto& device : metric->devices)
        if (device.deviceId == kIndependentDeviceId &&
            device.availability == PM_METRIC_AVAILABILITY_AVAILABLE)
            return true;
    return false;
}

PM_QUERY_ELEMENT MakeElement(PM_METRIC metric)
{
    return PM_QUERY_ELEMENT{metric, PM_STAT_NONE, kIndependentDeviceId, 0, 0, 0};
}

std::optional<double> DecodeDouble(std::span<const std::uint8_t> record,
    const PM_QUERY_ELEMENT& element)
{
    if (element.dataSize != sizeof(double) ||
        element.dataOffset > record.size() ||
        element.dataSize > record.size() - element.dataOffset)
        return std::nullopt;
    double value{};
    std::memcpy(&value, record.data() + element.dataOffset, sizeof(value));
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

std::optional<std::uint64_t> DecodeU64(std::span<const std::uint8_t> record,
    const PM_QUERY_ELEMENT& element)
{
    if (element.dataSize != sizeof(std::uint64_t) ||
        element.dataOffset > record.size() ||
        element.dataSize > record.size() - element.dataOffset)
        return std::nullopt;
    std::uint64_t value{};
    std::memcpy(&value, record.data() + element.dataOffset, sizeof(value));
    return value;
}

std::optional<std::int32_t> DecodeEnum(std::span<const std::uint8_t> record,
    const PM_QUERY_ELEMENT& element)
{
    if (element.dataSize < sizeof(std::int32_t) ||
        element.dataOffset > record.size() ||
        sizeof(std::int32_t) > record.size() - element.dataOffset)
        return std::nullopt;
    std::int32_t value{};
    std::memcpy(&value, record.data() + element.dataOffset, sizeof(value));
    return value;
}
}

PresentMonDebugFrameQueryPlan BuildPresentMonDebugFrameQueryPlan(
    const PresentMonTelemetryCapabilities& capabilities)
{
    PresentMonDebugFrameQueryPlan plan;
    const auto add = [&](PM_METRIC id, std::optional<std::size_t>& index)
    {
        if (!SupportsFrameMetric(capabilities, id))
            return;
        index = plan.elements.size();
        plan.elements.push_back(MakeElement(id));
    };
    add(PM_METRIC_BETWEEN_DISPLAY_CHANGE, plan.betweenDisplayChangeIndex);
    add(PM_METRIC_SWAP_CHAIN_ADDRESS, plan.swapChainAddressIndex);
    add(PM_METRIC_PRESENT_MODE, plan.presentModeIndex);
    add(PM_METRIC_FRAME_TYPE, plan.frameTypeIndex);
    return plan;
}

const char* PresentMonPresentModeName(std::int32_t presentMode) noexcept
{
    switch (presentMode)
    {
    case PM_PRESENT_MODE_HARDWARE_LEGACY_FLIP: return "HardwareLegacyFlip";
    case PM_PRESENT_MODE_HARDWARE_LEGACY_COPY_TO_FRONT_BUFFER:
        return "HardwareLegacyCopyToFrontBuffer";
    case PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP: return "HardwareIndependentFlip";
    case PM_PRESENT_MODE_COMPOSED_FLIP: return "ComposedFlip";
    case PM_PRESENT_MODE_COMPOSED_COPY_WITH_GPU_GDI: return "ComposedCopyWithGpuGdi";
    case PM_PRESENT_MODE_COMPOSED_COPY_WITH_CPU_GDI: return "ComposedCopyWithCpuGdi";
    case PM_PRESENT_MODE_HARDWARE_COMPOSED_INDEPENDENT_FLIP:
        return "HardwareComposedIndependentFlip";
    default: return "Unknown";
    }
}

const char* PresentMonFrameTypeName(std::int32_t frameType) noexcept
{
    switch (frameType)
    {
    case PM_FRAME_TYPE_UNSPECIFIED: return "Unspecified";
    case PM_FRAME_TYPE_APPLICATION: return "Application";
    case PM_FRAME_TYPE_REPEATED: return "Repeated";
    case PM_FRAME_TYPE_INTEL_XEFG: return "IntelXeFG";
    case PM_FRAME_TYPE_AMD_AFMF: return "AmdAFMF";
    default: return "NotSet";
    }
}

PresentMonDebugFrame FoldPresentMonDebugFrames(
    const PresentMonDebugFrameQueryPlan& plan,
    std::span<const std::uint8_t> blob, std::uint32_t frameCount,
    std::uint32_t recordSize)
{
    PresentMonDebugFrame frame;
    if (recordSize == 0)
        return frame;
    for (std::uint32_t i = 0; i < frameCount; ++i)
    {
        const std::size_t offset = static_cast<std::size_t>(i) * recordSize;
        if (offset + recordSize > blob.size())
            break;
        const auto record = blob.subspan(offset, recordSize);
        if (plan.betweenDisplayChangeIndex)
            if (auto value = DecodeDouble(record,
                plan.elements[*plan.betweenDisplayChangeIndex]))
                frame.betweenDisplayChangeMs = value;
        if (plan.swapChainAddressIndex)
            if (auto value = DecodeU64(record,
                plan.elements[*plan.swapChainAddressIndex]))
                frame.swapChainAddress = value;
        if (plan.presentModeIndex)
            if (auto value = DecodeEnum(record,
                plan.elements[*plan.presentModeIndex]))
                frame.presentMode = value;
        if (plan.frameTypeIndex)
            if (auto value = DecodeEnum(record,
                plan.elements[*plan.frameTypeIndex]))
                frame.frameType = value;
    }
    return frame;
}

bool PresentMonDebugFrameTelemetry::Initialize(
    PresentMonApi2Client& client,
    const PresentMonTelemetryCapabilities& capabilities)
{
    Shutdown(client);
    auto plan = BuildPresentMonDebugFrameQueryPlan(capabilities);
    if (plan.Empty())
        return false;
    plan_ = std::move(plan);
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

void PresentMonDebugFrameTelemetry::Shutdown(PresentMonApi2Client& client) noexcept
{
    if (query_)
        client.FreeFrameQuery(query_);
    query_ = nullptr;
    plan_ = {};
    queryElements_.clear();
    blobSize_ = 0;
    targetProcessId_ = 0;
    ready_ = false;
}

std::optional<PresentMonDebugFrame> PresentMonDebugFrameTelemetry::ReadLatest(
    PresentMonApi2Client& client, std::uint32_t processId)
{
    if (!ready_ || query_ == nullptr || processId == 0)
        return std::nullopt;
    if (processId != targetProcessId_)
    {
        targetProcessId_ = processId;
        client.FlushFrames(processId);
    }
    std::vector<std::uint8_t> blob(
        static_cast<std::size_t>(blobSize_) * kDebugFrameConsumeBatch);
    std::uint32_t count = kDebugFrameConsumeBatch;
    if (client.ConsumeFrames(query_, processId, blob.data(), &count) !=
        PM_STATUS_SUCCESS || count == 0)
        return std::nullopt;
    return FoldPresentMonDebugFrames(plan_, blob, count, blobSize_);
}
}
