#include "PresentMonFrameTelemetry.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
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
    PM_METRIC_TYPE type = PM_METRIC_TYPE_DYNAMIC_FRAME,
    PM_METRIC_AVAILABILITY avail = PM_METRIC_AVAILABILITY_AVAILABLE)
{
    PresentMonMetricCapability metric{};
    metric.id = id;
    metric.type = type;
    metric.devices.push_back({0, avail, 1});
    return metric;
}

PresentMonTelemetryCapabilities Capabilities(bool withBetweenDisplayChange = true,
    bool withExtras = true)
{
    PresentMonTelemetryCapabilities caps;
    caps.devices.push_back({0, PM_DEVICE_TYPE_INDEPENDENT,
        PM_DEVICE_VENDOR_UNKNOWN, "Independent"});
    if (withBetweenDisplayChange)
        caps.metrics.push_back(FrameMetric(PM_METRIC_BETWEEN_DISPLAY_CHANGE));
    if (withExtras)
    {
        caps.metrics.push_back(FrameMetric(PM_METRIC_PROCESS_ID));
        caps.metrics.push_back(FrameMetric(PM_METRIC_SWAP_CHAIN_ADDRESS));
        caps.metrics.push_back(FrameMetric(PM_METRIC_FRAME_TYPE, PM_METRIC_TYPE_FRAME_EVENT));
        caps.metrics.push_back(FrameMetric(PM_METRIC_PRESENT_MODE, PM_METRIC_TYPE_FRAME_EVENT));
    }
    return caps;
}

void QueryPlanning(bool& ok)
{
    const auto plan = BuildPresentMonFrameQueryPlan(Capabilities());
    ok &= Check(plan.has_value(), "BETWEEN_DISPLAY_CHANGE yields a frame query plan");
    ok &= Check(plan &&
        plan->elements.at(plan->betweenDisplayChangeIndex).metric ==
            PM_METRIC_BETWEEN_DISPLAY_CHANGE &&
        plan->elements.at(plan->betweenDisplayChangeIndex).stat == PM_STAT_NONE,
        "the required element is BETWEEN_DISPLAY_CHANGE with no statistic");
    ok &= Check(plan && plan->processIdIndex && plan->swapChainAddressIndex &&
        plan->frameTypeIndex && plan->presentModeIndex,
        "optional identity fields are planned when supported");

    const auto minimal = BuildPresentMonFrameQueryPlan(Capabilities(true, false));
    ok &= Check(minimal && minimal->elements.size() == 1 &&
        !minimal->processIdIndex && !minimal->swapChainAddressIndex,
        "BETWEEN_DISPLAY_CHANGE alone is a usable plan");

    ok &= Check(!BuildPresentMonFrameQueryPlan(Capabilities(false, true)),
        "no BETWEEN_DISPLAY_CHANGE means no plan");

    auto staticOnly = Capabilities(false, false);
    staticOnly.metrics.push_back(FrameMetric(PM_METRIC_BETWEEN_DISPLAY_CHANGE,
        PM_METRIC_TYPE_DYNAMIC));
    ok &= Check(!BuildPresentMonFrameQueryPlan(staticOnly),
        "a non-frame BETWEEN_DISPLAY_CHANGE metric is rejected");

    auto unavailable = Capabilities(false, false);
    unavailable.metrics.push_back(FrameMetric(PM_METRIC_BETWEEN_DISPLAY_CHANGE,
        PM_METRIC_TYPE_DYNAMIC_FRAME, PM_METRIC_AVAILABILITY_UNAVAILABLE));
    ok &= Check(!BuildPresentMonFrameQueryPlan(unavailable),
        "an unavailable BETWEEN_DISPLAY_CHANGE metric is rejected");
}

void Decoding(bool& ok)
{
    std::vector<std::uint8_t> record(24);
    PM_QUERY_ELEMENT element{PM_METRIC_BETWEEN_DISPLAY_CHANGE, PM_STAT_NONE, 0, 0,
        8, sizeof(double)};
    const double value = 8.33;
    std::memcpy(record.data() + 8, &value, sizeof(value));
    ok &= Check(DecodePresentMonFrameDouble(record, element) == value,
        "a double frame field decodes from its registered offset");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::memcpy(record.data() + 8, &nan, sizeof(nan));
    ok &= Check(!DecodePresentMonFrameDouble(record, element),
        "a non-finite frame field is unavailable");

    const double zero = 0.0;
    std::memcpy(record.data() + 8, &zero, sizeof(zero));
    ok &= Check(DecodePresentMonFrameDouble(record, element) == 0.0,
        "zero decodes (the displayed-frame threshold check is separate)");

    PM_QUERY_ELEMENT outOfRange{PM_METRIC_BETWEEN_DISPLAY_CHANGE, PM_STAT_NONE, 0, 0,
        20, sizeof(double)};
    ok &= Check(!DecodePresentMonFrameDouble(record, outOfRange),
        "an element that runs past the record is unavailable");
}

void FirstFrameDetection(bool& ok)
{
    PresentMonDisplayedFrameDetector detector;
    ok &= Check(!detector.Observe(0.0) && !detector.DisplayedFrameSeen(),
        "BETWEEN_DISPLAY_CHANGE <= 0 is not a displayed frame");
    ok &= Check(!detector.Observe(-1.0) && !detector.DisplayedFrameSeen(),
        "a negative interval is not a displayed frame");
    ok &= Check(!detector.Observe(std::numeric_limits<double>::infinity()),
        "a non-finite interval is not a displayed frame");
    ok &= Check(detector.Observe(8.33) && detector.DisplayedFrameSeen(),
        "the first positive interval reports the first displayed frame");
    ok &= Check(!detector.Observe(8.33) && !detector.Observe(16.6),
        "subsequent frames do not re-report");
    detector.Reset();
    ok &= Check(!detector.DisplayedFrameSeen() && detector.Observe(1.0),
        "Reset re-arms the detector");
}

int RunTests()
{
    bool ok = true;
    QueryPlanning(ok);
    Decoding(ok);
    FirstFrameDetection(ok);
    return ok ? 0 : 1;
}
}

int main()
{
    return RunTests();
}
