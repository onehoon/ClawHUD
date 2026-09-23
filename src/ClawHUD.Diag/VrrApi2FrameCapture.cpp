#include "VrrApi2FrameCapture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
constexpr std::uint32_t kEtwFlushPeriodMs = 8;
constexpr std::uint32_t kConsumeBatchCapacity = 256;

struct MetricSpec
{
    PM_METRIC metric;
    VrrFrameField field;
    bool required;
};

constexpr std::array kMetricSpecs{
    MetricSpec{ PM_METRIC_BETWEEN_DISPLAY_CHANGE, VrrFrameField::BetweenDisplayChange, true },
    MetricSpec{ PM_METRIC_SWAP_CHAIN_ADDRESS, VrrFrameField::SwapChainAddress, true },
    MetricSpec{ PM_METRIC_PRESENT_MODE, VrrFrameField::PresentMode, true },
    MetricSpec{ PM_METRIC_PROCESS_ID, VrrFrameField::ProcessId, false },
    MetricSpec{ PM_METRIC_DROPPED_FRAMES, VrrFrameField::Dropped, false },
    MetricSpec{ PM_METRIC_DISPLAYED_TIME, VrrFrameField::DisplayedTime, false },
    MetricSpec{ PM_METRIC_ALLOWS_TEARING, VrrFrameField::AllowsTearing, false },
    MetricSpec{ PM_METRIC_FRAME_TYPE, VrrFrameField::FrameType, false },
    MetricSpec{ PM_METRIC_PRESENT_RUNTIME, VrrFrameField::PresentRuntime, false },
    MetricSpec{ PM_METRIC_PRESENT_START_QPC, VrrFrameField::PresentStartQpc, false },
    MetricSpec{ PM_METRIC_BETWEEN_PRESENTS, VrrFrameField::BetweenPresents, false },
    MetricSpec{ PM_METRIC_UNTIL_DISPLAYED, VrrFrameField::UntilDisplayed, false },
    MetricSpec{ PM_METRIC_DISPLAY_LATENCY, VrrFrameField::DisplayLatency, false },
    MetricSpec{ PM_METRIC_RENDER_PRESENT_LATENCY, VrrFrameField::RenderPresentLatency, false },
    MetricSpec{ PM_METRIC_SYNC_INTERVAL, VrrFrameField::SyncInterval, false },
    MetricSpec{ PM_METRIC_PRESENT_FLAGS, VrrFrameField::PresentFlags, false },
    MetricSpec{ PM_METRIC_FLIP_DELAY, VrrFrameField::FlipDelay, false },
};

bool HasValidArray(const PM_INTROSPECTION_OBJARRAY* array) noexcept
{
    return array && (array->size == 0 || array->pData);
}

bool IsIndependentDevice(const PM_INTROSPECTION_ROOT* root, std::uint32_t id) noexcept
{
    if (!root || !HasValidArray(root->pDevices)) return false;
    for (std::size_t i = 0; i < root->pDevices->size; ++i)
    {
        const auto* device = static_cast<const PM_INTROSPECTION_DEVICE*>(root->pDevices->pData[i]);
        if (device && device->id == id && device->type == PM_DEVICE_TYPE_INDEPENDENT)
            return true;
    }
    return false;
}

std::optional<std::uint32_t> FindAvailableIndependentDevice(
    const PM_INTROSPECTION_ROOT* root, const PM_INTROSPECTION_METRIC* metric) noexcept
{
    if (!metric || !HasValidArray(metric->pDeviceMetricInfo)) return std::nullopt;
    for (std::size_t i = 0; i < metric->pDeviceMetricInfo->size; ++i)
    {
        const auto* info = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(
            metric->pDeviceMetricInfo->pData[i]);
        if (info && info->availability == PM_METRIC_AVAILABILITY_AVAILABLE &&
            IsIndependentDevice(root, info->deviceId))
            return info->deviceId;
    }
    return std::nullopt;
}

bool HasExpectedType(VrrFrameField field, PM_DATA_TYPE type, PM_ENUM enumId) noexcept
{
    switch (field)
    {
    case VrrFrameField::ProcessId:
        return type == PM_DATA_TYPE_UINT32 || type == PM_DATA_TYPE_UINT64;
    case VrrFrameField::SwapChainAddress:
    case VrrFrameField::PresentStartQpc:
        return type == PM_DATA_TYPE_UINT64;
    case VrrFrameField::PresentMode:
        return (type == PM_DATA_TYPE_ENUM || type == PM_DATA_TYPE_INT32) &&
            enumId == PM_ENUM_PRESENT_MODE;
    case VrrFrameField::PresentRuntime:
        return (type == PM_DATA_TYPE_ENUM || type == PM_DATA_TYPE_INT32) &&
            enumId == PM_ENUM_GRAPHICS_RUNTIME;
    case VrrFrameField::FrameType:
        return (type == PM_DATA_TYPE_ENUM || type == PM_DATA_TYPE_INT32) &&
            enumId == PM_ENUM_FRAME_TYPE;
    case VrrFrameField::AllowsTearing:
    case VrrFrameField::Dropped:
        return type == PM_DATA_TYPE_BOOL;
    case VrrFrameField::SyncInterval:
        return type == PM_DATA_TYPE_INT32 || type == PM_DATA_TYPE_UINT32;
    case VrrFrameField::PresentFlags:
        return type == PM_DATA_TYPE_UINT32 || type == PM_DATA_TYPE_INT32;
    case VrrFrameField::BetweenPresents:
    case VrrFrameField::BetweenDisplayChange:
    case VrrFrameField::DisplayedTime:
    case VrrFrameField::UntilDisplayed:
    case VrrFrameField::DisplayLatency:
    case VrrFrameField::RenderPresentLatency:
    case VrrFrameField::FlipDelay:
        return type == PM_DATA_TYPE_DOUBLE;
    }
    return false;
}

const PM_INTROSPECTION_METRIC* FindMetric(
    const PM_INTROSPECTION_ROOT* root, PM_METRIC id) noexcept
{
    if (!root || !HasValidArray(root->pMetrics)) return nullptr;
    for (std::size_t i = 0; i < root->pMetrics->size; ++i)
    {
        const auto* metric = static_cast<const PM_INTROSPECTION_METRIC*>(root->pMetrics->pData[i]);
        if (metric && metric->id == id) return metric;
    }
    return nullptr;
}

std::optional<VrrFrameMetricBinding> MakeBinding(
    const PM_INTROSPECTION_ROOT* root, const MetricSpec& spec)
{
    const auto* metric = FindMetric(root, spec.metric);
    if (!metric || (metric->type != PM_METRIC_TYPE_FRAME_EVENT &&
        metric->type != PM_METRIC_TYPE_DYNAMIC_FRAME) || !metric->pTypeInfo)
        return std::nullopt;

    const auto deviceId = FindAvailableIndependentDevice(root, metric);
    if (!deviceId || !HasExpectedType(spec.field, metric->pTypeInfo->frameType,
            metric->pTypeInfo->enumId))
        return std::nullopt;

    VrrFrameMetricBinding binding;
    binding.field = spec.field;
    binding.dataType = metric->pTypeInfo->frameType;
    binding.enumId = metric->pTypeInfo->enumId;
    binding.required = spec.required;
    binding.element = PM_QUERY_ELEMENT{ spec.metric, PM_STAT_NONE, *deviceId, 0, 0, 0 };
    return binding;
}

const VrrFrameMetricBinding* FindBinding(
    const VrrFrameQueryPlan& plan, VrrFrameField field) noexcept
{
    const auto found = std::find_if(plan.bindings.begin(), plan.bindings.end(),
        [field](const auto& binding) { return binding.field == field; });
    return found == plan.bindings.end() ? nullptr : &*found;
}

std::span<const std::uint8_t> FieldBytes(
    std::span<const std::uint8_t> record, const VrrFrameMetricBinding& binding,
    std::size_t expectedSize) noexcept
{
    const auto offset = binding.element.dataOffset;
    const auto size = binding.element.dataSize;
    if (size != expectedSize || offset > record.size() ||
        size > record.size() - static_cast<std::size_t>(offset))
        return {};
    return record.subspan(static_cast<std::size_t>(offset), expectedSize);
}

template<class T>
std::optional<T> ReadScalar(std::span<const std::uint8_t> record,
    const VrrFrameMetricBinding& binding, PM_DATA_TYPE type) noexcept
{
    if (binding.dataType != type) return std::nullopt;
    const auto bytes = FieldBytes(record, binding, sizeof(T));
    if (bytes.empty()) return std::nullopt;
    T value{};
    std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}

std::optional<double> ReadDouble(std::span<const std::uint8_t> record,
    const VrrFrameMetricBinding& binding) noexcept
{
    auto value = ReadScalar<double>(record, binding, PM_DATA_TYPE_DOUBLE);
    if (!value || !std::isfinite(*value)) return std::nullopt;
    return value;
}

std::optional<std::uint64_t> ReadUnsigned(std::span<const std::uint8_t> record,
    const VrrFrameMetricBinding& binding) noexcept
{
    if (binding.dataType == PM_DATA_TYPE_UINT32)
    {
        const auto value = ReadScalar<std::uint32_t>(record, binding, PM_DATA_TYPE_UINT32);
        if (value) return *value;
    }
    else if (binding.dataType == PM_DATA_TYPE_UINT64)
    {
        return ReadScalar<std::uint64_t>(record, binding, PM_DATA_TYPE_UINT64);
    }
    return std::nullopt;
}

std::optional<std::int32_t> ReadInteger(std::span<const std::uint8_t> record,
    const VrrFrameMetricBinding& binding) noexcept
{
    if (binding.dataType == PM_DATA_TYPE_INT32 || binding.dataType == PM_DATA_TYPE_ENUM)
        return ReadScalar<std::int32_t>(record, binding, binding.dataType);
    if (binding.dataType == PM_DATA_TYPE_UINT32)
    {
        const auto value = ReadScalar<std::uint32_t>(record, binding, PM_DATA_TYPE_UINT32);
        if (value && *value <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
            return static_cast<std::int32_t>(*value);
    }
    return std::nullopt;
}

std::optional<bool> ReadBool(std::span<const std::uint8_t> record,
    const VrrFrameMetricBinding& binding) noexcept
{
    if (binding.dataType != PM_DATA_TYPE_BOOL) return std::nullopt;
    const auto bytes = FieldBytes(record, binding, sizeof(std::uint8_t));
    if (bytes.empty() || bytes.front() > 1) return std::nullopt;
    return bytes.front() != 0;
}

std::optional<std::uint64_t> ExpectedFieldSize(PM_DATA_TYPE type) noexcept
{
    switch (type)
    {
    case PM_DATA_TYPE_DOUBLE: return sizeof(double);
    case PM_DATA_TYPE_INT32:
    case PM_DATA_TYPE_UINT32:
    case PM_DATA_TYPE_ENUM: return sizeof(std::uint32_t);
    case PM_DATA_TYPE_UINT64: return sizeof(std::uint64_t);
    case PM_DATA_TYPE_BOOL: return sizeof(std::uint8_t);
    default: return std::nullopt;
    }
}

class IntrospectionRootGuard
{
public:
    IntrospectionRootGuard(DiagPresentMonApi2Client& client,
        const PM_INTROSPECTION_ROOT* root) noexcept : client_(client), root_(root) {}
    ~IntrospectionRootGuard()
    {
        if (root_) client_.FreeIntrospectionRoot(root_);
    }

    IntrospectionRootGuard(const IntrospectionRootGuard&) = delete;
    IntrospectionRootGuard& operator=(const IntrospectionRootGuard&) = delete;

private:
    DiagPresentMonApi2Client& client_;
    const PM_INTROSPECTION_ROOT* root_{};
};
}

bool VrrPresentMonApiVersionMatches(const PM_VERSION& version) noexcept
{
    return version.major == PM_API_VERSION_MAJOR && version.minor == PM_API_VERSION_MINOR;
}

std::optional<VrrFrameQueryPlan> BuildVrrFrameQueryPlan(
    const PM_INTROSPECTION_ROOT* root)
{
    if (!root || !HasValidArray(root->pMetrics) || !HasValidArray(root->pDevices))
        return std::nullopt;

    VrrFrameQueryPlan plan;
    plan.bindings.reserve(kMetricSpecs.size());
    for (const auto& spec : kMetricSpecs)
    {
        auto binding = MakeBinding(root, spec);
        if (!binding)
        {
            if (spec.required) return std::nullopt;
            continue;
        }
        plan.bindings.push_back(*binding);
    }
    return plan;
}

std::optional<VrrFrameSample> DecodeVrrFrameSample(
    std::span<const std::uint8_t> record, std::uint32_t targetProcessId,
    const VrrFrameQueryPlan& plan)
{
    if (targetProcessId == 0 || record.empty()) return std::nullopt;

    const auto* address = FindBinding(plan, VrrFrameField::SwapChainAddress);
    const auto* presentMode = FindBinding(plan, VrrFrameField::PresentMode);
    const auto* displayChange = FindBinding(plan, VrrFrameField::BetweenDisplayChange);
    if (!address || !address->required || !presentMode || !presentMode->required ||
        !displayChange || !displayChange->required)
        return std::nullopt;

    const auto addressValue = ReadUnsigned(record, *address);
    const auto modeValue = ReadInteger(record, *presentMode);
    if (FieldBytes(record, *displayChange, sizeof(double)).empty()) return std::nullopt;
    const auto displayChangeValue = ReadDouble(record, *displayChange);
    if (!addressValue || *addressValue == 0 || !modeValue)
        return std::nullopt;

    VrrFrameSample sample;
    sample.processId = targetProcessId;
    sample.swapChainAddress = *addressValue;
    sample.presentMode = *modeValue;
    sample.betweenDisplayChangeMs = displayChangeValue;

    if (const auto* binding = FindBinding(plan, VrrFrameField::ProcessId))
    {
        const auto processId = ReadUnsigned(record, *binding);
        if (processId && *processId != targetProcessId) return std::nullopt;
    }
    if (const auto* binding = FindBinding(plan, VrrFrameField::PresentStartQpc))
        sample.presentStartQpc = ReadUnsigned(record, *binding);
    if (const auto* binding = FindBinding(plan, VrrFrameField::PresentRuntime))
        sample.presentRuntime = ReadInteger(record, *binding);
    if (const auto* binding = FindBinding(plan, VrrFrameField::FrameType))
        sample.frameType = ReadInteger(record, *binding);
    if (const auto* binding = FindBinding(plan, VrrFrameField::AllowsTearing))
        sample.allowsTearing = ReadBool(record, *binding);
    if (const auto* binding = FindBinding(plan, VrrFrameField::Dropped))
        sample.dropped = ReadBool(record, *binding);
    if (const auto* binding = FindBinding(plan, VrrFrameField::SyncInterval))
        sample.syncInterval = ReadInteger(record, *binding);
    if (const auto* binding = FindBinding(plan, VrrFrameField::PresentFlags))
    {
        if (binding->dataType == PM_DATA_TYPE_INT32)
        {
            const auto value = ReadScalar<std::int32_t>(record, *binding, PM_DATA_TYPE_INT32);
            if (value) sample.presentFlags = static_cast<std::uint32_t>(*value);
        }
        else if (const auto value = ReadUnsigned(record, *binding);
            value && *value <= std::numeric_limits<std::uint32_t>::max())
        {
            sample.presentFlags = static_cast<std::uint32_t>(*value);
        }
    }
    if (const auto* binding = FindBinding(plan, VrrFrameField::BetweenPresents))
        sample.betweenPresentsMs = ReadDouble(record, *binding);
    if (const auto* binding = FindBinding(plan, VrrFrameField::DisplayedTime))
        sample.displayedTimeMs = ReadDouble(record, *binding);
    if (const auto* binding = FindBinding(plan, VrrFrameField::UntilDisplayed))
        sample.untilDisplayedMs = ReadDouble(record, *binding);
    if (const auto* binding = FindBinding(plan, VrrFrameField::DisplayLatency))
        sample.displayLatencyMs = ReadDouble(record, *binding);
    if (const auto* binding = FindBinding(plan, VrrFrameField::RenderPresentLatency))
        sample.renderPresentLatencyMs = ReadDouble(record, *binding);
    if (const auto* binding = FindBinding(plan, VrrFrameField::FlipDelay))
        sample.flipDelayMs = ReadDouble(record, *binding);

    return sample;
}

VrrApi2FrameCapture::~VrrApi2FrameCapture() { Shutdown(); }

bool VrrApi2FrameCapture::Initialize()
{
    Shutdown();
    try
    {
        if (!client_.Initialize()) return false;
        clientInitialized_ = true;
        if (!VrrPresentMonApiVersionMatches(client_.ApiVersion()) ||
            !client_.FrameQueryEndpointsAvailable() ||
            client_.OpenSession() != PM_STATUS_SUCCESS)
        {
            Shutdown();
            return false;
        }
        if (client_.SetEtwFlushPeriod(kEtwFlushPeriodMs) != PM_STATUS_SUCCESS)
        {
            Shutdown();
            return false;
        }

        const PM_INTROSPECTION_ROOT* root{};
        if (client_.GetIntrospectionRoot(&root) != PM_STATUS_SUCCESS || !root)
        {
            if (root) client_.FreeIntrospectionRoot(root);
            Shutdown();
            return false;
        }

        std::optional<VrrFrameQueryPlan> plan;
        {
            IntrospectionRootGuard rootGuard(client_, root);
            plan = BuildVrrFrameQueryPlan(root);
        }
        if (!plan)
        {
            Shutdown();
            return false;
        }
        plan_ = std::move(*plan);

        std::vector<PM_QUERY_ELEMENT> elements;
        elements.reserve(plan_.bindings.size());
        for (const auto& binding : plan_.bindings) elements.push_back(binding.element);

        PM_FRAME_QUERY_HANDLE query{};
        std::uint32_t blobSize{};
        const auto status = client_.RegisterFrameQuery(&query, elements.data(),
            elements.size(), &blobSize);
        if (status != PM_STATUS_SUCCESS || !query || blobSize == 0 ||
            blobSize > std::numeric_limits<std::size_t>::max() / kConsumeBatchCapacity)
        {
            if (status == PM_STATUS_SUCCESS && query) client_.FreeFrameQuery(query);
            Shutdown();
            return false;
        }

        query_ = query;
        blobSize_ = blobSize;
        for (std::size_t i = 0; i < plan_.bindings.size(); ++i)
            plan_.bindings[i].element = elements[i];
        for (const auto& binding : plan_.bindings)
        {
            if (!binding.required) continue;
            const auto expected = ExpectedFieldSize(binding.dataType);
            if (!expected || binding.element.dataSize != *expected ||
                binding.element.dataOffset > blobSize_ ||
                binding.element.dataSize > blobSize_ - binding.element.dataOffset)
            {
                Shutdown();
                return false;
            }
        }

        ready_ = true;
        return true;
    }
    catch (...)
    {
        Shutdown();
        return false;
    }
}

bool VrrApi2FrameCapture::FlushFrames(std::uint32_t processId) noexcept
{
    return ready_ && processId != 0 && trackedProcessId_ == 0 &&
        client_.FlushFrames(processId) == PM_STATUS_SUCCESS;
}

bool VrrApi2FrameCapture::StartTracking(std::uint32_t processId, bool flushBeforeStart)
{
    if (!ready_ || processId == 0 || trackedProcessId_ != 0 ||
        client_.StartTrackingProcess(processId) != PM_STATUS_SUCCESS)
        return false;

    trackedProcessId_ = processId;
    if (flushBeforeStart && client_.FlushFrames(processId) != PM_STATUS_SUCCESS)
    {
        StopTracking();
        return false;
    }
    samples_.clear();
    return true;
}

bool VrrApi2FrameCapture::DrainFrames()
{
    if (!ready_ || trackedProcessId_ == 0 || blobSize_ == 0 ||
        blobSize_ > std::numeric_limits<std::size_t>::max() / kConsumeBatchCapacity)
        return false;

    std::vector<std::uint8_t> buffer(
        static_cast<std::size_t>(blobSize_) * kConsumeBatchCapacity);
    for (;;)
    {
        std::uint32_t frameCount = kConsumeBatchCapacity;
        if (client_.ConsumeFrames(query_, trackedProcessId_, buffer.data(),
                &frameCount) != PM_STATUS_SUCCESS || frameCount > kConsumeBatchCapacity)
            return false;

        for (std::uint32_t i = 0; i < frameCount; ++i)
        {
            const auto offset = static_cast<std::size_t>(i) * blobSize_;
            const auto record = std::span<const std::uint8_t>(buffer).subspan(offset, blobSize_);
            auto sample = DecodeVrrFrameSample(record, trackedProcessId_, plan_);
            if (sample) samples_.push_back(std::move(*sample));
        }
        if (frameCount < kConsumeBatchCapacity) return true;
    }
}

void VrrApi2FrameCapture::StopTracking() noexcept
{
    if (!trackedProcessId_) return;
    client_.StopTrackingProcess(trackedProcessId_);
    trackedProcessId_ = 0;
}

void VrrApi2FrameCapture::Shutdown() noexcept
{
    ready_ = false;
    if (query_)
    {
        client_.FreeFrameQuery(query_);
        query_ = nullptr;
    }
    StopTracking();
    blobSize_ = 0;
    plan_ = {};
    samples_.clear();
    if (clientInitialized_) client_.Shutdown();
    clientInitialized_ = false;
}
