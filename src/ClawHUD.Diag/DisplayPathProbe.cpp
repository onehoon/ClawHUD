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
    result.activePathCount = static_cast<std::uint32_t>(activePaths.size());

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

VrrWindowsTargets EnumerateVrrWindowsTargets() noexcept
{
    try
    {
        constexpr UINT32 flags = QDC_ONLY_ACTIVE_PATHS;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            UINT32 pathCount{};
            UINT32 modeCount{};
            const LONG sizeResult = GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount);
            if (sizeResult != ERROR_SUCCESS)
                return { {}, false, sizeResult };
            if (pathCount == 0)
                return { {}, true, ERROR_SUCCESS };

            std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
            const LONG queryResult = QueryDisplayConfig(flags, &pathCount,
                paths.data(), &modeCount, modes.data(), nullptr);
            if (queryResult == ERROR_INSUFFICIENT_BUFFER)
                continue;
            if (queryResult != ERROR_SUCCESS || pathCount > paths.size() ||
                modeCount > modes.size())
                return { {}, false, queryResult == ERROR_SUCCESS
                    ? ERROR_INVALID_DATA : queryResult };

            VrrWindowsTargets result;
            result.enumerationComplete = true;
            result.targets.reserve(pathCount);
            for (UINT32 index = 0; index < pathCount; ++index)
            {
                const auto& path = paths[index];
                if ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) == 0)
                    continue;

                DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof(source);
                source.header.adapterId = path.sourceInfo.adapterId;
                source.header.id = path.sourceInfo.id;

                DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
                target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                target.header.size = sizeof(target);
                target.header.adapterId = path.targetInfo.adapterId;
                target.header.id = path.targetInfo.id;

                const LONG sourceResult = DisplayConfigGetDeviceInfo(&source.header);
                const LONG targetResult = DisplayConfigGetDeviceInfo(&target.header);
                if (sourceResult != ERROR_SUCCESS || targetResult != ERROR_SUCCESS ||
                    source.viewGdiDeviceName[0] == L'\0' ||
                    target.monitorDevicePath[0] == L'\0')
                {
                    result.enumerationComplete = false;
                    result.error = sourceResult != ERROR_SUCCESS ? sourceResult :
                        targetResult != ERROR_SUCCESS ? targetResult : ERROR_NOT_FOUND;
                }

                result.targets.push_back({ source.viewGdiDeviceName,
                    target.monitorDevicePath, target.monitorFriendlyDeviceName,
                    path.targetInfo.adapterId, path.targetInfo.id,
                    path.sourceInfo.id, path.targetInfo.targetAvailable != FALSE });
            }
            return result;
        }
        return { {}, false, ERROR_INSUFFICIENT_BUFFER };
    }
    catch (...)
    {
        return { {}, false, ERROR_GEN_FAILURE };
    }
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
