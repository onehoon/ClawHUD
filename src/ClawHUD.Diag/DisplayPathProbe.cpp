#include "DisplayPathProbe.h"

#include <cmath>
#include <utility>
#include <vector>

namespace
{
wchar_t FoldAscii(wchar_t value) noexcept
{
    return value >= L'A' && value <= L'Z'
        ? static_cast<wchar_t>(value + (L'a' - L'A')) : value;
}

bool EqualDeviceName(std::wstring_view left, std::wstring_view right) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (FoldAscii(left[i]) != FoldAscii(right[i])) return false;
    return true;
}

std::optional<double> ReadModeRefreshRate(const wchar_t* deviceName) noexcept
{
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsExW(deviceName, ENUM_CURRENT_SETTINGS, &mode, 0) ||
        mode.dmDisplayFrequency <= 1)
        return std::nullopt;
    return static_cast<double>(mode.dmDisplayFrequency);
}

DisplayPathProbeResult Unavailable() noexcept
{
    return { DisplayPathProbeStatus::Unavailable, std::nullopt };
}
}

DisplayPathProbeResult ResolveVrrDisplayPath(std::wstring_view monitorDeviceName,
    const RECT& monitorRect, std::span<const VrrActiveDisplayPath> activePaths,
    bool enumerationComplete, std::optional<double> fallbackNominalRefreshHz)
{
    std::vector<const VrrActiveDisplayPath*> matches;
    for (const auto& path : activePaths)
        if (EqualDeviceName(monitorDeviceName, path.sourceDeviceName)) matches.push_back(&path);

    if (matches.size() > 1)
        return { DisplayPathProbeStatus::Ambiguous, std::nullopt };
    if (!enumerationComplete)
        return Unavailable();
    if (matches.empty())
        return { DisplayPathProbeStatus::NotFound, std::nullopt };

    const auto& matched = *matches.front();
    VrrDisplayPath result;
    result.monitorDeviceName = std::wstring(monitorDeviceName);
    result.monitorRect = monitorRect;
    result.sourceAdapterLuid = matched.sourceAdapterLuid;
    result.sourceId = matched.sourceId;
    result.targetAdapterLuid = matched.targetAdapterLuid;
    result.targetId = matched.targetId;
    result.refreshRateNumerator = matched.refreshRateNumerator;
    result.refreshRateDenominator = matched.refreshRateDenominator;

    if (matched.refreshRateNumerator && matched.refreshRateDenominator)
        result.nominalRefreshHz = static_cast<double>(matched.refreshRateNumerator) /
            static_cast<double>(matched.refreshRateDenominator);
    else if (fallbackNominalRefreshHz && std::isfinite(*fallbackNominalRefreshHz) &&
        *fallbackNominalRefreshHz > 0.0)
    {
        result.nominalRefreshHz = *fallbackNominalRefreshHz;
        result.usedModeRefreshFallback = true;
    }

    return { DisplayPathProbeStatus::Matched, std::move(result) };
}

DisplayPathProbeResult DisplayPathProbe::Probe(HMONITOR monitor) const noexcept
{
    try
    {
        if (!monitor) return Unavailable();
        MONITORINFOEXW monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&monitorInfo)))
            return Unavailable();

        std::vector<VrrActiveDisplayPath> activePaths;
        bool complete = false;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            UINT32 pathCount{};
            UINT32 modeCount{};
            if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
                return Unavailable();

            std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
            const LONG status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount,
                paths.data(), &modeCount, modes.data(), nullptr);
            if (status == ERROR_INSUFFICIENT_BUFFER) continue;
            if (status != ERROR_SUCCESS || pathCount > paths.size() || modeCount > modes.size())
                return Unavailable();

            activePaths.clear();
            complete = true;
            activePaths.reserve(pathCount);
            for (UINT32 i = 0; i < pathCount; ++i)
            {
                const auto& path = paths[i];
                DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
                sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                sourceName.header.size = sizeof(sourceName);
                sourceName.header.adapterId = path.sourceInfo.adapterId;
                sourceName.header.id = path.sourceInfo.id;
                if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS)
                {
                    complete = false;
                    continue;
                }
                activePaths.push_back({ sourceName.viewGdiDeviceName,
                    path.sourceInfo.adapterId, path.sourceInfo.id,
                    path.targetInfo.adapterId, path.targetInfo.id,
                    path.targetInfo.refreshRate.Numerator,
                    path.targetInfo.refreshRate.Denominator });
            }
            break;
        }
        if (!complete) return Unavailable();

        auto result = ResolveVrrDisplayPath(monitorInfo.szDevice, monitorInfo.rcMonitor,
            activePaths, true);
        if (result.status == DisplayPathProbeStatus::Matched && result.path &&
            result.path->nominalRefreshHz <= 0.0)
        {
            if (const auto fallback = ReadModeRefreshRate(monitorInfo.szDevice))
            {
                result.path->nominalRefreshHz = *fallback;
                result.path->usedModeRefreshFallback = true;
            }
        }
        return result;
    }
    catch (...) { return Unavailable(); }
}
