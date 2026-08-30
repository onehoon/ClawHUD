#include "MsiEcHudTelemetry.h"

#include <cmath>

namespace clawhud
{
std::optional<int> DecodeCpuTempC(std::span<const std::uint8_t> payload)
{
    if (payload.empty() || payload[0] == 0)
        return std::nullopt;
    return static_cast<int>(payload[0]);
}

std::optional<int> DecodeFanRpm(std::uint8_t first, std::uint8_t second)
{
    const int delta = static_cast<int>(first) - static_cast<int>(second);
    if (delta == 0)
        return 0;
    return static_cast<int>(std::abs(480000.0 / static_cast<double>(delta)));
}

std::optional<FanTelemetry> DecodeFanTelemetry(std::span<const std::uint8_t> payload)
{
    if (payload.size() < 4)
        return std::nullopt;
    return FanTelemetry{
        DecodeFanRpm(payload[0], payload[1]),
        DecodeFanRpm(payload[2], payload[3])};
}

std::optional<int> DecodeCpuPackagePowerW(
    std::span<const std::uint8_t> payload)
{
    if (payload.empty())
        return std::nullopt;
    return static_cast<int>(payload[0]);
}

std::int16_t DecodeBatteryCurrentMa(std::uint8_t low, std::uint8_t high) noexcept
{
    const auto raw = static_cast<std::uint16_t>(low) |
        (static_cast<std::uint16_t>(high) << 8);
    return static_cast<std::int16_t>(raw);
}

std::uint16_t DecodeBatteryVoltageMv(std::uint8_t low, std::uint8_t high) noexcept
{
    return static_cast<std::uint16_t>(low) |
        (static_cast<std::uint16_t>(high) << 8);
}

std::optional<MsiEcBatteryPowerSample> DecodeBatteryPower(
    std::uint8_t currentLow, std::uint8_t currentHigh,
    std::uint8_t voltageLow, std::uint8_t voltageHigh) noexcept
{
    const auto currentMa = DecodeBatteryCurrentMa(currentLow, currentHigh);
    const auto voltageMv = DecodeBatteryVoltageMv(voltageLow, voltageHigh);
    if (currentMa >= 0 || voltageMv == 0)
        return std::nullopt;

    const double powerW = (-static_cast<double>(currentMa) *
        static_cast<double>(voltageMv)) / 1'000'000.0;
    if (!(powerW > 0.0) || !std::isfinite(powerW))
        return std::nullopt;
    return MsiEcBatteryPowerSample{ currentMa, voltageMv, powerW };
}
}
