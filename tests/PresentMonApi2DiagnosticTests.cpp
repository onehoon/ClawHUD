#include "PresentMonApi2Diagnostic.h"

#include <cstring>
#include <iostream>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool ok = true;
    ok &= Check(ClassifyApi2Metric(true, true, true, true, true) ==
        Api2MetricResult::Working, "changing dynamic metric is working");
    ok &= Check(ClassifyApi2Metric(true, true, true, false, true) ==
        Api2MetricResult::ZeroOnly, "zero-only metric is classified separately");
    ok &= Check(ClassifyApi2Metric(true, true, true, true, false) ==
        Api2MetricResult::Static, "static metric is classified separately");
    ok &= Check(ClassifyApi2Metric(false, true, false, false, true) ==
        Api2MetricResult::Unavailable, "unavailable metric is recorded");
    ok &= Check(ClassifyApi2Metric(true, false, false, false, true) ==
        Api2MetricResult::QueryFailed, "query failure is recorded");
    ok &= Check(ClassifyApi2Metric(true, true, false, false, true) ==
        Api2MetricResult::Invalid, "missing sample is invalid");
    ok &= Check(Api2MetricFailureIsNonFatal(),
        "one metric failure does not abort the diagnostic");
    ok &= Check(Api2FrameConsumeZeroIsNonFatal(0),
        "zero frame consume is not an immediate failure");
    ok &= Check(!Api2TargetPidIsUsable(0, 100) &&
        !Api2TargetPidIsUsable(100, 100) && Api2TargetPidIsUsable(101, 100),
        "missing or self target PID is skipped without fabrication");
    ok &= Check(Api2StaticMetricType(PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_UINT64) ==
        PM_DATA_TYPE_UINT64, "static metrics use their raw frame type");
    const std::uint64_t memorySize = 19288697856ull;
    ok &= Check(Api2DecodeStaticValue(
        reinterpret_cast<const std::uint8_t*>(&memorySize), PM_DATA_TYPE_UINT64) ==
        "19288697856", "static uint64 values decode as integers");
    const std::uint32_t arraySize = 3;
    ok &= Check(Api2DecodeStaticValue(
        reinterpret_cast<const std::uint8_t*>(&arraySize), PM_DATA_TYPE_UINT32) ==
        "3", "static uint32 values decode as integers");
    const std::int32_t enumValue = 7;
    ok &= Check(Api2DecodeStaticValue(
        reinterpret_cast<const std::uint8_t*>(&enumValue), PM_DATA_TYPE_ENUM) ==
        "7", "static enum values decode as integers");
    const bool enabled = true;
    ok &= Check(Api2DecodeStaticValue(
        reinterpret_cast<const std::uint8_t*>(&enabled), PM_DATA_TYPE_BOOL) ==
        "true", "static bool values decode as booleans");
    const char name[] = "Intel Arc";
    ok &= Check(Api2DecodeStaticValue(
        reinterpret_cast<const std::uint8_t*>(name), PM_DATA_TYPE_STRING) ==
        "Intel Arc", "static string values decode as strings");
    ok &= Check(Api2MetricSupportsFrameQuery(PM_METRIC_TYPE_FRAME_EVENT) &&
        Api2MetricSupportsFrameQuery(PM_METRIC_TYPE_DYNAMIC_FRAME) &&
        !Api2MetricSupportsFrameQuery(PM_METRIC_TYPE_DYNAMIC) &&
        !Api2MetricSupportsFrameQuery(PM_METRIC_TYPE_STATIC),
        "frame queries include frame-event and dynamic-frame metrics only");
    ok &= Check(Api2FrameMetricType(PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_UINT64) ==
        PM_DATA_TYPE_UINT64, "dynamic-frame metrics decode using frameType");
    const auto path = Api2DiagnosticOutputPath(L"logs", L"20260830-120000",
        L"-frames.csv");
    ok &= Check(path == std::filesystem::path(L"logs/api2-20260830-120000-frames.csv"),
        "API2 output filenames remain separated");
    ok &= Check(std::string(Api2MetricResultName(Api2MetricResult::Working)) ==
        "WORKING", "metric result names are stable");
    return ok ? 0 : 1;
}
