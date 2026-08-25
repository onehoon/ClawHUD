#include "WindowsUsageTelemetry.h"

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
    return std::abs(actual - expected) < 0.001;
}
}

int main()
{
    bool ok = true;
    ok &= Check(ValidateUsagePercent(33.0).value() == 33.0, "valid CPU usage");
    ok &= Check(ValidateUsagePercent(0.0).value() == 0.0, "valid zero usage");
    ok &= Check(!ValidateUsagePercent(-1.0) && !ValidateUsagePercent(101.0) &&
        !ValidateUsagePercent(NAN), "invalid usage omitted");
    ok &= Check(Near(MaxGpuUsagePercent({12.0, 44.0, 31.0}).value(), 44.0),
        "maximum 3D GPU usage");
    ok &= Check(!MaxGpuUsagePercent({-1.0, 101.0, NAN}),
        "invalid GPU values omitted");
    return ok ? 0 : 1;
}
