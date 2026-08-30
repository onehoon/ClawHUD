#pragma once

#include <windows.h>
#include <powrprof.h>

#include <optional>
#include <cstdint>
#include <string>

namespace clawhud
{
struct WindowsPowerTelemetry
{
    std::optional<int> batteryPercent;
    std::optional<int> remainingMinutes;
    std::optional<bool> onBattery;
    std::optional<std::uint64_t> remainingCapacityMWh;
};

std::optional<WindowsPowerTelemetry> DecodeWindowsPowerStatus(
    const SYSTEM_POWER_STATUS& status);
std::optional<int> SelectRemainingMinutes(
    const WindowsPowerTelemetry& telemetry,
    std::optional<int> estimatedMinutes);
std::wstring FormatBatteryDiagnostics(
    BOOL gpsCallOk,
    DWORD gpsError,
    const SYSTEM_POWER_STATUS& gps,
    LONG sbsStatus,
    const SYSTEM_BATTERY_STATE& sbs);
std::optional<WindowsPowerTelemetry> ReadWindowsPowerTelemetry();
}
