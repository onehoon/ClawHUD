#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clawhud
{
struct IntelVrrActiveDisplayPath
{
    std::wstring sourceDeviceName;
    LUID sourceAdapterLuid{};
    UINT32 sourceId{};
    LUID targetAdapterLuid{};
    UINT32 targetId{};
    UINT32 refreshRateNumerator{};
    UINT32 refreshRateDenominator{};
};

struct IntelVrrDisplayPath
{
    std::wstring monitorDeviceName;
    RECT monitorRect{};
    LUID sourceAdapterLuid{};
    UINT32 sourceId{};
    LUID targetAdapterLuid{};
    UINT32 targetId{};
    UINT32 refreshRateNumerator{};
    UINT32 refreshRateDenominator{};
    std::uint32_t activePathCount{};
    double nominalRefreshHz{};
    bool usedModeRefreshFallback{};
};

struct IntelVrrWindowsTarget
{
    std::wstring sourceDeviceName;
    std::wstring monitorDevicePath;
    std::wstring friendlyName;
    LUID adapterLuid{};
    UINT32 targetId{};
    UINT32 sourceId{};
    bool available{};
};

struct IntelVrrWindowsTargets
{
    std::vector<IntelVrrWindowsTarget> targets;
    bool enumerationComplete{};
    LONG error{};
};

// Enumerates every active Windows target with its exact monitor device path.
IntelVrrWindowsTargets EnumerateIntelVrrWindowsTargets() noexcept;

enum class IntelDisplayPathProbeStatus
{
    Matched,
    NotFound,
    Ambiguous,
    Unavailable,
};

struct IntelDisplayPathProbeResult
{
    IntelDisplayPathProbeStatus status{ IntelDisplayPathProbeStatus::Unavailable };
    std::optional<IntelVrrDisplayPath> path;
};

IntelDisplayPathProbeResult ResolveIntelVrrDisplayPath(std::wstring_view monitorDeviceName,
    const RECT& monitorRect, std::span<const IntelVrrActiveDisplayPath> activePaths,
    bool enumerationComplete = true,
    std::optional<double> fallbackNominalRefreshHz = std::nullopt);

class IntelDisplayPathProbe
{
public:
    IntelDisplayPathProbeResult Probe(HMONITOR monitor) const noexcept;
};
}
