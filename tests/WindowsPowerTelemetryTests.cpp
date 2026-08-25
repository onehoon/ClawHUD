#include "WindowsPowerTelemetry.h"

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
    SYSTEM_POWER_STATUS status{};
    status.BatteryLifePercent = 72;
    status.ACLineStatus = 0;
    status.BatteryLifeTime = 9000;
    const auto dc = DecodeWindowsPowerStatus(status);
    ok &= Check(dc && dc->batteryPercent == 72 && dc->onBattery == true &&
        dc->remainingMinutes == 150, "DC power decode");

    status.ACLineStatus = 1;
    const auto ac = DecodeWindowsPowerStatus(status);
    ok &= Check(ac && ac->onBattery == false, "AC power decode");

    status.BatteryLifePercent = 255;
    status.ACLineStatus = 255;
    status.BatteryLifeTime = DWORD(-1);
    const auto unavailable = DecodeWindowsPowerStatus(status);
    ok &= Check(unavailable && !unavailable->batteryPercent &&
        !unavailable->onBattery && !unavailable->remainingMinutes,
        "unavailable power fields");
    return ok ? 0 : 1;
}
