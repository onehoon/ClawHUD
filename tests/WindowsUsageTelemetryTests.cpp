#include "WindowsUsageTelemetry.h"

#include <cmath>
#include <cstdint>
#include <iostream>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
bool Near(double actual, double expected)
{
    return std::abs(actual - expected) < 0.001;
}
}

int main()
{
    bool ok = true;
    ok &= Check(UsedPhysicalMemory(32ull * 1024 * 1024 * 1024,
            12ull * 1024 * 1024 * 1024).value() ==
        20ull * 1024 * 1024 * 1024, "physical memory usage calculation");
    ok &= Check(!UsedPhysicalMemory(12, 32),
        "invalid physical memory availability is omitted");
    ok &= Check(NormalizeUsagePercent(33.0).value() == 33.0, "valid CPU usage");
    ok &= Check(NormalizeUsagePercent(0.0).value() == 0.0, "valid zero usage");
    ok &= Check(NormalizeUsagePercent(125.0).value() == 100.0,
        "turbo CPU usage capped");
    ok &= Check(!NormalizeUsagePercent(-1.0) && !NormalizeUsagePercent(NAN),
        "invalid CPU usage omitted");
    const LUID intelLuid{0x12345678, static_cast<LONG>(0x9ABCDEF0)};
    const auto paddedLuid = ParseGpuMemoryInstanceLuid(
        L"luid_0x00000000_0x00123456_phys_0");
    ok &= Check(paddedLuid && paddedLuid->HighPart == 0 &&
        paddedLuid->LowPart == 0x00123456,
        "zero-padded LUID parses to exact values");
    const LUID compactLuid{0x00123456, 0};
    ok &= Check(IsIntelGpuMemoryCounterInstance(
        L"luid_0x0_0x123456_phys_0", compactLuid),
        "compact LUID matches");
    ok &= Check(IsIntelGpuMemoryCounterInstance(
        L"luid_0x00000000_0x123456_phys_0", compactLuid),
        "mixed LUID padding matches");
    ok &= Check(IsIntelGpuMemoryCounterInstance(
        L"luid_0x9abcdef0_0x12345678_phys_0", intelLuid),
        "matching Intel adapter LUID counter");
    const LUID sharedPrefixTarget{0x00013245, 0};
    ok &= Check(IsIntelGpuMemoryCounterInstance(
        L"luid_0x00000000_0x00013245_phys_0", sharedPrefixTarget),
        "matching full LUID with shared leading digits");
    ok &= Check(!IsIntelGpuMemoryCounterInstance(
        L"luid_0x00000000_0x0001368a_phys_0", sharedPrefixTarget),
        "different full LUID with shared leading digits ignored");
    ok &= Check(!IsIntelGpuMemoryCounterInstance(
        L"luid_0x9abcdef0_0x12345678_other_0", intelLuid),
        "non-physical LUID suffix is rejected");
    ok &= Check(!IsIntelGpuMemoryCounterInstance(
        L"luid_0x0_0x1234567_phys_0", compactLuid),
        "LUID prefix collision is rejected");
    ok &= Check(!IsIntelGpuMemoryCounterInstance(L"foo", compactLuid) &&
        !IsIntelGpuMemoryCounterInstance(L"luid_xxx", compactLuid) &&
        !IsIntelGpuMemoryCounterInstance(L"luid_0xZZ_0x1234_phys_0", compactLuid) &&
        !IsIntelGpuMemoryCounterInstance(L"luid_0x0_0x1234", compactLuid) &&
        !IsIntelGpuMemoryCounterInstance(L"luid_0x100000000_0x0_phys_0", compactLuid),
        "malformed LUID instances are rejected");
    ok &= Check(CombineGpuMemoryBytes(2u, 3u).value() == 5u,
        "dedicated and shared memory are summed");
    ok &= Check(CombineGpuMemoryBytes(0u, 0u).value() == 0u,
        "zero memory remains available");
    ok &= Check(!CombineGpuMemoryBytes(4u, std::nullopt) &&
        !CombineGpuMemoryBytes(std::nullopt, 5u),
        "one invalid memory counter makes VRAM unavailable");
    ok &= Check(!CombineGpuMemoryBytes(std::nullopt, std::nullopt),
        "missing Intel memory counters are unavailable");
    WindowsUsageTelemetry previous{};
    previous.cpuUsagePercent = 25.0;
    previous.systemMemoryUsedBytes = 10;
    previous.intelGpuMemoryUsedBytes = 20;
    const auto retained = MergeWindowsUsageTelemetry(previous,
        std::nullopt);
    ok &= Check(retained && retained->cpuUsagePercent == 25.0 &&
        retained->systemMemoryUsedBytes == 10 &&
        retained->intelGpuMemoryUsedBytes == 20,
        "failed usage sample retains last-known-good telemetry");
    WindowsUsageTelemetry partial{};
    partial.systemMemoryUsedBytes = 11;
    const auto partialUpdate = MergeWindowsUsageTelemetry(retained, partial);
    ok &= Check(partialUpdate && partialUpdate->cpuUsagePercent == 25.0 &&
        partialUpdate->systemMemoryUsedBytes == 11 &&
        partialUpdate->intelGpuMemoryUsedBytes == 20,
        "partial usage sample preserves missing metrics");
    WindowsUsageTelemetry firstSample{};
    firstSample.systemMemoryUsedBytes = 30;
    firstSample.intelGpuMemoryUsedBytes = 40;
    const auto priming = MergeWindowsUsageTelemetry(std::nullopt, firstSample);
    ok &= Check(priming && !priming->cpuUsagePercent &&
        priming->systemMemoryUsedBytes == 30 &&
        priming->intelGpuMemoryUsedBytes == 40,
        "usage priming keeps RAM and VRAM without faking CPU");
    WindowsUsageTelemetry updated{};
    updated.cpuUsagePercent = 35.0;
    updated.systemMemoryUsedBytes = 11;
    updated.intelGpuMemoryUsedBytes = 21;
    const auto recovered = MergeWindowsUsageTelemetry(retained, updated);
    ok &= Check(recovered && recovered->cpuUsagePercent == 35.0 &&
        recovered->systemMemoryUsedBytes == 11 &&
        recovered->intelGpuMemoryUsedBytes == 21,
        "valid usage sample updates retained telemetry");
    ok &= Check(!ShouldInvalidateWindowsUsageTelemetry(2, 3) &&
        ShouldInvalidateWindowsUsageTelemetry(3, 3) &&
        !ShouldInvalidateWindowsUsageTelemetry(3, 0),
        "usage telemetry invalidation is bounded");
    ok &= Check(ShouldRetryIntelGpuMemoryCounters(true, true, 0) &&
        ShouldRetryIntelGpuMemoryCounters(false, true, 2) &&
        !ShouldRetryIntelGpuMemoryCounters(true, false, 3) &&
        !ShouldRetryIntelGpuMemoryCounters(false, false, 0),
        "Intel memory binding retry is bounded and only targets incomplete binding");
    return ok ? 0 : 1;
}
