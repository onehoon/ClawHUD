#include "WindowsPowerTelemetry.h"

#include "RuntimeLogger.h"

#include <iomanip>
#include <sstream>

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

std::wstring FormatBatteryDiagnostics(
    BOOL gpsCallOk,
    DWORD gpsError,
    const SYSTEM_POWER_STATUS& gps,
    LONG sbsStatus,
    const SYSTEM_BATTERY_STATE& sbs)
{
    std::wstringstream message;
    message << L"[BatteryDiag] GPS.CallOk=" << (gpsCallOk ? 1 : 0)
        << L" GPS.GetLastError=" << gpsError;
    if (gpsCallOk)
    {
        message << L" GPS.ACLineStatus=" << static_cast<unsigned int>(gps.ACLineStatus)
            << (gps.ACLineStatus == 255 ? L"(unknown)" : L"")
            << L" GPS.BatteryFlag=" << static_cast<unsigned int>(gps.BatteryFlag)
            << (gps.BatteryFlag == 255 ? L"(unknown)" : L"")
            << L" GPS.BatteryLifePercent=" << static_cast<unsigned int>(gps.BatteryLifePercent)
            << (gps.BatteryLifePercent == 255 ? L"(unknown)" : L"")
            << L" GPS.BatteryLifeTime=" << gps.BatteryLifeTime
            << (gps.BatteryLifeTime == DWORD(-1) ? L"(unknown)" : L"")
            << L" GPS.BatteryFullLifeTime=" << gps.BatteryFullLifeTime
            << (gps.BatteryFullLifeTime == DWORD(-1) ? L"(unknown)" : L"");
    }
    else
    {
        message << L" GPS.ACLineStatus=unknown"
            << L" GPS.BatteryFlag=unknown"
            << L" GPS.BatteryLifePercent=unknown"
            << L" GPS.BatteryLifeTime=unknown"
            << L" GPS.BatteryFullLifeTime=unknown";
    }

    message << L" SBS.NtStatus=0x" << std::uppercase << std::hex << std::setfill(L'0')
        << std::setw(8) << static_cast<unsigned long>(sbsStatus);
    if (sbsStatus >= 0)
    {
        message << std::nouppercase << std::setfill(L' ') << std::dec
            << L" SBS.AcOnLine=" << (sbs.AcOnLine ? 1 : 0)
            << L" SBS.BatteryPresent=" << (sbs.BatteryPresent ? 1 : 0)
            << L" SBS.Charging=" << (sbs.Charging ? 1 : 0)
            << L" SBS.Discharging=" << (sbs.Discharging ? 1 : 0)
            << L" SBS.MaxCapacity=" << sbs.MaxCapacity
            << L" SBS.RemainingCapacity=" << sbs.RemainingCapacity
            << L" SBS.Rate=" << static_cast<LONG>(sbs.Rate)
            << L" SBS.EstimatedTime=" << sbs.EstimatedTime
            << (sbs.EstimatedTime == ULONG(-1) ? L"(unknown)" : L"");
    }
    else
    {
        message << L" SBS.AcOnLine=unknown"
            << L" SBS.BatteryPresent=unknown"
            << L" SBS.Charging=unknown"
            << L" SBS.Discharging=unknown"
            << L" SBS.MaxCapacity=unknown"
            << L" SBS.RemainingCapacity=unknown"
            << L" SBS.Rate=unknown"
            << L" SBS.EstimatedTime=unknown";
    }
    return message.str();
}

std::optional<WindowsPowerTelemetry> ReadWindowsPowerTelemetry()
{
    SYSTEM_POWER_STATUS status{};
    SetLastError(ERROR_SUCCESS);
    const BOOL gpsCallOk = GetSystemPowerStatus(&status);
    const DWORD gpsError = GetLastError();

    SYSTEM_BATTERY_STATE batteryState{};
    const LONG sbsStatus = CallNtPowerInformation(
        SystemBatteryState, nullptr, 0, &batteryState, sizeof(batteryState));

    RuntimeLogger::Log(RuntimeLogLevel::Debug,
        FormatBatteryDiagnostics(gpsCallOk, gpsError, status, sbsStatus, batteryState));

    if (!gpsCallOk)
        return std::nullopt;
    auto result = DecodeWindowsPowerStatus(status);
    if (result && sbsStatus >= 0 && batteryState.BatteryPresent &&
        batteryState.RemainingCapacity != ULONG(-1))
    {
        result->remainingCapacityMWh = batteryState.RemainingCapacity;
    }
    return result;
}

std::optional<int> SelectRemainingMinutes(
    const WindowsPowerTelemetry& telemetry,
    std::optional<int> estimatedMinutes)
{
    return estimatedMinutes ? estimatedMinutes : telemetry.remainingMinutes;
}
}
