#include "PresentMonDebugFrameTelemetry.h"

#include <cstring>
#include <iostream>
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

PresentMonMetricCapability FrameMetric(PM_METRIC id,
    PM_METRIC_AVAILABILITY avail = PM_METRIC_AVAILABILITY_AVAILABLE)
{
    PresentMonMetricCapability metric{};
    metric.id = id;
    metric.type = PM_METRIC_TYPE_FRAME_EVENT;
    metric.devices.push_back({0, avail, 1});
    return metric;
}
}

int main()
{
    bool ok = true;

    PresentMonTelemetryCapabilities caps;
    caps.metrics.push_back(FrameMetric(PM_METRIC_BETWEEN_DISPLAY_CHANGE));
    caps.metrics.push_back(FrameMetric(PM_METRIC_SWAP_CHAIN_ADDRESS));
    caps.metrics.push_back(FrameMetric(PM_METRIC_PRESENT_MODE));
    caps.metrics.push_back(FrameMetric(PM_METRIC_FRAME_TYPE));
    const auto plan = BuildPresentMonDebugFrameQueryPlan(caps);
    ok &= Check(!plan.Empty() && plan.elements.size() == 4 &&
        plan.betweenDisplayChangeIndex && plan.swapChainAddressIndex &&
        plan.presentModeIndex && plan.frameTypeIndex,
        "every supported debug frame field is planned");

    ok &= Check(BuildPresentMonDebugFrameQueryPlan({}).Empty(),
        "no supported frame metrics means an unusable plan");

    // Lay out records manually: [double bdc][u64 swap][i32 mode][i32 type]
    PresentMonDebugFrameQueryPlan manual;
    manual.elements = {
        {PM_METRIC_BETWEEN_DISPLAY_CHANGE, PM_STAT_NONE, 0, 0, 0, sizeof(double)},
        {PM_METRIC_SWAP_CHAIN_ADDRESS, PM_STAT_NONE, 0, 0, 8, sizeof(std::uint64_t)},
        {PM_METRIC_PRESENT_MODE, PM_STAT_NONE, 0, 0, 16, sizeof(std::int32_t)},
        {PM_METRIC_FRAME_TYPE, PM_STAT_NONE, 0, 0, 20, sizeof(std::int32_t)}};
    manual.betweenDisplayChangeIndex = 0;
    manual.swapChainAddressIndex = 1;
    manual.presentModeIndex = 2;
    manual.frameTypeIndex = 3;
    constexpr std::uint32_t recordSize = 24;

    std::vector<std::uint8_t> blob(recordSize * 2);
    auto writeRecord = [&](std::size_t i, double bdc, std::uint64_t swap,
        std::int32_t mode, std::int32_t type)
    {
        std::uint8_t* r = blob.data() + i * recordSize;
        std::memcpy(r + 0, &bdc, sizeof(bdc));
        std::memcpy(r + 8, &swap, sizeof(swap));
        std::memcpy(r + 16, &mode, sizeof(mode));
        std::memcpy(r + 20, &type, sizeof(type));
    };
    writeRecord(0, 8.33, 0x1111,
        PM_PRESENT_MODE_COMPOSED_FLIP, PM_FRAME_TYPE_APPLICATION);
    writeRecord(1, 16.6, 0x2222,
        PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP, PM_FRAME_TYPE_INTEL_XEFG);

    const auto folded = FoldPresentMonDebugFrames(manual, blob, 2, recordSize);
    ok &= Check(folded.betweenDisplayChangeMs == 16.6 &&
        folded.swapChainAddress == 0x2222ULL &&
        folded.presentMode == PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP &&
        folded.frameType == PM_FRAME_TYPE_INTEL_XEFG,
        "folding keeps the newest record's fields");

    const auto none = FoldPresentMonDebugFrames(manual, blob, 0, recordSize);
    ok &= Check(!none.swapChainAddress && !none.betweenDisplayChangeMs,
        "an empty batch folds to nothing");

    ok &= Check(std::string(PresentMonPresentModeName(
        PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP)) == "HardwareIndependentFlip" &&
        std::string(PresentMonFrameTypeName(PM_FRAME_TYPE_INTEL_XEFG)) == "IntelXeFG" &&
        std::string(PresentMonPresentModeName(999)) == "Unknown",
        "enum names map, unknown values fall back");

    return ok ? 0 : 1;
}
