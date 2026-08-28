#include "IgclGpuTelemetry.h"

#include <cmath>
#include <iostream>

using namespace clawhud;

int main()
{
    bool ok = true;
    const auto check = [&](bool value, const char* name)
    {
        if (!value)
        {
            std::cerr << name << "\n";
            ok = false;
        }
    };

    const auto almostEqual = [](double actual, double expected)
    {
        return std::abs(actual - expected) < 0.001;
    };
    check(almostEqual(CalculateIgclGpuUsage(10.0, 5.0, 11.0, 5.95).value(), 95.0),
        "usage delta");
    check(!CalculateIgclGpuUsage(10.0, 5.0, 10.0, 6.0),
        "non-increasing timestamp");
    check(!CalculateIgclGpuUsage(10.0, 5.0, 11.0, 4.0),
        "decreasing activity");
    check(!CalculateIgclGpuUsage(10.0, 5.0, 11.0, NAN),
        "non-finite activity");
    check(CalculateIgclGpuUsage(10.0, 5.0, 11.0, 7.0).value() == 100.0,
        "usage clamp");
    check(CalculateIgclGpuUsage(10.0, 5.0, 11.0, 5.0).value() == 0.0,
        "zero activity delta");
    return ok ? 0 : 1;
}
