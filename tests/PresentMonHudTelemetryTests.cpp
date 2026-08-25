#include "PresentMonHudTelemetry.h"

#include <cmath>
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
bool Near(double actual, double expected)
{
    return std::abs(actual - expected) < 0.5;
}
}

int main()
{
    bool ok = true;
    ok &= Check(Near(CalculateDisplayedFps(60, 0.500).value(), 120.0), "120 FPS bucket");
    ok &= Check(Near(CalculateDisplayedFps(20, 0.500).value(), 40.0), "40 FPS bucket");
    ok &= Check(Near(CalculateDisplayedFps(37, 0.507).value(), 37.0 / 0.507),
        "actual elapsed bucket");
    ok &= Check(!CalculateDisplayedFps(0, 0.500), "empty window unavailable");
    ok &= Check(!CalculateDisplayedFps(20, 0.0), "invalid elapsed window unavailable");
    ok &= Check(Near(CalculateDisplayedFpsFromIntervals({250.0, 250.0}).value(), 4.0),
        "display-timeline 500ms bucket");
    ok &= Check(!CalculateDisplayedFpsFromIntervals({100.0, 200.0}),
        "incomplete display-timeline bucket unavailable");
    ok &= Check(!CalculateDisplayedFpsFromIntervals({250.0, -1.0}),
        "invalid display interval unavailable");

    const std::vector<std::string> headers{
        "DisplayedTime", "MsBetweenDisplayChange", "FrameType"};
    const auto application = ParseDisplayedFrame(headers, {"10.0", "8.33", "Application"});
    ok &= Check(application && application->frameType == "Application",
        "application frame accepted");
    const auto generated = ParseDisplayedFrame(headers, {"18.0", "8.33", "Intel XeSS-FG"});
    ok &= Check(generated && generated->frameType == "Intel XeSS-FG",
        "generated frame accepted");
    ok &= Check(!ParseDisplayedFrame(headers, {"NA", "8.33", "Application"}),
        "not-displayed row ignored");
    ok &= Check(!ParseDisplayedFrame(headers, {"10.0", "bad", "Application"}),
        "malformed row ignored");
    return ok ? 0 : 1;
}
