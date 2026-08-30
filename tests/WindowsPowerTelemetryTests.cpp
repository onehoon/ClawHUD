#include "WindowsPowerTelemetry.h"
#include "BatteryPowerEstimator.h"

#include <chrono>
#include <cmath>
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

BatteryPowerEstimator::TimePoint At(int seconds)
{
    return BatteryPowerEstimator::TimePoint{} + std::chrono::seconds(seconds);
}

void CheckBatteryPowerEstimator(bool& ok)
{
    BatteryPowerEstimator estimator;
    for (int seconds = 1; seconds <= 9; ++seconds)
        estimator.Observe(true, 40.0, At(seconds));
    ok &= Check(!estimator.Ready() &&
        !estimator.EstimateRemainingMinutes(60000),
        "EC estimate waits for minimum history");

    estimator.Observe(true, 42.0, At(10));
    ok &= Check(estimator.Ready() && estimator.SampleCount() == 10 &&
        std::abs(estimator.AveragePowerW().value() - (40.0 * 9.0 + 42.0) / 10.0) < 0.001,
        "EC estimate is ready at the second five-second BAT interval");
    ok &= Check(estimator.EstimateRemainingMinutes(60000).value() == 90,
        "EC estimate uses RemainingCapacity");

    estimator.Observe(true, 44.0, At(21));
    ok &= Check(estimator.SampleCount() == 11 &&
        std::abs(estimator.AveragePowerW().value() -
            (40.0 * 9.0 + 42.0 + 44.0) / 11.0) < 0.001,
        "EC estimate retains only the rolling twenty seconds");
    estimator.Observe(true, std::nullopt, At(22));
    ok &= Check(estimator.SampleCount() == 11 && estimator.Ready(),
        "invalid EC sample does not poison valid history");
    estimator.Reset();
    estimator.Observe(false, 40.0, At(23));
    ok &= Check(estimator.SampleCount() == 0 && !estimator.Ready(),
        "AC resets EC history");
}

void CheckBatteryDiagnostics(bool& ok)
{
    SYSTEM_POWER_STATUS gps{};
    gps.ACLineStatus = 0;
    gps.BatteryFlag = 1;
    gps.BatteryLifePercent = 72;
    gps.BatteryLifeTime = DWORD(-1);
    gps.BatteryFullLifeTime = DWORD(-1);

    SYSTEM_BATTERY_STATE sbs{};
    sbs.AcOnLine = 0;
    sbs.BatteryPresent = 1;
    sbs.Discharging = 1;
    sbs.MaxCapacity = 80000;
    sbs.RemainingCapacity = 57600;
    sbs.Rate = -18500;
    sbs.EstimatedTime = ULONG(-1);

    const auto message = FormatBatteryDiagnostics(
        TRUE, ERROR_SUCCESS, gps, 0L, sbs);
    ok &= Check(message.find(L"GPS.BatteryLifeTime=4294967295(unknown)") != std::wstring::npos,
        "GPS unknown lifetime remains identifiable");
    ok &= Check(message.find(L"GPS.BatteryFullLifeTime=4294967295(unknown)") != std::wstring::npos,
        "GPS unknown full lifetime remains identifiable");
    ok &= Check(message.find(L"SBS.Rate=-18500") != std::wstring::npos,
        "SBS rate remains signed");
    ok &= Check(message.find(L"SBS.EstimatedTime=4294967295(unknown)") != std::wstring::npos,
        "SBS unknown estimate remains identifiable");

    const auto failure = FormatBatteryDiagnostics(
        FALSE, ERROR_ACCESS_DENIED, gps, static_cast<LONG>(0xC00000A3u), sbs);
    ok &= Check(failure.find(L"GPS.CallOk=0 GPS.GetLastError=5") != std::wstring::npos,
        "GPS failure is logged");
    ok &= Check(failure.find(L"GPS.BatteryLifeTime=unknown") != std::wstring::npos,
        "GPS failure fields are unknown");
    ok &= Check(failure.find(L"SBS.NtStatus=0xC00000A3") != std::wstring::npos,
        "SBS failure status is logged");
    ok &= Check(failure.find(L"SBS.Rate=unknown") != std::wstring::npos,
        "SBS failure fields are unknown");

    gps.ACLineStatus = 255;
    gps.BatteryFlag = 255;
    gps.BatteryLifePercent = 255;
    const auto unknown = FormatBatteryDiagnostics(TRUE, ERROR_SUCCESS, gps, 0L, sbs);
    ok &= Check(unknown.find(L"GPS.ACLineStatus=255(unknown)") != std::wstring::npos,
        "GPS AC status sentinel remains identifiable");
    ok &= Check(unknown.find(L"GPS.BatteryFlag=255(unknown)") != std::wstring::npos,
        "GPS battery flag sentinel remains identifiable");
    ok &= Check(unknown.find(L"GPS.BatteryLifePercent=255(unknown)") != std::wstring::npos,
        "GPS battery percent sentinel remains identifiable");
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
        !dc->remainingMinutes, "DC power decode without Windows lifetime estimate");

    status.ACLineStatus = 1;
    const auto ac = DecodeWindowsPowerStatus(status);
    ok &= Check(ac && ac->onBattery == false && !ac->remainingMinutes,
        "AC power decode without Windows lifetime estimate");

    status.BatteryLifePercent = 255;
    status.ACLineStatus = 255;
    status.BatteryFlag = 255;
    status.BatteryLifeTime = DWORD(-1);
    const auto unavailable = DecodeWindowsPowerStatus(status);
    ok &= Check(unavailable && !unavailable->batteryPercent &&
        !unavailable->onBattery && !unavailable->remainingMinutes,
        "unavailable power fields");
    CheckBatteryDiagnostics(ok);
    CheckBatteryPowerEstimator(ok);
    return ok ? 0 : 1;
}
