#include "WindowsPowerTelemetry.h"
#include "BatteryRuntimeEstimator.h"
#include "BatteryPowerEstimator.h"
#include "MsiEcHudTelemetry.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

using Estimator = BatteryRuntimeEstimator;

Estimator::TimePoint At(int seconds)
{
    return Estimator::TimePoint{} + std::chrono::seconds(seconds);
}

void CheckBatteryEstimator(bool& ok)
{
    Estimator estimator;
    auto result = estimator.Observe(true, 60000, At(0));
    ok &= Check(result.state == BatteryEstimateState::AnchorCreated &&
        !result.remainingMinutes, "initial DC sample creates anchor");

    for (int seconds = 5; seconds <= 25; seconds += 5)
        result = estimator.Observe(true, 60000 - seconds * 2, At(seconds));
    ok &= Check(!result.remainingMinutes, "short capacity delta is not accepted");
    result = estimator.Observe(true, 59940, At(30));
    ok &= Check(result.state == BatteryEstimateState::Updated &&
        result.remainingMinutes && result.anchorCapacity == 60000 &&
        std::abs(result.averageDischargeWatts - 7.2) < 0.01,
        "first usable rolling window produces an estimate");

    result = estimator.Observe(true, 59930, At(35));
    ok &= Check(result.state == BatteryEstimateState::Updated &&
        result.remainingMinutes && result.anchorCapacity == 59990 &&
        std::abs(result.elapsedSeconds - 30.0) < 0.01,
        "rolling update does not require a fresh anchor cycle");

    for (int seconds = 40; seconds <= 70; seconds += 5)
        result = estimator.Observe(true, 59930, At(seconds));
    ok &= Check(result.state == BatteryEstimateState::Held &&
        result.resetReason && std::wstring(result.resetReason) == L"capacity-unchanged" &&
        result.remainingMinutes && result.averageDischargeWatts > 0.0,
        "unchanged capacity holds the last valid estimate");

    for (int seconds = 75; seconds <= 130; seconds += 5)
        result = estimator.Observe(true, 59930, At(seconds));
    result = estimator.Observe(true, 59800, At(135));
    ok &= Check(result.state == BatteryEstimateState::Updated &&
        result.remainingMinutes && result.anchorCapacity == 59930 &&
        result.currentCapacity == 59800 &&
        std::abs(result.elapsedSeconds - 100.0) < 0.01,
        "capacity change after a long stall uses the full plateau window");

    Estimator coarseStepEstimator;
    coarseStepEstimator.Observe(true, 60591, At(0));
    for (int seconds = 5; seconds <= 115; seconds += 5)
        coarseStepEstimator.Observe(true, 60591, At(seconds));
    result = coarseStepEstimator.Observe(true, 60015, At(120));
    ok &= Check(result.state == BatteryEstimateState::Updated &&
        std::abs(result.elapsedSeconds - 120.0) < 0.01 &&
        std::abs(result.averageDischargeWatts - 17.28) < 0.01,
        "coarse capacity step uses the full plateau duration");

    Estimator correctionEstimator;
    correctionEstimator.Observe(true, 60000, At(0));
    correctionEstimator.Observe(true, 59990, At(5));
    correctionEstimator.Observe(true, 59980, At(10));
    correctionEstimator.Observe(true, 59970, At(15));
    correctionEstimator.Observe(true, 59960, At(20));
    correctionEstimator.Observe(true, 59950, At(25));
    correctionEstimator.Observe(true, 59940, At(30));
    result = correctionEstimator.Observe(true, 59900, At(35));
    result = correctionEstimator.Observe(true, 59920, At(40));
    ok &= Check(result.state == BatteryEstimateState::Reset &&
        result.resetReason && std::wstring(result.resetReason) ==
            L"capacity-correction" && correctionEstimator.SampleCount() == 1 &&
            result.elapsedSeconds == 0.0,
        "immediate upward capacity correction resets the window");

    Estimator gapEstimator;
    gapEstimator.Observe(true, 60000, At(0));
    gapEstimator.Observe(true, 59940, At(30));
    result = gapEstimator.Observe(true, 59900, At(95));
    ok &= Check(result.state == BatteryEstimateState::Reset &&
        result.resetReason && std::wstring(result.resetReason) == L"sample-gap" &&
        result.elapsedSeconds == 0.0,
        "sample gap resets the rolling measurement window");
    result = gapEstimator.Observe(true, 59890, At(100));
    ok &= Check(result.state != BatteryEstimateState::Updated,
        "sample gap does not contaminate the next discharge estimate");

    result = estimator.Observe(false, std::nullopt, At(140));
    ok &= Check(result.state == BatteryEstimateState::Reset &&
        result.resetReason && std::wstring(result.resetReason) == L"ac-connected" &&
        !result.remainingMinutes, "AC reconnect resets estimator");
    result = estimator.Observe(true, 59800, At(145));
    ok &= Check(result.state == BatteryEstimateState::AnchorCreated &&
        !result.remainingMinutes, "AC to DC starts a fresh anchor");

    for (int seconds = 150; seconds <= 6150; seconds += 5)
        estimator.Observe(true, 59800, At(seconds));
    ok &= Check(estimator.SampleCount() <= 37,
        "rolling sample history remains bounded to the retention window");
}

void CheckBatteryPowerEstimator(bool& ok)
{
    BatteryPowerEstimator estimator;
    for (int seconds = 0; seconds <= 9; ++seconds)
        estimator.Observe(true, 40.0, At(seconds));
    ok &= Check(!estimator.Ready() &&
        !estimator.EstimateRemainingMinutes(60000),
        "EC estimate waits for minimum history");
    estimator.Observe(true, 42.0, At(10));
    ok &= Check(estimator.Ready() && estimator.SampleCount() == 11 &&
        std::abs(estimator.AveragePowerW().value() - (40.0 * 10.0 + 42.0) / 11.0) < 0.001,
        "EC estimate becomes ready after ten seconds");
    ok &= Check(estimator.EstimateRemainingMinutes(60000).value() == 90,
        "EC estimate uses RemainingCapacity");
    estimator.Observe(true, 44.0, At(21));
    ok &= Check(estimator.SampleCount() == 11 &&
        std::abs(estimator.AveragePowerW().value() - (40.0 * 9.0 + 42.0 + 44.0) / 11.0) < 0.001,
        "EC estimate retains only the rolling twenty seconds");
    estimator.Observe(true, std::nullopt, At(22));
    ok &= Check(estimator.SampleCount() == 11 && estimator.Ready(),
        "invalid EC sample does not poison valid history");
    estimator.Reset();
    estimator.Observe(false, 40.0, At(23));
    ok &= Check(estimator.SampleCount() == 0, "AC resets EC history");
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
        dc->remainingMinutes == 150, "DC power decode");
    ok &= Check(dc && SelectRemainingMinutes(*dc, 490) == 490,
        "EC or capacity estimate takes priority over Windows estimate");
    ok &= Check(dc && SelectRemainingMinutes(*dc, std::nullopt) == 150,
        "Windows estimate remains available when no estimator result exists");

    status.ACLineStatus = 1;
    const auto ac = DecodeWindowsPowerStatus(status);
    ok &= Check(ac && ac->onBattery == false, "AC power decode");

    status.BatteryLifePercent = 255;
    status.ACLineStatus = 255;
    status.BatteryFlag = 255;
    status.BatteryLifeTime = DWORD(-1);
    const auto unavailable = DecodeWindowsPowerStatus(status);
    ok &= Check(unavailable && !unavailable->batteryPercent &&
        !unavailable->onBattery && !unavailable->remainingMinutes,
        "unavailable power fields");
    ok &= Check(unavailable && SelectRemainingMinutes(*unavailable, 490) == 490,
        "capacity estimate fills unknown Windows remaining time");
    CheckBatteryDiagnostics(ok);
    CheckBatteryEstimator(ok);
    CheckBatteryPowerEstimator(ok);
    return ok ? 0 : 1;
}
