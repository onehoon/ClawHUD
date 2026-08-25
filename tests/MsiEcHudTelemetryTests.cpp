#include "MsiEcHudTelemetry.h"

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
    ok &= Check(SelectHudFanRpm(4285, 4324).value() == (4285 + 4324) / 2,
        "fan average");
    ok &= Check(SelectHudFanRpm(4285, std::nullopt).value() == 4285 &&
        SelectHudFanRpm(std::nullopt, 4324).value() == 4324 &&
        !SelectHudFanRpm(std::nullopt, std::nullopt), "fan partial availability");

    ok &= Check(DecodeCpuPackagePowerW(std::vector<std::uint8_t>{0x02}).value() == 2 &&
        DecodeCpuPackagePowerW(std::vector<std::uint8_t>{0x18}).value() == 24 &&
        DecodeCpuPackagePowerW(std::vector<std::uint8_t>{0x00}).value() == 0 &&
        !DecodeCpuPackagePowerW(std::vector<std::uint8_t>{}), "CPU package power decode");
    return ok ? 0 : 1;
}
