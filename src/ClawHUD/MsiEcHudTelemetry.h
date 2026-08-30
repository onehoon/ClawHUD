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

struct MsiEcHudTelemetry
{
    std::optional<int> cpuTempC;
    std::optional<int> fan1Rpm;
    std::optional<int> fan2Rpm;
    std::optional<int> cpuPackagePowerW;
};

std::optional<int> DecodeCpuTempC(std::span<const std::uint8_t> payload);
std::optional<int> DecodeFanRpm(std::uint8_t first, std::uint8_t second);
std::optional<FanTelemetry> DecodeFanTelemetry(std::span<const std::uint8_t> payload);
std::optional<int> DecodeCpuPackagePowerW(
    std::span<const std::uint8_t> payload);
}
