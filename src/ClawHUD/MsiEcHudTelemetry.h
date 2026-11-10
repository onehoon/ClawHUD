#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace clawhud
{
struct FanTelemetry
{
    std::optional<int> fan1Rpm;
    std::optional<int> fan2Rpm;
};

struct MsiEcA8BatteryPowerSample
{
    std::uint16_t rawCurrent{};
    std::uint16_t voltageMv{};
    std::uint16_t dischargeCurrentMa{};
    double powerW{};
};

struct MsiEcHudTelemetry
{
    std::optional<int> cpuTempC;
    std::optional<int> fan1Rpm;
    std::optional<int> fan2Rpm;
    std::optional<int> cpuPackagePowerW;
    std::optional<double> batteryDischargePowerW;
    bool batteryDischargeAttempted{};
    std::optional<MsiEcA8BatteryPowerSample> a8BatterySample;
    bool cpuTemperatureAttempted{};
    bool fanRpmAttempted{};
};

struct MsiEcBatteryPowerSample
{
    std::int16_t currentMa{};
    std::uint16_t voltageMv{};
    double powerW{};
};

std::optional<int> DecodeCpuTempC(std::span<const std::uint8_t> payload);
std::optional<int> DecodeFanRpm(std::uint8_t first, std::uint8_t second);
std::optional<int> DecodeFirstFanRpm(std::span<const std::uint8_t> payload);
std::optional<FanTelemetry> DecodeFanTelemetry(std::span<const std::uint8_t> payload);
std::optional<int> DecodeCpuPackagePowerW(
    std::span<const std::uint8_t> payload);
std::int16_t DecodeBatteryCurrentMa(std::uint8_t low, std::uint8_t high) noexcept;
std::uint16_t DecodeBatteryVoltageMv(std::uint8_t low, std::uint8_t high) noexcept;
std::optional<MsiEcBatteryPowerSample> DecodeBatteryPower(
    std::uint8_t currentLow, std::uint8_t currentHigh,
    std::uint8_t voltageLow, std::uint8_t voltageHigh) noexcept;
std::optional<MsiEcA8BatteryPowerSample> DecodeA8BatteryPower(
    std::uint8_t currentLow, std::uint8_t currentHigh,
    std::uint8_t voltageLow, std::uint8_t voltageHigh) noexcept;
}
