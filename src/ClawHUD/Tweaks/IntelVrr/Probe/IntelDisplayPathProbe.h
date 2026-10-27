#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

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
