#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

struct VrrActiveDisplayPath
{
    std::wstring sourceDeviceName;
    LUID sourceAdapterLuid{};
    UINT32 sourceId{};
    LUID targetAdapterLuid{};
    UINT32 targetId{};
    UINT32 refreshRateNumerator{};
    UINT32 refreshRateDenominator{};
};

struct VrrDisplayPath
{
    std::wstring monitorDeviceName;
    RECT monitorRect{};
    LUID sourceAdapterLuid{};
    UINT32 sourceId{};
    LUID targetAdapterLuid{};
    UINT32 targetId{};
    UINT32 refreshRateNumerator{};
    UINT32 refreshRateDenominator{};
    double nominalRefreshHz{};
    bool usedModeRefreshFallback{};
};

enum class DisplayPathProbeStatus
{
    Matched,
    NotFound,
    Ambiguous,
    Unavailable,
};

struct DisplayPathProbeResult
{
    DisplayPathProbeStatus status{ DisplayPathProbeStatus::Unavailable };
    std::optional<VrrDisplayPath> path;
};

DisplayPathProbeResult ResolveVrrDisplayPath(std::wstring_view monitorDeviceName,
    const RECT& monitorRect, std::span<const VrrActiveDisplayPath> activePaths,
    bool enumerationComplete = true,
    std::optional<double> fallbackNominalRefreshHz = std::nullopt);

class DisplayPathProbe
{
public:
    DisplayPathProbeResult Probe(HMONITOR monitor) const noexcept;
};
