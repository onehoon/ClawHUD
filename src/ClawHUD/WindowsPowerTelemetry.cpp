#include "WindowsPowerTelemetry.h"

namespace clawhud
{
std::optional<WindowsPowerTelemetry> DecodeWindowsPowerStatus(
    const SYSTEM_POWER_STATUS& status)
{
    WindowsPowerTelemetry result{};
    if (status.BatteryLifePercent != 255)
        result.batteryPercent = static_cast<int>(status.BatteryLifePercent);
    if (status.ACLineStatus != 255)
        result.onBattery = status.ACLineStatus == 0;
    if (status.BatteryLifeTime != DWORD(-1))
        result.remainingMinutes = static_cast<int>(status.BatteryLifeTime / 60);
    return result;
}

std::optional<WindowsPowerTelemetry> ReadWindowsPowerTelemetry()
{
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status))
        return std::nullopt;
    return DecodeWindowsPowerStatus(status);
}
}
