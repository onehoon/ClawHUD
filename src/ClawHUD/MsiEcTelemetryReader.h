#pragma once

#include "EcHelperClient.h"
#include "MsiEcHudTelemetry.h"
#include "SupportedHardware.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace clawhud
{
// The caller supplies the existing lazy helper owner. Keeping the actual
// conditional read sequence here lets tests exercise production behavior with
// a fake reader without starting WMI, a pipe, or UAC.
template<class ReaderFactory>
MsiEcHudTelemetry ReadMsiEcHudTelemetry(
    const HudSensorSupport& sensors, bool onBattery,
    ReaderFactory&& getReader)
{
    MsiEcHudTelemetry result{};
    const bool readBatteryDischarge = sensors.msiBatteryDischarge && onBattery;
    if (!sensors.msiCpuTemperature && !sensors.msiFanRpm &&
        !sensors.msiCpuPackagePower && !readBatteryDischarge)
        return result;

    auto& reader = getReader();
    const auto abortAfterFailure = [&reader]()
    {
        return ShouldAbortEcTelemetrySample(reader.LastStage());
    };

    std::vector<std::uint8_t> payload;
    if (sensors.msiCpuTemperature)
    {
        if (reader.ReadTemperature(payload))
            result.cpuTempC = DecodeCpuTempC(payload);
        else if (abortAfterFailure())
            return result;
    }

    if (sensors.msiFanRpm)
    {
        payload.clear();
        if (reader.ReadFan(payload))
        {
            if (const auto fans = DecodeFanTelemetry(payload))
            {
                result.fan1Rpm = fans->fan1Rpm;
                result.fan2Rpm = fans->fan2Rpm;
            }
        }
        else if (abortAfterFailure())
            return result;
    }

    if (sensors.msiCpuPackagePower)
    {
        payload.clear();
        if (reader.ReadData(221, payload))
            result.cpuPackagePowerW = DecodeCpuPackagePowerW(payload);
        else if (abortAfterFailure())
            return result;
    }

    if (!readBatteryDischarge)
        return result;

    std::vector<std::uint8_t> currentLow;
    std::vector<std::uint8_t> currentHigh;
    std::vector<std::uint8_t> voltageLow;
    std::vector<std::uint8_t> voltageHigh;
    const bool c0 = reader.ReadData(70, currentLow);
    if (!c0 || currentLow.empty())
        return result;
    const bool c1 = reader.ReadData(71, currentHigh);
    if (!c1 || currentHigh.empty())
        return result;
    const bool v0 = reader.ReadData(74, voltageLow);
    if (!v0 || voltageLow.empty())
        return result;
    const bool v1 = reader.ReadData(75, voltageHigh);
    if (!v1 || voltageHigh.empty())
        return result;
    if (c0 && c1 && v0 && v1 && !currentLow.empty() && !currentHigh.empty() &&
        !voltageLow.empty() && !voltageHigh.empty())
    {
        const auto battery = DecodeBatteryPower(
            currentLow[0], currentHigh[0], voltageLow[0], voltageHigh[0]);
        if (battery)
            result.batteryDischargePowerW = battery->powerW;
    }
    return result;
}
}
