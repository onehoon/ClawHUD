#include "WindowsMemoryTelemetry.h"

#include <windows.h>

namespace clawhud
{
std::optional<std::uint64_t> UsedPhysicalMemory(
    std::uint64_t totalBytes, std::uint64_t availableBytes) noexcept
{
    if (availableBytes > totalBytes)
        return std::nullopt;
    return totalBytes - availableBytes;
}

std::optional<std::uint64_t> ReadSystemMemoryUsedBytes() noexcept
{
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory))
        return std::nullopt;
    return UsedPhysicalMemory(memory.ullTotalPhys, memory.ullAvailPhys);
}
}
