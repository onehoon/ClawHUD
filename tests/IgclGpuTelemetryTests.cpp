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

    check(ShouldLogIgclInitializationFailure(false),
        "first initialization failure is logged");
    check(!ShouldLogIgclInitializationFailure(true),
        "repeated initialization failure is suppressed");
    bool initializationFailureLogged = false;
    check(ShouldLogIgclInitializationFailure(initializationFailureLogged),
        "initialization outage starts logging");
    initializationFailureLogged = true;
    check(!ShouldLogIgclInitializationFailure(initializationFailureLogged),
        "initialization outage remains deduplicated");

    check(clawhud::ObserveIgclTelemetryTransition(true, 1, false, 3) ==
        clawhud::IgclTelemetryTransition::None,
        "single failure does not mark unavailable");
    check(clawhud::ObserveIgclTelemetryTransition(true, 3, false, 3) ==
        clawhud::IgclTelemetryTransition::Unavailable,
        "third consecutive failure marks unavailable");
    check(clawhud::ObserveIgclTelemetryTransition(false, 1, false, 3) ==
        clawhud::IgclTelemetryTransition::None,
        "unavailable state ignores incomplete failure streak");
    check(clawhud::ObserveIgclTelemetryTransition(false, 0, true, 3) ==
        clawhud::IgclTelemetryTransition::Recovered,
        "successful sample recovers unavailable telemetry");
    check(clawhud::ObserveIgclTelemetryTransition(true, 0, true, 3) ==
        clawhud::IgclTelemetryTransition::None,
        "successful sample keeps available telemetry healthy");
    check(clawhud::ObserveIgclTelemetryTransition(true, 1, true, 3) ==
        clawhud::IgclTelemetryTransition::None,
        "successful sample clears failure streak");

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
    check(observe(true) == clawhud::IgclTelemetryTransition::None,
        "healthy sample has no transition");
    check(observe(false) == clawhud::IgclTelemetryTransition::None,
        "first failure has no transition");
    check(observe(true) == clawhud::IgclTelemetryTransition::None,
        "recovery before threshold has no transition");
    check(observe(false) == clawhud::IgclTelemetryTransition::None,
        "new failure streak starts without transition");
    check(observe(false) == clawhud::IgclTelemetryTransition::None,
        "second consecutive failure has no transition");
    check(observe(false) == clawhud::IgclTelemetryTransition::Unavailable,
        "third consecutive failure emits unavailable");
    check(observe(true) == clawhud::IgclTelemetryTransition::Recovered,
        "successful sample emits recovery");
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
