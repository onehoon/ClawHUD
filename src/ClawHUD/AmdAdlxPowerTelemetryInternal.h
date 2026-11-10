#pragma once

#include "AmdAdlxPowerTelemetry.h"

#include <cstdint>
#include <optional>

namespace clawhud::detail
{
struct AdlxPowerApi
{
    void* context{};
    AdlxPowerStatus (*initialize)(void*, void**) noexcept{};
    AdlxPowerStatus (*getGpuList)(void*, void*, void**) noexcept{};
    AdlxPowerStatus (*getGpuListSize)(void*, void*, std::uint32_t*) noexcept{};
    AdlxPowerStatus (*getGpuListBegin)(void*, void*, std::uint32_t*) noexcept{};
    AdlxPowerStatus (*getGpuAt)(void*, void*, std::uint32_t, void**) noexcept{};
    AdlxPowerStatus (*getPerformanceServices)(void*, void*, void**) noexcept{};
    AdlxPowerStatus (*getSupportedGpuMetrics)(void*, void*, void*, void**) noexcept{};
    AdlxPowerStatus (*isGpuPowerSupported)(void*, void*, bool*) noexcept{};
    AdlxPowerStatus (*getCurrentGpuMetrics)(void*, void*, void*, void**) noexcept{};
    AdlxPowerStatus (*readGpuPower)(void*, void*, double*) noexcept{};
    bool (*release)(void*, void*) noexcept{};
    void (*shutdown)(void*) noexcept{};
};

inline constexpr std::uint64_t kAdlxMinimumApiVersion = 1ULL << 48;
inline constexpr std::uint64_t kAdlxKnownHeaderVersion = (2ULL << 48) | 125ULL;

std::optional<std::uint64_t> SelectAdlxInitializationVersion(std::uint64_t runtimeVersion) noexcept;
}
