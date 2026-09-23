#ifdef NDEBUG
#undef NDEBUG
#endif

#include "VrrApi2FrameCapture.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

namespace
{
class IntrospectionFixture
{
    struct OwnedMetric
    {
        PM_INTROSPECTION_DATA_TYPE_INFO typeInfo{};
        PM_INTROSPECTION_METRIC metric{};
        std::vector<PM_INTROSPECTION_DEVICE_METRIC_INFO> deviceInfo;
        std::vector<const void*> devicePointers;
        PM_INTROSPECTION_OBJARRAY deviceArray{};
    };

public:
    void AddDevice(std::uint32_t id, PM_DEVICE_TYPE type)
    {
        devices_.push_back(PM_INTROSPECTION_DEVICE{ id, type,
            PM_DEVICE_VENDOR_UNKNOWN, nullptr, nullptr });
    }

    void AddMetric(PM_METRIC id, PM_METRIC_TYPE metricType, PM_DATA_TYPE frameType,
        PM_ENUM enumId = PM_ENUM_NULL_ENUM,
        std::vector<PM_INTROSPECTION_DEVICE_METRIC_INFO> deviceInfo = {})
    {
        metrics_.emplace_back();
        auto& owned = metrics_.back();
        owned.typeInfo.frameType = frameType;
        owned.typeInfo.enumId = enumId;
        owned.deviceInfo = std::move(deviceInfo);
        for (const auto& info : owned.deviceInfo)
            owned.devicePointers.push_back(&info);
        owned.deviceArray = PM_INTROSPECTION_OBJARRAY{
            owned.devicePointers.data(), owned.devicePointers.size() };
        owned.metric.id = id;
        owned.metric.type = metricType;
        owned.metric.pTypeInfo = &owned.typeInfo;
        owned.metric.pDeviceMetricInfo = &owned.deviceArray;
    }

    const PM_INTROSPECTION_ROOT* Root()
    {
        metricPointers_.clear();
        for (auto& owned : metrics_) metricPointers_.push_back(&owned.metric);
        devicePointers_.clear();
        for (auto& device : devices_) devicePointers_.push_back(&device);
        metricArray_ = PM_INTROSPECTION_OBJARRAY{ metricPointers_.data(), metricPointers_.size() };
        deviceArray_ = PM_INTROSPECTION_OBJARRAY{ devicePointers_.data(), devicePointers_.size() };
        root_.pMetrics = &metricArray_;
        root_.pDevices = &deviceArray_;
        return &root_;
    }

private:
    std::deque<OwnedMetric> metrics_;
    std::vector<PM_INTROSPECTION_DEVICE> devices_;
    std::vector<const void*> metricPointers_;
    std::vector<const void*> devicePointers_;
    PM_INTROSPECTION_OBJARRAY metricArray_{};
    PM_INTROSPECTION_OBJARRAY deviceArray_{};
    PM_INTROSPECTION_ROOT root_{};
};

PM_INTROSPECTION_DEVICE_METRIC_INFO DeviceMetric(
    std::uint32_t id, PM_METRIC_AVAILABILITY availability)
{
    return PM_INTROSPECTION_DEVICE_METRIC_INFO{ id, availability, 1 };
}

void AddRequiredMetrics(IntrospectionFixture& fixture)
{
    const std::vector<PM_INTROSPECTION_DEVICE_METRIC_INFO> devices{
        DeviceMetric(0, PM_METRIC_AVAILABILITY_AVAILABLE),
        DeviceMetric(19, PM_METRIC_AVAILABILITY_AVAILABLE) };
    fixture.AddMetric(PM_METRIC_BETWEEN_DISPLAY_CHANGE, PM_METRIC_TYPE_FRAME_EVENT,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM, devices);
    fixture.AddMetric(PM_METRIC_SWAP_CHAIN_ADDRESS, PM_METRIC_TYPE_FRAME_EVENT,
        PM_DATA_TYPE_UINT64, PM_ENUM_NULL_ENUM, devices);
    fixture.AddMetric(PM_METRIC_PRESENT_MODE, PM_METRIC_TYPE_FRAME_EVENT,
        PM_DATA_TYPE_ENUM, PM_ENUM_PRESENT_MODE, devices);
}

struct BuiltRecord
{
    VrrFrameQueryPlan plan;
    std::vector<std::uint8_t> bytes;
};

template<class T>
void AddField(BuiltRecord& record, VrrFrameField field, PM_METRIC metric,
    PM_DATA_TYPE type, PM_ENUM enumId, const T& value)
{
    const auto offset = record.bytes.size();
    record.bytes.resize(offset + sizeof(value));
    std::memcpy(record.bytes.data() + offset, &value, sizeof(value));
    VrrFrameMetricBinding binding;
    binding.field = field;
    binding.dataType = type;
    binding.enumId = enumId;
    binding.required = field == VrrFrameField::SwapChainAddress ||
        field == VrrFrameField::PresentMode || field == VrrFrameField::BetweenDisplayChange;
    binding.element = PM_QUERY_ELEMENT{ metric, PM_STAT_NONE, 19, 0, offset, sizeof(value) };
    record.plan.bindings.push_back(binding);
}

BuiltRecord MakeCompleteRecord(std::uint32_t processId = 4321)
{
    BuiltRecord record;
    AddField(record, VrrFrameField::ProcessId, PM_METRIC_PROCESS_ID,
        PM_DATA_TYPE_UINT32, PM_ENUM_NULL_ENUM, processId);
    const std::uint64_t address = 0x123456789abcdef0ull;
    AddField(record, VrrFrameField::SwapChainAddress, PM_METRIC_SWAP_CHAIN_ADDRESS,
        PM_DATA_TYPE_UINT64, PM_ENUM_NULL_ENUM, address);
    const std::uint64_t qpc = 987654321ull;
    AddField(record, VrrFrameField::PresentStartQpc, PM_METRIC_PRESENT_START_QPC,
        PM_DATA_TYPE_UINT64, PM_ENUM_NULL_ENUM, qpc);
    const std::int32_t mode = PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP;
    AddField(record, VrrFrameField::PresentMode, PM_METRIC_PRESENT_MODE,
        PM_DATA_TYPE_ENUM, PM_ENUM_PRESENT_MODE, mode);
    const std::int32_t runtime = PM_GRAPHICS_RUNTIME_DXGI;
    AddField(record, VrrFrameField::PresentRuntime, PM_METRIC_PRESENT_RUNTIME,
        PM_DATA_TYPE_ENUM, PM_ENUM_GRAPHICS_RUNTIME, runtime);
    const std::int32_t frameType = PM_FRAME_TYPE_INTEL_XEFG;
    AddField(record, VrrFrameField::FrameType, PM_METRIC_FRAME_TYPE,
        PM_DATA_TYPE_ENUM, PM_ENUM_FRAME_TYPE, frameType);
    const std::uint8_t tearing = 1;
    AddField(record, VrrFrameField::AllowsTearing, PM_METRIC_ALLOWS_TEARING,
        PM_DATA_TYPE_BOOL, PM_ENUM_NULL_ENUM, tearing);
    const std::uint8_t dropped = 0;
    AddField(record, VrrFrameField::Dropped, PM_METRIC_DROPPED_FRAMES,
        PM_DATA_TYPE_BOOL, PM_ENUM_NULL_ENUM, dropped);
    const std::int32_t syncInterval = 1;
    AddField(record, VrrFrameField::SyncInterval, PM_METRIC_SYNC_INTERVAL,
        PM_DATA_TYPE_INT32, PM_ENUM_NULL_ENUM, syncInterval);
    const std::uint32_t presentFlags = 5;
    AddField(record, VrrFrameField::PresentFlags, PM_METRIC_PRESENT_FLAGS,
        PM_DATA_TYPE_UINT32, PM_ENUM_NULL_ENUM, presentFlags);

    const double betweenPresents = 16.67;
    AddField(record, VrrFrameField::BetweenPresents, PM_METRIC_BETWEEN_PRESENTS,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM, betweenPresents);
    const double betweenDisplayChange = 16.68;
    AddField(record, VrrFrameField::BetweenDisplayChange, PM_METRIC_BETWEEN_DISPLAY_CHANGE,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM, betweenDisplayChange);
    const double displayedTime = 16.65;
    AddField(record, VrrFrameField::DisplayedTime, PM_METRIC_DISPLAYED_TIME,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM, displayedTime);
    const double untilDisplayed = 0.7;
    AddField(record, VrrFrameField::UntilDisplayed, PM_METRIC_UNTIL_DISPLAYED,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM, untilDisplayed);
    const double displayLatency = 2.1;
    AddField(record, VrrFrameField::DisplayLatency, PM_METRIC_DISPLAY_LATENCY,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM, displayLatency);
    const double renderLatency = 1.2;
    AddField(record, VrrFrameField::RenderPresentLatency, PM_METRIC_RENDER_PRESENT_LATENCY,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM, renderLatency);
    const double flipDelay = 0.2;
    AddField(record, VrrFrameField::FlipDelay, PM_METRIC_FLIP_DELAY,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM, flipDelay);
    return record;
}

const VrrFrameMetricBinding& Binding(const VrrFrameQueryPlan& plan, VrrFrameField field)
{
    for (const auto& binding : plan.bindings)
        if (binding.field == field) return binding;
    std::abort();
}

void TestVersionGate()
{
    PM_VERSION version{};
    version.major = 3;
    version.minor = 4;
    assert(VrrPresentMonApiVersionMatches(version));
    version.patch = 99;
    assert(VrrPresentMonApiVersionMatches(version));
    version.minor = 5;
    assert(!VrrPresentMonApiVersionMatches(version));
    version.major = 4;
    version.minor = 4;
    assert(!VrrPresentMonApiVersionMatches(version));
}

void TestQueryPlanRequiresCoreAndSelectsIndependentDevice()
{
    IntrospectionFixture fixture;
    fixture.AddDevice(0, PM_DEVICE_TYPE_GRAPHICS_ADAPTER);
    fixture.AddDevice(19, PM_DEVICE_TYPE_INDEPENDENT);
    AddRequiredMetrics(fixture);

    const auto plan = BuildVrrFrameQueryPlan(fixture.Root());
    assert(plan && plan->bindings.size() == 3);
    for (const auto& binding : plan->bindings)
    {
        assert(binding.required);
        assert(binding.element.deviceId == 19);
        assert(binding.element.stat == PM_STAT_NONE);
    }

    IntrospectionFixture missing;
    missing.AddDevice(19, PM_DEVICE_TYPE_INDEPENDENT);
    missing.AddMetric(PM_METRIC_BETWEEN_DISPLAY_CHANGE, PM_METRIC_TYPE_FRAME_EVENT,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM,
        { DeviceMetric(19, PM_METRIC_AVAILABILITY_AVAILABLE) });
    missing.AddMetric(PM_METRIC_SWAP_CHAIN_ADDRESS, PM_METRIC_TYPE_FRAME_EVENT,
        PM_DATA_TYPE_UINT64, PM_ENUM_NULL_ENUM,
        { DeviceMetric(19, PM_METRIC_AVAILABILITY_AVAILABLE) });
    assert(!BuildVrrFrameQueryPlan(missing.Root()));
}

void TestOptionalMetricsAndDynamicOnlyExclusion()
{
    IntrospectionFixture fixture;
    fixture.AddDevice(19, PM_DEVICE_TYPE_INDEPENDENT);
    AddRequiredMetrics(fixture);
    fixture.AddMetric(PM_METRIC_PROCESS_ID, PM_METRIC_TYPE_FRAME_EVENT,
        PM_DATA_TYPE_UINT32, PM_ENUM_NULL_ENUM,
        { DeviceMetric(19, PM_METRIC_AVAILABILITY_AVAILABLE) });
    fixture.AddMetric(PM_METRIC_DROPPED_FRAMES, PM_METRIC_TYPE_FRAME_EVENT,
        PM_DATA_TYPE_BOOL, PM_ENUM_NULL_ENUM,
        { DeviceMetric(19, PM_METRIC_AVAILABILITY_NOT_SUPPORTED_BY_DEVICE) });
    fixture.AddMetric(PM_METRIC_BETWEEN_PRESENTS, PM_METRIC_TYPE_DYNAMIC_FRAME,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM,
        { DeviceMetric(19, PM_METRIC_AVAILABILITY_AVAILABLE) });
    fixture.AddMetric(PM_METRIC_DISPLAYED_FRAME_TIME, PM_METRIC_TYPE_DYNAMIC,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM,
        { DeviceMetric(19, PM_METRIC_AVAILABILITY_AVAILABLE) });

    const auto plan = BuildVrrFrameQueryPlan(fixture.Root());
    assert(plan);
    bool sawProcessId = false;
    bool sawDropped = false;
    bool sawBetweenPresents = false;
    bool sawDynamicOnly = false;
    for (const auto& binding : plan->bindings)
    {
        sawProcessId |= binding.field == VrrFrameField::ProcessId;
        sawDropped |= binding.field == VrrFrameField::Dropped;
        sawBetweenPresents |= binding.field == VrrFrameField::BetweenPresents;
        sawDynamicOnly |= binding.element.metric == PM_METRIC_DISPLAYED_FRAME_TIME;
    }
    assert(sawProcessId);
    assert(!sawDropped);
    assert(sawBetweenPresents);
    assert(!sawDynamicOnly);
}

void TestTypedFrameDecode()
{
    auto record = MakeCompleteRecord();
    const auto sample = DecodeVrrFrameSample(record.bytes, 4321, record.plan);
    assert(sample);
    assert(sample->processId == 4321);
    assert(sample->swapChainAddress == 0x123456789abcdef0ull);
    assert(sample->presentStartQpc == 987654321ull);
    assert(sample->presentMode == PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    assert(sample->presentRuntime == PM_GRAPHICS_RUNTIME_DXGI);
    assert(sample->frameType == PM_FRAME_TYPE_INTEL_XEFG);
    assert(sample->allowsTearing == true);
    assert(sample->dropped == false);
    assert(sample->syncInterval == 1);
    assert(sample->presentFlags == 5);
    assert(sample->betweenPresentsMs == 16.67);
    assert(sample->betweenDisplayChangeMs == 16.68);
    assert(sample->displayedTimeMs == 16.65);
    assert(sample->untilDisplayedMs == 0.7);
    assert(sample->displayLatencyMs == 2.1);
    assert(sample->renderPresentLatencyMs == 1.2);
    assert(sample->flipDelayMs == 0.2);

    auto signedFlags = MakeCompleteRecord();
    auto& signedFlagsBinding = const_cast<VrrFrameMetricBinding&>(
        Binding(signedFlags.plan, VrrFrameField::PresentFlags));
    signedFlagsBinding.dataType = PM_DATA_TYPE_INT32;
    const std::int32_t allFlagsSet = -1;
    std::memcpy(signedFlags.bytes.data() + signedFlagsBinding.element.dataOffset,
        &allFlagsSet, sizeof(allFlagsSet));
    const auto signedFlagsSample = DecodeVrrFrameSample(
        signedFlags.bytes, 4321, signedFlags.plan);
    assert(signedFlagsSample);
    assert(signedFlagsSample->presentFlags == std::numeric_limits<std::uint32_t>::max());
}

void TestInvalidRequiredAndOptionalFields()
{
    auto record = MakeCompleteRecord();
    auto zeroAddress = record;
    const std::uint64_t zero = 0;
    const auto& addressBinding = Binding(zeroAddress.plan, VrrFrameField::SwapChainAddress);
    std::memcpy(zeroAddress.bytes.data() + addressBinding.element.dataOffset,
        &zero, sizeof(zero));
    assert(!DecodeVrrFrameSample(zeroAddress.bytes, 4321, zeroAddress.plan));

    auto badOffset = record;
    auto& required = const_cast<VrrFrameMetricBinding&>(
        Binding(badOffset.plan, VrrFrameField::BetweenDisplayChange));
    required.element.dataOffset = badOffset.bytes.size() + 1;
    assert(!DecodeVrrFrameSample(badOffset.bytes, 4321, badOffset.plan));

    auto badSize = record;
    auto& requiredSize = const_cast<VrrFrameMetricBinding&>(
        Binding(badSize.plan, VrrFrameField::BetweenDisplayChange));
    requiredSize.element.dataSize = sizeof(float);
    assert(!DecodeVrrFrameSample(badSize.bytes, 4321, badSize.plan));

    auto optionalOffset = record;
    auto& optional = const_cast<VrrFrameMetricBinding&>(
        Binding(optionalOffset.plan, VrrFrameField::DisplayLatency));
    optional.element.dataOffset = optionalOffset.bytes.size() + 1;
    const auto sample = DecodeVrrFrameSample(optionalOffset.bytes, 4321, optionalOffset.plan);
    assert(sample);
    assert(!sample->displayLatencyMs);

    auto nonFinite = record;
    const auto& latency = Binding(nonFinite.plan, VrrFrameField::DisplayLatency);
    const double infinity = std::numeric_limits<double>::infinity();
    std::memcpy(nonFinite.bytes.data() + latency.element.dataOffset, &infinity, sizeof(infinity));
    const auto finiteRequired = DecodeVrrFrameSample(nonFinite.bytes, 4321, nonFinite.plan);
    assert(finiteRequired);
    assert(!finiteRequired->displayLatencyMs);

    const auto& displayChange = Binding(nonFinite.plan, VrrFrameField::BetweenDisplayChange);
    std::memcpy(nonFinite.bytes.data() + displayChange.element.dataOffset, &infinity, sizeof(infinity));
    assert(!DecodeVrrFrameSample(nonFinite.bytes, 4321, nonFinite.plan));
}

void TestPidMismatchAndMissingOptionalField()
{
    auto mismatch = MakeCompleteRecord(4322);
    assert(!DecodeVrrFrameSample(mismatch.bytes, 4321, mismatch.plan));

    BuiltRecord minimal;
    const std::uint64_t address = 11;
    AddField(minimal, VrrFrameField::SwapChainAddress, PM_METRIC_SWAP_CHAIN_ADDRESS,
        PM_DATA_TYPE_UINT64, PM_ENUM_NULL_ENUM, address);
    const std::int32_t mode = PM_PRESENT_MODE_COMPOSED_FLIP;
    AddField(minimal, VrrFrameField::PresentMode, PM_METRIC_PRESENT_MODE,
        PM_DATA_TYPE_ENUM, PM_ENUM_PRESENT_MODE, mode);
    const double displayChange = 33.3;
    AddField(minimal, VrrFrameField::BetweenDisplayChange, PM_METRIC_BETWEEN_DISPLAY_CHANGE,
        PM_DATA_TYPE_DOUBLE, PM_ENUM_NULL_ENUM, displayChange);

    const auto sample = DecodeVrrFrameSample(minimal.bytes, 777, minimal.plan);
    assert(sample);
    assert(sample->processId == 777);
    assert(!sample->dropped);
    assert(!sample->presentRuntime);
}
}

int main()
{
    TestVersionGate();
    TestQueryPlanRequiresCoreAndSelectsIndependentDevice();
    TestOptionalMetricsAndDynamicOnlyExclusion();
    TestTypedFrameDecode();
    TestInvalidRequiredAndOptionalFields();
    TestPidMismatchAndMissingOptionalField();
}
