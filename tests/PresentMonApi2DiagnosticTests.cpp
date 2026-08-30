#include "PresentMonApi2Diagnostic.h"

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
    const auto path = Api2DiagnosticOutputPath(L"logs", L"20260830-120000",
        L"-frames.csv");
    ok &= Check(path == std::filesystem::path(L"logs/api2-20260830-120000-frames.csv"),
        "API2 output filenames remain separated");
    ok &= Check(std::string(Api2MetricResultName(Api2MetricResult::Working)) ==
        "WORKING", "metric result names are stable");
    return ok ? 0 : 1;
}
