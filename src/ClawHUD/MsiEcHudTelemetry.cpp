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

std::optional<int> SelectHudFanRpm(
    std::optional<int> fan1Rpm, std::optional<int> fan2Rpm)
{
    if (fan1Rpm && fan2Rpm)
        return (*fan1Rpm + *fan2Rpm) / 2;
    if (fan1Rpm)
        return fan1Rpm;
    if (fan2Rpm)
        return fan2Rpm;
    return std::nullopt;
}

std::optional<int> DecodeCpuPackagePowerW(
    std::span<const std::uint8_t> payload)
{
    if (payload.empty())
        return std::nullopt;
    return static_cast<int>(payload[0]);
}
}
