#pragma once

#include "Tweaks/IntelVrr/Probe/IntelDisplayPathProbe.h"

using VrrActiveDisplayPath = clawhud::IntelVrrActiveDisplayPath;
using VrrDisplayPath = clawhud::IntelVrrDisplayPath;
using VrrWindowsTarget = clawhud::IntelVrrWindowsTarget;
using VrrWindowsTargets = clawhud::IntelVrrWindowsTargets;
using DisplayPathProbeStatus = clawhud::IntelDisplayPathProbeStatus;
using DisplayPathProbeResult = clawhud::IntelDisplayPathProbeResult;
using DisplayPathProbe = clawhud::IntelDisplayPathProbe;

inline DisplayPathProbeResult ResolveVrrDisplayPath(
    std::wstring_view monitorDeviceName, const RECT& monitorRect,
    std::span<const VrrActiveDisplayPath> activePaths,
    bool enumerationComplete = true,
    std::optional<double> fallbackNominalRefreshHz = std::nullopt)
{
    return clawhud::ResolveIntelVrrDisplayPath(monitorDeviceName, monitorRect,
        activePaths, enumerationComplete, fallbackNominalRefreshHz);
}

inline VrrWindowsTargets EnumerateVrrWindowsTargets() noexcept
{
    return clawhud::EnumerateIntelVrrWindowsTargets();
}
