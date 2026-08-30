#pragma once

#include <windows.h>
#include <powrprof.h>

#include <optional>
#include <string>

namespace clawhud
{
struct WindowsPowerTelemetry
{
    std::optional<int> batteryPercent;
    std::optional<int> remainingMinutes;
    std::optional<bool> onBattery;
};

std::optional<WindowsPowerTelemetry> DecodeWindowsPowerStatus(
    const SYSTEM_POWER_STATUS& status);
std::wstring FormatBatteryDiagnostics(
    BOOL gpsCallOk,
    DWORD gpsError,
    const SYSTEM_POWER_STATUS& gps,
    LONG sbsStatus,
    const SYSTEM_BATTERY_STATE& sbs);
std::optional<WindowsPowerTelemetry> ReadWindowsPowerTelemetry();
}
