#include "IgclGpuTelemetry.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace clawhud;

int main()
{
    assert(clawhud::ObserveIgclTelemetryTransition(true, 1, false, 3) ==
        clawhud::IgclTelemetryTransition::None);
    assert(clawhud::ObserveIgclTelemetryTransition(true, 3, false, 3) ==
        clawhud::IgclTelemetryTransition::Unavailable);
    assert(clawhud::ObserveIgclTelemetryTransition(false, 1, false, 3) ==
        clawhud::IgclTelemetryTransition::None);
    assert(clawhud::ObserveIgclTelemetryTransition(false, 0, true, 3) ==
        clawhud::IgclTelemetryTransition::Recovered);
    assert(clawhud::ObserveIgclTelemetryTransition(true, 0, true, 3) ==
        clawhud::IgclTelemetryTransition::None);
    assert(clawhud::ObserveIgclTelemetryTransition(true, 1, true, 3) ==
        clawhud::IgclTelemetryTransition::None);

    unsigned failures = 0;
    bool available = true;
    auto observe = [&](bool success)
    {
        if (success)
            failures = 0;
        else
            ++failures;
        const auto transition = clawhud::ObserveIgclTelemetryTransition(
            available, failures, success, 3);
        if (transition == clawhud::IgclTelemetryTransition::Unavailable)
            available = false;
        else if (transition == clawhud::IgclTelemetryTransition::Recovered)
            available = true;
        return transition;
    };
    assert(observe(true) == clawhud::IgclTelemetryTransition::None);
    assert(observe(false) == clawhud::IgclTelemetryTransition::None);
    assert(observe(true) == clawhud::IgclTelemetryTransition::None);
    assert(observe(false) == clawhud::IgclTelemetryTransition::None);
    assert(observe(false) == clawhud::IgclTelemetryTransition::None);
    assert(observe(false) == clawhud::IgclTelemetryTransition::Unavailable);
    assert(observe(true) == clawhud::IgclTelemetryTransition::Recovered);
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
