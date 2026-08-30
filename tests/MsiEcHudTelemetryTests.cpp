#include "MsiEcHudTelemetry.h"
#include "TelemetryRetention.h"

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
}

int main()
{
    bool ok = true;
    ok &= Check(DecodeCpuTempC(std::vector<std::uint8_t>{0x20}).value() == 32, "CPU temperature 0x20");
    ok &= Check(DecodeCpuTempC(std::vector<std::uint8_t>{0x2C}).value() == 44, "CPU temperature 0x2C");
    ok &= Check(DecodeCpuTempC(std::vector<std::uint8_t>{0x34}).value() == 52, "CPU temperature 0x34");
    ok &= Check(!DecodeCpuTempC(std::vector<std::uint8_t>{0x00}) &&
        !DecodeCpuTempC(std::vector<std::uint8_t>{}), "CPU temperature unavailable");

    ok &= Check(DecodeFanRpm(0x00, 0x6F).value() == 4324, "fan 4324 RPM");
    ok &= Check(DecodeFanRpm(0x00, 0x70).value() == 4285, "fan 4285 RPM");
    ok &= Check(DecodeFanRpm(0x00, 0x71).value() == 4247, "fan signed delta");
    ok &= Check(DecodeFanRpm(0x70, 0x70).value() == 0, "fan zero RPM");
    ok &= Check(!DecodeFanTelemetry(std::vector<std::uint8_t>{0x00, 0x6F, 0x00}), "short fan payload");
    const auto fans = DecodeFanTelemetry(std::vector<std::uint8_t>{0x00, 0x70, 0x00, 0x6F});
    ok &= Check(fans && fans->fan1Rpm.value() == 4285 && fans->fan2Rpm.value() == 4324,
        "fan pair decode");
    ok &= Check(DecodeCpuPackagePowerW(std::vector<std::uint8_t>{0x02}).value() == 2 &&
        DecodeCpuPackagePowerW(std::vector<std::uint8_t>{0x18}).value() == 24 &&
        DecodeCpuPackagePowerW(std::vector<std::uint8_t>{0x00}).value() == 0 &&
        !DecodeCpuPackagePowerW(std::vector<std::uint8_t>{}), "CPU package power decode");

    std::optional<int> temperature;
    const std::optional<int> temperature67 = 67;
    const std::optional<int> temperature68 = 68;
    unsigned temperatureMisses = 0;
    const std::optional<int> missing;
    UpdateRetainedTelemetryField(temperature, temperature67, temperatureMisses, 3);
    UpdateRetainedTelemetryField(temperature, missing, temperatureMisses, 3);
    UpdateRetainedTelemetryField(temperature, missing, temperatureMisses, 3);
    ok &= Check(temperature && *temperature == 67,
        "EC temperature retains through two missing samples");
    UpdateRetainedTelemetryField(temperature, missing, temperatureMisses, 3);
    ok &= Check(!temperature,
        "EC temperature clears after three missing samples");
    UpdateRetainedTelemetryField(temperature, temperature68, temperatureMisses, 3);
    ok &= Check(temperature && *temperature == 68 && temperatureMisses == 0,
        "EC temperature recovers immediately");

    std::optional<int> fan1;
    std::optional<int> fan2;
    std::optional<int> tdp;
    unsigned fan1Misses = 0;
    unsigned fan2Misses = 0;
    unsigned tdpMisses = 0;
    const std::optional<int> fan13200 = 3200;
    const std::optional<int> fan23500 = 3500;
    const std::optional<int> fan23600 = 3600;
    const std::optional<int> tdp22 = 22;
    const std::optional<int> tdpZero = 0;
    UpdateRetainedTelemetryField(fan1, fan13200, fan1Misses, 3);
    UpdateRetainedTelemetryField(fan2, fan23500, fan2Misses, 3);
    UpdateRetainedTelemetryField(tdp, tdp22, tdpMisses, 3);
    UpdateRetainedTelemetryField(fan1, missing, fan1Misses, 3);
    UpdateRetainedTelemetryField(fan2, fan23600, fan2Misses, 3);
    ok &= Check(fan1 && *fan1 == 3200 && fan2 && *fan2 == 3600,
        "EC fan fields retain independently");
    UpdateRetainedTelemetryField(tdp, tdpZero, tdpMisses, 3);
    UpdateRetainedTelemetryField(tdp, missing, tdpMisses, 3);
    ok &= Check(tdp && *tdp == 0 && tdpMisses == 1,
        "EC TDP zero is valid and retained");
    return ok ? 0 : 1;
}
