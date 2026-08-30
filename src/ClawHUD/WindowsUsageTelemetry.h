#pragma once

#include <windows.h>
#include <pdh.h>

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace clawhud
{
struct WindowsUsageTelemetry
{
    std::optional<double> cpuUsagePercent;
    std::optional<std::uint64_t> systemMemoryUsedBytes;
    std::optional<std::uint64_t> intelGpuMemoryUsedBytes;
};

std::optional<std::uint64_t> UsedPhysicalMemory(
    std::uint64_t totalBytes, std::uint64_t availableBytes) noexcept;
std::optional<std::uint64_t> ReadSystemMemoryUsedBytes() noexcept;
std::optional<double> NormalizeUsagePercent(double value) noexcept;
std::optional<LUID> ParseGpuMemoryInstanceLuid(std::wstring_view instance);
bool IsIntelGpuMemoryCounterInstance(std::wstring_view instance,
    const LUID& adapterLuid);
std::optional<std::uint64_t> CombineGpuMemoryBytes(
    std::optional<std::uint64_t> dedicated,
    std::optional<std::uint64_t> shared) noexcept;
std::optional<WindowsUsageTelemetry> MergeWindowsUsageTelemetry(
    const std::optional<WindowsUsageTelemetry>& previous,
    const std::optional<WindowsUsageTelemetry>& sample) noexcept;
bool ShouldInvalidateWindowsUsageTelemetry(
    unsigned consecutiveFailures, unsigned failureThreshold) noexcept;
bool ShouldRetryIntelGpuMemoryCounters(bool dedicatedEmpty,
    bool sharedEmpty, unsigned int attempts) noexcept;
bool NeedsIntelGpuMemoryBinding(bool dedicatedEmpty,
    bool sharedEmpty) noexcept;
bool ShouldReleaseIntelGpuMemoryCounters(
    unsigned consecutiveFailures, unsigned failureThreshold) noexcept;
bool ShouldRearmIntelGpuMemoryCounters(unsigned attempts,
    unsigned cooldownSamples, unsigned maxAttempts,
    unsigned cooldownThreshold) noexcept;

class WindowsUsageSampler
{
public:
    ~WindowsUsageSampler();
    bool Initialize();
    void Reset() noexcept;
    std::optional<WindowsUsageTelemetry> Sample();
    bool Initialized() const noexcept { return query_ != nullptr; }

private:
    static bool IsValidCounter(const PDH_FMT_COUNTERVALUE& value) noexcept;
    bool AddIntelGpuMemoryCounters();
    bool TryBindIntelGpuMemoryCounters();
    void ReleaseIntelGpuMemoryCounters() noexcept;
    std::optional<double> ReadCounter(PDH_HCOUNTER counter,
        bool capAbove100) const;
    std::optional<std::uint64_t> ReadByteCounter(PDH_HCOUNTER counter) const;
    std::optional<std::uint64_t> ReadByteCounters(
        const std::vector<PDH_HCOUNTER>& counters) const;

    PDH_HQUERY query_{};
    PDH_HCOUNTER cpuCounter_{};
    std::vector<PDH_HCOUNTER> intelDedicatedMemoryCounters_;
    std::vector<PDH_HCOUNTER> intelSharedMemoryCounters_;
    bool primed_{};
    bool memoryDiagnosticsLogged_{};
    unsigned int intelMemoryRebindAttempts_{};
    unsigned int intelMemoryFailureCount_{};
    unsigned int intelMemoryRebindCooldownSamples_{};
};
}
