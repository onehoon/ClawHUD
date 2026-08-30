#include "TelemetryRetention.h"

#include <iostream>
#include <optional>

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
    std::optional<double> value = 25.0;
    unsigned misses = 0;
    const std::optional<double> missingDouble;
    UpdateRetainedTelemetryField(value, missingDouble, misses, 3);
    ok &= Check(value == 25.0 && misses == 1,
        "a missing sample retains the last value");
    UpdateRetainedTelemetryField(value, std::optional<double>{55.0}, misses, 3);
    ok &= Check(value == 55.0 && misses == 0,
        "a valid sample updates and resets retention");
    UpdateRetainedTelemetryField(value, missingDouble, misses, 3);
    UpdateRetainedTelemetryField(value, missingDouble, misses, 3);
    UpdateRetainedTelemetryField(value, missingDouble, misses, 3);
    ok &= Check(!value && misses == 0,
        "the missing threshold invalidates retained data");

    std::optional<std::uint64_t> empty;
    unsigned emptyMisses = 0;
    const std::optional<std::uint64_t> missingBytes;
    UpdateRetainedTelemetryField(empty, missingBytes, emptyMisses, 3);
    ok &= Check(!empty && emptyMisses == 0,
        "missing data without a cached value remains empty");
    return ok ? 0 : 1;
}
