#pragma once

#include <cstdint>
#include <optional>

namespace clawhud
{
std::optional<std::uint64_t> UsedPhysicalMemory(
    std::uint64_t totalBytes, std::uint64_t availableBytes) noexcept;
std::optional<std::uint64_t> ReadSystemMemoryUsedBytes() noexcept;
}
