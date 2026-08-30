#include "WindowsPowerTelemetry.h"
#include "BatteryRuntimeEstimator.h"

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
    auto result = estimator.Observe(true, 60777, At(0));
    ok &= Check(result.state == BatteryEstimateState::AnchorCreated &&
        !result.remainingMinutes, "initial DC sample creates anchor");

    result = estimator.Observe(true, 60715, At(25));
    ok &= Check(!result.remainingMinutes, "short capacity delta is not accepted");
    result = estimator.Observe(true, 60777, At(30));
    ok &= Check(result.state == BatteryEstimateState::Waiting &&
        !result.remainingMinutes, "unchanged capacity does not produce zero rate");
    result = estimator.Observe(true, 60715, At(40));
    ok &= Check(result.state == BatteryEstimateState::Updated &&
        result.remainingMinutes && std::abs(result.averageDischargeWatts - 5.58) < 0.01,
        "capacity delta uses retained anchor and full window");

    result = estimator.Observe(true, 60715, At(45));
    ok &= Check(result.remainingMinutes &&
        std::abs(result.averageDischargeWatts - 5.58) < 0.01,
        "previous discharge estimate is retained");

    result = estimator.Observe(true, 60740, At(50));
    ok &= Check(result.state == BatteryEstimateState::Reset &&
        result.resetReason && std::wstring(result.resetReason) == L"capacity-correction" &&
        result.averageDischargeWatts > 0.0,
        "capacity increase resets without negative discharge");

    result = estimator.Observe(false, std::nullopt, At(55));
    ok &= Check(result.state == BatteryEstimateState::Reset &&
        result.resetReason && std::wstring(result.resetReason) == L"ac-connected" &&
        !result.remainingMinutes, "AC reconnect resets estimator");
    result = estimator.Observe(true, 60740, At(60));
    ok &= Check(result.state == BatteryEstimateState::AnchorCreated &&
        !result.remainingMinutes, "AC to DC starts a fresh anchor");

    result = estimator.Observe(true, 60740, At(125));
    ok &= Check(result.state == BatteryEstimateState::Reset &&
        result.resetReason && std::wstring(result.resetReason) == L"sample-gap" &&
        !result.remainingMinutes, "large sample gap resets measurement window");
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
    ok &= Check(dc && SelectRemainingMinutes(*dc, 490) == 150,
        "Windows remaining time takes priority");

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
    return ok ? 0 : 1;
}
