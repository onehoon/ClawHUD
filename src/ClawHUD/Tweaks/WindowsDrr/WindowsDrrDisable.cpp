#include "WindowsDrrDisable.h"

#include "RuntimeLogger.h"
#include "Tweaks/VrrDiagnosticLogger.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace clawhud
{
namespace
{
constexpr UINT32 kReadFlags = QDC_ONLY_ACTIVE_PATHS |
    QDC_VIRTUAL_MODE_AWARE |
    QDC_VIRTUAL_REFRESH_RATE_AWARE;

constexpr UINT32 kSetBaseFlags = SDC_USE_SUPPLIED_DISPLAY_CONFIG |
    SDC_VIRTUAL_MODE_AWARE |
    SDC_VIRTUAL_REFRESH_RATE_AWARE;

constexpr int kMaximumReadAttempts = 3;

void LogDrr(RuntimeLogLevel level, const std::wstring& message) noexcept
{
    const wchar_t* levelName = level == RuntimeLogLevel::Error ? L"ERROR" :
        level == RuntimeLogLevel::Warn ? L"WARN" : L"INFO";
    VrrDiagnosticLogger::WriteWide("DRR",
        std::wstring(L"[") + levelName + L"] " + message);
}

struct DisplayConfigSnapshot
{
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
};

struct SnapshotReadResult
{
    bool succeeded{};
    DrrDisableStatus failureStatus{ DrrDisableStatus::Unavailable };
    LONG error{};
    DisplayConfigSnapshot snapshot;
};

class Win32DisplayConfigApi final : public IWindowsDrrDisplayConfigApi
{
public:
    LONG GetBufferSizes(UINT32 flags, UINT32* pathCount, UINT32* modeCount) override
    {
        return ::GetDisplayConfigBufferSizes(flags, pathCount, modeCount);
    }

    LONG Query(UINT32 flags, UINT32* pathCount,
        DISPLAYCONFIG_PATH_INFO* paths, UINT32* modeCount,
        DISPLAYCONFIG_MODE_INFO* modes) override
    {
        return ::QueryDisplayConfig(flags, pathCount, paths, modeCount, modes, nullptr);
    }

    LONG Set(UINT32 pathCount, DISPLAYCONFIG_PATH_INFO* paths,
        UINT32 modeCount, DISPLAYCONFIG_MODE_INFO* modes, UINT32 flags) override
    {
        return ::SetDisplayConfig(pathCount, paths, modeCount, modes, flags);
    }
};

bool IsUnsupportedError(LONG error) noexcept
{
    return error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED;
}

DrrDisableStatus QueryFailureStatus(LONG error) noexcept
{
    return IsUnsupportedError(error)
        ? DrrDisableStatus::Unsupported
        : DrrDisableStatus::Unavailable;
}

SnapshotReadResult ReadActiveConfig(IWindowsDrrDisplayConfigApi& api)
{
    for (int attempt = 0; attempt < kMaximumReadAttempts; ++attempt)
    {
        UINT32 pathCapacity{};
        UINT32 modeCapacity{};
        const LONG sizeStatus = api.GetBufferSizes(kReadFlags, &pathCapacity, &modeCapacity);
        if (sizeStatus == ERROR_INSUFFICIENT_BUFFER)
            continue;
        if (sizeStatus != ERROR_SUCCESS)
            return { false, QueryFailureStatus(sizeStatus), sizeStatus, {} };
        if (pathCapacity == 0)
            return { false, DrrDisableStatus::Unavailable, ERROR_NOT_FOUND, {} };

        DisplayConfigSnapshot snapshot;
        snapshot.paths.resize(pathCapacity);
        snapshot.modes.resize(modeCapacity);
        UINT32 pathCount = pathCapacity;
        UINT32 modeCount = modeCapacity;
        const LONG queryStatus = api.Query(kReadFlags, &pathCount,
            snapshot.paths.data(), &modeCount, snapshot.modes.data());
        if (queryStatus == ERROR_INSUFFICIENT_BUFFER)
            continue;
        if (queryStatus != ERROR_SUCCESS)
            return { false, QueryFailureStatus(queryStatus), queryStatus, {} };
        if (pathCount > pathCapacity || modeCount > modeCapacity)
            return { false, DrrDisableStatus::Unavailable, ERROR_INVALID_DATA, {} };

        snapshot.paths.resize(pathCount);
        snapshot.modes.resize(modeCount);
        return { true, DrrDisableStatus::Unavailable, ERROR_SUCCESS, std::move(snapshot) };
    }

    return { false, DrrDisableStatus::Unavailable, ERROR_INSUFFICIENT_BUFFER, {} };
}

bool SameLuid(const LUID& left, const LUID& right) noexcept
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

bool SameRational(const DISPLAYCONFIG_RATIONAL& left,
    const DISPLAYCONFIG_RATIONAL& right) noexcept
{
    return left.Numerator == right.Numerator && left.Denominator == right.Denominator;
}

bool SameSize(const DISPLAYCONFIG_2DREGION& left,
    const DISPLAYCONFIG_2DREGION& right) noexcept
{
    return left.cx == right.cx && left.cy == right.cy;
}

bool SamePoint(const POINTL& left, const POINTL& right) noexcept
{
    return left.x == right.x && left.y == right.y;
}

bool SameRect(const RECTL& left, const RECTL& right) noexcept
{
    return left.left == right.left && left.top == right.top &&
        left.right == right.right && left.bottom == right.bottom;
}

bool SameSourceInfo(const DISPLAYCONFIG_PATH_SOURCE_INFO& left,
    const DISPLAYCONFIG_PATH_SOURCE_INFO& right) noexcept
{
    return SameLuid(left.adapterId, right.adapterId) &&
        left.id == right.id && left.modeInfoIdx == right.modeInfoIdx &&
        left.statusFlags == right.statusFlags;
}

bool SameTargetInfo(const DISPLAYCONFIG_PATH_TARGET_INFO& left,
    const DISPLAYCONFIG_PATH_TARGET_INFO& right) noexcept
{
    return SameLuid(left.adapterId, right.adapterId) &&
        left.id == right.id && left.modeInfoIdx == right.modeInfoIdx &&
        left.outputTechnology == right.outputTechnology &&
        left.rotation == right.rotation && left.scaling == right.scaling &&
        // The virtual refresh may change as the expected result of clearing BOOST.
        left.scanLineOrdering == right.scanLineOrdering &&
        left.targetAvailable == right.targetAvailable &&
        left.statusFlags == right.statusFlags;
}

bool SamePathExceptBoost(const DISPLAYCONFIG_PATH_INFO& left,
    const DISPLAYCONFIG_PATH_INFO& right) noexcept
{
    constexpr UINT32 boostFlag = DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE;
    return SameSourceInfo(left.sourceInfo, right.sourceInfo) &&
        SameTargetInfo(left.targetInfo, right.targetInfo) &&
        (left.flags & ~boostFlag) == (right.flags & ~boostFlag);
}

bool SameMode(const DISPLAYCONFIG_MODE_INFO& left,
    const DISPLAYCONFIG_MODE_INFO& right) noexcept
{
    if (left.infoType != right.infoType || left.id != right.id ||
        !SameLuid(left.adapterId, right.adapterId))
        return false;

    switch (left.infoType)
    {
    case DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE:
        return left.sourceMode.width == right.sourceMode.width &&
            left.sourceMode.height == right.sourceMode.height &&
            left.sourceMode.pixelFormat == right.sourceMode.pixelFormat &&
            SamePoint(left.sourceMode.position, right.sourceMode.position);

    case DISPLAYCONFIG_MODE_INFO_TYPE_TARGET:
    {
        const auto& leftSignal = left.targetMode.targetVideoSignalInfo;
        const auto& rightSignal = right.targetMode.targetVideoSignalInfo;
        return leftSignal.pixelRate == rightSignal.pixelRate &&
            SameRational(leftSignal.hSyncFreq, rightSignal.hSyncFreq) &&
            SameRational(leftSignal.vSyncFreq, rightSignal.vSyncFreq) &&
            SameSize(leftSignal.activeSize, rightSignal.activeSize) &&
            SameSize(leftSignal.totalSize, rightSignal.totalSize) &&
            leftSignal.videoStandard == rightSignal.videoStandard &&
            leftSignal.scanLineOrdering == rightSignal.scanLineOrdering;
    }

    case DISPLAYCONFIG_MODE_INFO_TYPE_DESKTOP_IMAGE:
        return SamePoint(left.desktopImageInfo.PathSourceSize,
                right.desktopImageInfo.PathSourceSize) &&
            SameRect(left.desktopImageInfo.DesktopImageRegion,
                right.desktopImageInfo.DesktopImageRegion) &&
            SameRect(left.desktopImageInfo.DesktopImageClip,
                right.desktopImageInfo.DesktopImageClip);

    default:
        return false;
    }
}

bool SameConfiguration(const DisplayConfigSnapshot& before,
    const DisplayConfigSnapshot& after) noexcept
{
    if (before.paths.size() != after.paths.size() ||
        before.modes.size() != after.modes.size())
        return false;

    for (std::size_t index = 0; index < before.paths.size(); ++index)
        if (!SamePathExceptBoost(before.paths[index], after.paths[index]))
            return false;

    for (std::size_t index = 0; index < before.modes.size(); ++index)
        if (!SameMode(before.modes[index], after.modes[index]))
            return false;

    return true;
}

bool SamePathIdentity(const DISPLAYCONFIG_PATH_INFO& left,
    const DISPLAYCONFIG_PATH_INFO& right) noexcept
{
    return SameLuid(left.sourceInfo.adapterId, right.sourceInfo.adapterId) &&
        left.sourceInfo.id == right.sourceInfo.id &&
        SameLuid(left.targetInfo.adapterId, right.targetInfo.adapterId) &&
        left.targetInfo.id == right.targetInfo.id;
}

bool VerifyBoostDisabledForOriginallyBoostedPaths(
    const DisplayConfigSnapshot& before,
    const DisplayConfigSnapshot& after) noexcept
{
    if (!SameConfiguration(before, after))
        return false;

    for (const auto& original : before.paths)
    {
        const DISPLAYCONFIG_PATH_INFO* match{};
        unsigned matchCount{};
        for (const auto& current : after.paths)
        {
            if (!SamePathIdentity(original, current))
                continue;
            match = &current;
            ++matchCount;
        }

        if (matchCount != 1 || !match)
            return false;

        const bool originallyActive =
            (original.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;
        const bool stillActive =
            (match->flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;
        const bool boostStillEnabled =
            (match->flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) != 0;
        if (originallyActive != stillActive ||
            (originallyActive && boostStillEnabled) ||
            (!originallyActive && boostStillEnabled !=
                ((original.flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) != 0)))
            return false;
    }

    return true;
}

std::wstring DeviceNameForPath(const DISPLAYCONFIG_PATH_INFO& path)
{
    DISPLAYCONFIG_TARGET_DEVICE_NAME name{};
    name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    name.header.size = sizeof(name);
    name.header.adapterId = path.targetInfo.adapterId;
    name.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS &&
        name.monitorFriendlyDeviceName[0] != L'\0')
        return name.monitorFriendlyDeviceName;

    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS)
        return source.viewGdiDeviceName;
    return L"unknown";
}

std::wstring DevicePathForPath(const DISPLAYCONFIG_PATH_INFO& path)
{
    DISPLAYCONFIG_TARGET_DEVICE_NAME name{};
    name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    name.header.size = sizeof(name);
    name.header.adapterId = path.targetInfo.adapterId;
    name.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS &&
        name.monitorDevicePath[0] != L'\0')
        return name.monitorDevicePath;
    return L"unknown";
}

std::wstring FormatRational(const DISPLAYCONFIG_RATIONAL& value)
{
    if (value.Denominator == 0)
        return L"unknown";

    std::wostringstream output;
    output << value.Numerator << L'/' << value.Denominator;
    return output.str();
}

void LogSnapshot(const wchar_t* stage, const DisplayConfigSnapshot& snapshot)
{
    const unsigned activeCount = static_cast<unsigned>(std::count_if(
        snapshot.paths.begin(), snapshot.paths.end(), [](const auto& path)
        {
            return (path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;
        }));
    const unsigned boostCount = static_cast<unsigned>(std::count_if(
        snapshot.paths.begin(), snapshot.paths.end(), [](const auto& path)
        {
            return (path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0 &&
                (path.flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) != 0;
        }));

    LogDrr(RuntimeLogLevel::Info,
        std::wstring(L"Windows DRR ") + stage + L" activePathCount=" +
        std::to_wstring(activeCount) + L" drrOnCount=" + std::to_wstring(boostCount));

    for (std::size_t index = 0; index < snapshot.paths.size(); ++index)
    {
        const auto& path = snapshot.paths[index];
        if ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) == 0)
            continue;

        std::wstring physicalRefresh = L"unknown";
        const UINT32 targetModeIndex = path.targetInfo.targetModeInfoIdx;
        if (targetModeIndex < snapshot.modes.size() &&
            snapshot.modes[targetModeIndex].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
            physicalRefresh = FormatRational(
                snapshot.modes[targetModeIndex].targetMode.targetVideoSignalInfo.vSyncFreq);

        LogDrr(RuntimeLogLevel::Info,
            std::wstring(L"Windows DRR ") + stage + L" pathIndex=" + std::to_wstring(index) +
            L" monitor=" + DeviceNameForPath(path) +
            L" monitorDevicePath=" + DevicePathForPath(path) +
            L" sourceAdapter=" + std::to_wstring(path.sourceInfo.adapterId.HighPart) + L":" +
                std::to_wstring(path.sourceInfo.adapterId.LowPart) +
            L" sourceId=" + std::to_wstring(path.sourceInfo.id) +
            L" targetAdapter=" + std::to_wstring(path.targetInfo.adapterId.HighPart) + L":" +
                std::to_wstring(path.targetInfo.adapterId.LowPart) +
            L" targetId=" + std::to_wstring(path.targetInfo.id) +
            L" boost=" + std::to_wstring(
                (path.flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) != 0 ? 1 : 0) +
            L" virtualRefresh=" + FormatRational(path.targetInfo.refreshRate) +
            L" physicalRefresh=" + physicalRefresh);
    }
}

DrrDisableResult ReadFailureResult(const SnapshotReadResult& read,
    unsigned activePathCount = 0, unsigned changedPathCount = 0) noexcept
{
    return { read.failureStatus, activePathCount, changedPathCount, read.error };
}
}

const wchar_t* DrrDisableStatusName(DrrDisableStatus status) noexcept
{
    switch (status)
    {
    case DrrDisableStatus::AlreadyOff: return L"AlreadyOff";
    case DrrDisableStatus::Applied: return L"Applied";
    case DrrDisableStatus::Unsupported: return L"Unsupported";
    case DrrDisableStatus::ValidateFailed: return L"ValidateFailed";
    case DrrDisableStatus::ApplyFailed: return L"ApplyFailed";
    case DrrDisableStatus::VerifyFailed: return L"VerifyFailed";
    default: return L"Unavailable";
    }
}

DrrDisableResult DisableWindowsDrrForActivePaths()
{
    Win32DisplayConfigApi api;
    return DisableWindowsDrrForActivePaths(api);
}

DrrDisableResult DisableWindowsDrrForActivePaths(IWindowsDrrDisplayConfigApi& api)
{
    SnapshotReadResult read;
    try
    {
        read = ReadActiveConfig(api);
    }
    catch (...)
    {
        LogDrr(RuntimeLogLevel::Error,
            L"Windows DRR active-path query failed with an unexpected exception");
        return { DrrDisableStatus::Unavailable, 0, 0, ERROR_GEN_FAILURE };
    }

    if (!read.succeeded)
    {
        LogDrr(read.failureStatus == DrrDisableStatus::Unsupported
                ? RuntimeLogLevel::Info : RuntimeLogLevel::Warn,
            std::wstring(L"Windows DRR query status=") +
                DrrDisableStatusName(read.failureStatus) + L" win32=" +
                std::to_wstring(read.error));
        return ReadFailureResult(read);
    }

    auto original = std::move(read.snapshot);
    const unsigned activePathCount = static_cast<unsigned>(std::count_if(
        original.paths.begin(), original.paths.end(), [](const auto& path)
        {
            return (path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;
        }));
    if (activePathCount == 0)
    {
        LogDrr(RuntimeLogLevel::Warn,
            L"Windows DRR query returned no active display paths");
        return { DrrDisableStatus::Unavailable, 0, 0, ERROR_NOT_FOUND };
    }

    LogSnapshot(L"before", original);
    auto requested = original;
    unsigned changedPathCount{};
    for (auto& path : requested.paths)
    {
        if ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) == 0 ||
            (path.flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) == 0)
            continue;
        path.flags &= ~static_cast<UINT32>(DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE);
        ++changedPathCount;
    }

    if (changedPathCount == 0)
    {
        LogDrr(RuntimeLogLevel::Info,
            L"Windows DRR status=AlreadyOff; SetDisplayConfig was not called");
        return { DrrDisableStatus::AlreadyOff, activePathCount, 0, ERROR_SUCCESS };
    }

    const UINT32 pathCount = static_cast<UINT32>(requested.paths.size());
    const UINT32 modeCount = static_cast<UINT32>(requested.modes.size());
    const LONG validateStatus = api.Set(pathCount, requested.paths.data(),
        modeCount, requested.modes.data(), kSetBaseFlags | SDC_VALIDATE);
    LogDrr(validateStatus == ERROR_SUCCESS
            ? RuntimeLogLevel::Info : RuntimeLogLevel::Warn,
        std::wstring(L"Windows DRR SetDisplayConfig validate win32=") +
            std::to_wstring(validateStatus));
    if (validateStatus != ERROR_SUCCESS)
        return { DrrDisableStatus::ValidateFailed,
            activePathCount, changedPathCount, validateStatus };

    const LONG applyStatus = api.Set(pathCount, requested.paths.data(),
        modeCount, requested.modes.data(),
        kSetBaseFlags | SDC_APPLY | SDC_SAVE_TO_DATABASE);
    LogDrr(applyStatus == ERROR_SUCCESS
            ? RuntimeLogLevel::Info : RuntimeLogLevel::Warn,
        std::wstring(L"Windows DRR SetDisplayConfig apply win32=") +
            std::to_wstring(applyStatus));
    if (applyStatus != ERROR_SUCCESS)
        return { DrrDisableStatus::ApplyFailed,
            activePathCount, changedPathCount, applyStatus };

    SnapshotReadResult verification;
    try
    {
        verification = ReadActiveConfig(api);
    }
    catch (...)
    {
        LogDrr(RuntimeLogLevel::Error,
            L"Windows DRR verification query failed with an unexpected exception");
        return { DrrDisableStatus::VerifyFailed, activePathCount,
            changedPathCount, ERROR_GEN_FAILURE };
    }

    if (!verification.succeeded)
    {
        LogDrr(RuntimeLogLevel::Error,
            std::wstring(L"Windows DRR verification query failed win32=") +
                std::to_wstring(verification.error));
        return { DrrDisableStatus::VerifyFailed, activePathCount,
            changedPathCount, verification.error };
    }

    LogSnapshot(L"after", verification.snapshot);
    if (!VerifyBoostDisabledForOriginallyBoostedPaths(original, verification.snapshot))
    {
        LogDrr(RuntimeLogLevel::Error,
            L"Windows DRR apply could not be verified or changed other display configuration");
        return { DrrDisableStatus::VerifyFailed, activePathCount,
            changedPathCount, ERROR_INVALID_DATA };
    }

    LogDrr(RuntimeLogLevel::Info,
        L"Windows DRR status=Applied changedPathCount=" +
            std::to_wstring(changedPathCount) + L" remainingDrrOnCount=0");
    return { DrrDisableStatus::Applied, activePathCount,
        changedPathCount, ERROR_SUCCESS };
}
}
