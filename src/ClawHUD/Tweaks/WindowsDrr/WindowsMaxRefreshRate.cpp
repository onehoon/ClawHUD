#include "WindowsMaxRefreshRate.h"

#include "Tweaks/VrrDiagnosticLogger.h"

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace clawhud
{
bool detail::TryGetTargetPhysicalRefresh(const DISPLAYCONFIG_PATH_INFO& path,
    const DISPLAYCONFIG_MODE_INFO* modes, UINT32 modeCount,
    DISPLAYCONFIG_RATIONAL& physicalRefresh) noexcept
{
    if (!modes)
        return false;

    const bool virtualMode =
        (path.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0;
    const UINT32 targetModeIndex = virtualMode
        ? path.targetInfo.targetModeInfoIdx
        : path.targetInfo.modeInfoIdx;
    const UINT32 invalidIndex = virtualMode
        ? DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID
        : DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
    if (targetModeIndex == invalidIndex || targetModeIndex >= modeCount ||
        modes[targetModeIndex].infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
        return false;

    physicalRefresh = modes[targetModeIndex].targetMode.targetVideoSignalInfo.vSyncFreq;
    return true;
}

namespace
{
constexpr UINT32 kReadFlags = QDC_ONLY_ACTIVE_PATHS |
    QDC_VIRTUAL_MODE_AWARE | QDC_VIRTUAL_REFRESH_RATE_AWARE;
constexpr int kMaximumReadAttempts = 3;
constexpr DWORD kMaximumModeCount = 4096;
constexpr long double kRefreshVerificationToleranceHz = 0.5L;

using SourceKey = std::tuple<LONG, ULONG, UINT32>;
using TargetKey = std::tuple<LONG, ULONG, UINT32>;

struct DisplayGroup
{
    SourceKey key{};
    std::wstring gdiName;
    std::wstring monitorName;
    std::vector<const WindowsActiveDisplayPath*> paths;
    bool unsafe{};
    std::wstring unsafeReason;
};

struct ModeCandidate
{
    DEVMODEW mode{};
    unsigned hz{};
};

struct PreparedChange
{
    std::size_t targetIndex{};
    DEVMODEW original{};
    DEVMODEW requested{};
    unsigned requestedHz{};
    bool staged{};
};

class Win32MaxRefreshRateApi final : public IWindowsMaxRefreshRateApi
{
public:
    LONG ReadActiveDisplayPaths(
        std::vector<WindowsActiveDisplayPath>& paths) override
    {
        paths.clear();
        for (int attempt = 0; attempt < kMaximumReadAttempts; ++attempt)
        {
            UINT32 pathCapacity{};
            UINT32 modeCapacity{};
            LONG status = GetDisplayConfigBufferSizes(kReadFlags,
                &pathCapacity, &modeCapacity);
            if (status == ERROR_INSUFFICIENT_BUFFER)
                continue;
            if (status != ERROR_SUCCESS)
                return status;

            std::vector<DISPLAYCONFIG_PATH_INFO> nativePaths(
                std::max<UINT32>(pathCapacity, 1));
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(
                std::max<UINT32>(modeCapacity, 1));
            UINT32 pathCount = pathCapacity;
            UINT32 modeCount = modeCapacity;
            status = QueryDisplayConfig(kReadFlags, &pathCount,
                nativePaths.data(), &modeCount, modes.data(), nullptr);
            if (status == ERROR_INSUFFICIENT_BUFFER)
                continue;
            if (status != ERROR_SUCCESS)
                return status;
            if (pathCount > pathCapacity || modeCount > modeCapacity)
                return ERROR_INVALID_DATA;

            paths.reserve(pathCount);
            const bool remoteSession = GetSystemMetrics(SM_REMOTESESSION) != 0;
            for (UINT32 index = 0; index < pathCount; ++index)
            {
                const auto& path = nativePaths[index];
                if ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) == 0)
                    continue;

                WindowsActiveDisplayPath output{};
                output.sourceAdapter = path.sourceInfo.adapterId;
                output.sourceId = path.sourceInfo.id;
                output.targetAdapter = path.targetInfo.adapterId;
                output.targetId = path.targetInfo.id;
                output.outputTechnology = path.targetInfo.outputTechnology;
                output.flags = path.flags;
                output.virtualRefresh = path.targetInfo.refreshRate;
                output.targetAvailable = path.targetInfo.targetAvailable != FALSE;
                output.remoteSession = remoteSession;

                DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof(source);
                source.header.adapterId = path.sourceInfo.adapterId;
                source.header.id = path.sourceInfo.id;
                if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS &&
                    source.viewGdiDeviceName[0] != L'\0')
                {
                    output.gdiDeviceName = source.viewGdiDeviceName;
                    output.sourceNameValid = true;
                }

                DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
                target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                target.header.size = sizeof(target);
                target.header.adapterId = path.targetInfo.adapterId;
                target.header.id = path.targetInfo.id;
                if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS)
                {
                    output.monitorDevicePath = target.monitorDevicePath;
                    output.monitorName = target.monitorFriendlyDeviceName;
                }

                (void)detail::TryGetTargetPhysicalRefresh(path, modes.data(), modeCount,
                    output.physicalRefresh);

                if (output.sourceNameValid)
                {
                    for (DWORD deviceIndex = 0; deviceIndex < 256; ++deviceIndex)
                    {
                        DISPLAY_DEVICEW display{};
                        display.cb = sizeof(display);
                        if (!EnumDisplayDevicesW(nullptr, deviceIndex, &display, 0))
                            break;
                        if (!SameDeviceName(display.DeviceName, output.gdiDeviceName))
                            continue;
                        output.attachedToDesktop =
                            (display.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
                        output.mirroringDriver =
                            (display.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0;
                        break;
                    }
                }

                DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
                color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
                color.header.size = sizeof(color);
                color.header.adapterId = path.targetInfo.adapterId;
                color.header.id = path.targetInfo.id;
                if (DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS)
                {
                    output.advancedColorKnown = true;
                    output.advancedColorEnabled = color.advancedColorEnabled != 0;
                }
                paths.push_back(std::move(output));
            }
            return ERROR_SUCCESS;
        }
        return ERROR_INSUFFICIENT_BUFFER;
    }

    bool EnumDisplaySettings(const std::wstring& deviceName,
        DWORD modeIndex, DEVMODEW& mode) override
    {
        return EnumDisplaySettingsExW(deviceName.c_str(), modeIndex, &mode, 0) != FALSE;
    }

    LONG ChangeDisplaySettings(const wchar_t* deviceName,
        DEVMODEW* mode, DWORD flags) override
    {
        return ChangeDisplaySettingsExW(deviceName, mode, nullptr, flags, nullptr);
    }

private:
    static bool SameDeviceName(const wchar_t* left, const std::wstring& right) noexcept
    {
        return left && !right.empty() &&
            CompareStringOrdinal(left, -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
    }
};

bool SameLuid(const LUID& left, const LUID& right) noexcept
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

SourceKey MakeSourceKey(const WindowsActiveDisplayPath& path) noexcept
{
    return { path.sourceAdapter.HighPart, path.sourceAdapter.LowPart, path.sourceId };
}

TargetKey MakeTargetKey(const WindowsActiveDisplayPath& path) noexcept
{
    return { path.targetAdapter.HighPart, path.targetAdapter.LowPart, path.targetId };
}

bool SamePathIdentity(const WindowsActiveDisplayPath& left,
    const WindowsActiveDisplayPath& right) noexcept
{
    return SameLuid(left.sourceAdapter, right.sourceAdapter) &&
        left.sourceId == right.sourceId &&
        SameLuid(left.targetAdapter, right.targetAdapter) &&
        left.targetId == right.targetId;
}

bool IsBoostEnabled(const WindowsActiveDisplayPath& path) noexcept
{
    return (path.flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) != 0;
}

std::wstring FoldName(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return value;
}

bool HasFields(const DEVMODEW& mode, DWORD fields) noexcept
{
    return (mode.dmFields & fields) == fields;
}

DEVMODEW EmptyMode() noexcept
{
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    return mode;
}

bool SameLayout(const DEVMODEW& left, const DEVMODEW& right) noexcept
{
    constexpr DWORD required = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_POSITION;
    if (!HasFields(left, required) || !HasFields(right, required) ||
        left.dmPelsWidth != right.dmPelsWidth ||
        left.dmPelsHeight != right.dmPelsHeight ||
        left.dmBitsPerPel != right.dmBitsPerPel ||
        left.dmPosition.x != right.dmPosition.x ||
        left.dmPosition.y != right.dmPosition.y)
        return false;

    if (HasFields(left, DM_DISPLAYORIENTATION) != HasFields(right, DM_DISPLAYORIENTATION) ||
        (HasFields(left, DM_DISPLAYORIENTATION) &&
            left.dmDisplayOrientation != right.dmDisplayOrientation))
        return false;
    if (HasFields(left, DM_DISPLAYFLAGS) != HasFields(right, DM_DISPLAYFLAGS) ||
        (HasFields(left, DM_DISPLAYFLAGS) && left.dmDisplayFlags != right.dmDisplayFlags))
        return false;
    return true;
}

bool SameCurrentMode(const DEVMODEW& left, const DEVMODEW& right) noexcept
{
    return SameLayout(left, right) &&
        HasFields(left, DM_DISPLAYFREQUENCY) &&
        HasFields(right, DM_DISPLAYFREQUENCY) &&
        left.dmDisplayFrequency == right.dmDisplayFrequency;
}

std::wstring SkipReason(const WindowsActiveDisplayPath& path)
{
    if (!path.targetAvailable)
        return L"TargetUnavailable";
    if (!path.sourceNameValid || path.gdiDeviceName.empty())
        return L"MissingGdiSourceName";
    if (path.monitorDevicePath.empty())
        return L"MissingTargetDevicePath";
    if (!path.attachedToDesktop)
        return L"SourceNotAttached";
    if (path.mirroringDriver)
        return L"MirroringDriver";
    if (path.remoteSession)
        return L"RemoteSession";
    if (path.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL ||
        path.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST)
        return L"VirtualOrWirelessTarget";
    return {};
}

std::wstring RationalText(const DISPLAYCONFIG_RATIONAL& value)
{
    if (value.Denominator == 0)
        return L"unknown";
    return std::to_wstring(value.Numerator) + L"/" +
        std::to_wstring(value.Denominator);
}

std::wstring LuidText(const LUID& value)
{
    std::wostringstream output;
    output << L"0x" << std::hex << static_cast<ULONG>(value.HighPart) << L":" <<
        value.LowPart;
    return output.str();
}

bool RationalWithinTolerance(const DISPLAYCONFIG_RATIONAL& value,
    unsigned expectedHz) noexcept
{
    if (value.Denominator == 0)
        return true;
    const long double actual = static_cast<long double>(value.Numerator) /
        static_cast<long double>(value.Denominator);
    const long double difference = actual > expectedHz
        ? actual - expectedHz : expectedHz - actual;
    return difference <= kRefreshVerificationToleranceHz;
}

bool SameRationalWithinTolerance(const DISPLAYCONFIG_RATIONAL& left,
    const DISPLAYCONFIG_RATIONAL& right) noexcept
{
    if (left.Denominator == 0 || right.Denominator == 0)
        return true;
    const long double leftHz = static_cast<long double>(left.Numerator) / left.Denominator;
    const long double rightHz = static_cast<long double>(right.Numerator) / right.Denominator;
    const long double difference = leftHz > rightHz ? leftHz - rightHz : rightHz - leftHz;
    return difference <= kRefreshVerificationToleranceHz;
}

const WindowsActiveDisplayPath* FindPath(
    const std::vector<WindowsActiveDisplayPath>& paths,
    const WindowsActiveDisplayPath& expected)
{
    const WindowsActiveDisplayPath* match{};
    for (const auto& path : paths)
    {
        if (!SamePathIdentity(path, expected))
            continue;
        if (match)
            return nullptr;
        match = &path;
    }
    return match;
}

bool SameTopology(const std::vector<WindowsActiveDisplayPath>& before,
    const std::vector<WindowsActiveDisplayPath>& after)
{
    if (before.size() != after.size())
        return false;
    for (const auto& original : before)
    {
        const auto* current = FindPath(after, original);
        if (!current || current->flags != original.flags ||
            current->outputTechnology != original.outputTechnology ||
            current->gdiDeviceName != original.gdiDeviceName ||
            current->monitorDevicePath != original.monitorDevicePath ||
            current->attachedToDesktop != original.attachedToDesktop ||
            current->targetAvailable != original.targetAvailable)
            return false;
    }
    return true;
}

WindowsMaxRefreshTargetResult MakeTargetResult(const DisplayGroup& group)
{
    WindowsMaxRefreshTargetResult result{};
    result.gdiDeviceName = group.gdiName;
    result.monitorName = group.monitorName;
    if (!group.paths.empty())
    {
        result.sourceAdapter = group.paths.front()->sourceAdapter;
        result.sourceId = group.paths.front()->sourceId;
        result.targetAdapter = group.paths.front()->targetAdapter;
        result.targetId = group.paths.front()->targetId;
    }
    return result;
}

void LogTargetIdentity(const WindowsActiveDisplayPath& path)
{
    std::wostringstream message;
    message << L"source=" << (path.gdiDeviceName.empty() ? L"unknown" : path.gdiDeviceName)
        << L" monitor=" << (path.monitorName.empty() ? L"unknown" : path.monitorName)
        << L" devicePath=" << (path.monitorDevicePath.empty() ? L"unknown" : path.monitorDevicePath)
        << L" sourceAdapter=" << LuidText(path.sourceAdapter)
        << L" sourceId=" << path.sourceId
        << L" targetAdapter=" << LuidText(path.targetAdapter)
        << L" targetId=" << path.targetId
        << L" outputTechnology=" << path.outputTechnology
        << L" boost=" << (IsBoostEnabled(path) ? 1 : 0)
        << L" virtual=" << RationalText(path.virtualRefresh)
        << L" physical=" << RationalText(path.physicalRefresh)
        << L" advancedColor=" << (!path.advancedColorKnown ? L"unknown" :
            path.advancedColorEnabled ? L"on" : L"off");
    VrrDiagnosticLogger::WriteWide("RefreshRate", message.str());
}

std::wstring DisplayChangeName(LONG status)
{
    switch (status)
    {
    case DISP_CHANGE_SUCCESSFUL: return L"DISP_CHANGE_SUCCESSFUL";
    case DISP_CHANGE_RESTART: return L"DISP_CHANGE_RESTART";
    case DISP_CHANGE_BADMODE: return L"DISP_CHANGE_BADMODE";
    case DISP_CHANGE_BADPARAM: return L"DISP_CHANGE_BADPARAM";
    case DISP_CHANGE_BADFLAGS: return L"DISP_CHANGE_BADFLAGS";
    case DISP_CHANGE_FAILED: return L"DISP_CHANGE_FAILED";
    case DISP_CHANGE_NOTUPDATED: return L"DISP_CHANGE_NOTUPDATED";
    case DISP_CHANGE_BADDUALVIEW: return L"DISP_CHANGE_BADDUALVIEW";
    default: return L"DISP_CHANGE_UNKNOWN";
    }
}

void LogChange(const wchar_t* stage, const std::wstring& name, LONG status)
{
    VrrDiagnosticLogger::WriteWide("RefreshRate",
        std::wstring(stage) + L" source=" + name + L" code=" +
        DisplayChangeName(status) + L" value=" + std::to_wstring(status));
}

void LogFinalResult(const WindowsMaxRefreshResult& result)
{
    std::wostringstream summary;
    summary << L"result=" << WindowsMaxRefreshStatusName(result.status)
        << L" activePaths=" << result.activePathCount
        << L" eligible=" << result.eligibleDisplayCount
        << L" applied=" << result.appliedCount
        << L" alreadyMax=" << result.alreadyMaxCount
        << L" skipped=" << result.skippedCount
        << L" failed=" << result.failedCount
        << L" win32=" << result.win32Error
        << L" drrInvariantChecked=" << (result.finalDrrInvariantChecked ? 1 : 0)
        << L" drrInvariantFailed=" << (result.finalDrrInvariantFailed ? 1 : 0);
    VrrDiagnosticLogger::WriteWide("RefreshRate", summary.str());
    for (const auto& target : result.targets)
    {
        std::wostringstream line;
        line << L"final target sourceAdapter=" << LuidText(target.sourceAdapter)
            << L" sourceId=" << target.sourceId
            << L" source=" << (target.gdiDeviceName.empty() ? L"unknown" : target.gdiDeviceName)
            << L" targetAdapter=" << LuidText(target.targetAdapter)
            << L" targetId=" << target.targetId
            << L" monitor=" << (target.monitorName.empty() ? L"unknown" : target.monitorName)
            << L" candidates=" << target.candidateCount
            << L" compatibleCandidates=" << target.compatibleCandidateCount
            << L" beforeHz=" << target.beforeHz
            << L" selectedHz=" << target.maximumHz
            << L" changeCode=" << target.displayChangeCode
            << L" result=" << WindowsMaxRefreshStatusName(target.status);
        VrrDiagnosticLogger::WriteWide("RefreshRate", line.str());
    }
}

bool EnumerateModes(IWindowsMaxRefreshRateApi& api, const std::wstring& deviceName,
    std::vector<DEVMODEW>& modes, bool& complete)
{
    modes.clear();
    complete = false;
    for (DWORD index = 0; index < kMaximumModeCount; ++index)
    {
        auto mode = EmptyMode();
        if (!api.EnumDisplaySettings(deviceName, index, mode))
        {
            complete = true;
            return true;
        }
        modes.push_back(mode);
    }
    return false;
}

std::wstring CandidateSkipReason(const DEVMODEW& current,
    const DEVMODEW& candidate)
{
    constexpr DWORD required = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY |
        DM_BITSPERPEL;
    if (!HasFields(candidate, required))
        return L"MissingFields";
    if (candidate.dmDisplayFrequency <= 1)
        return L"DefaultFrequency";
    if (candidate.dmPelsWidth != current.dmPelsWidth ||
        candidate.dmPelsHeight != current.dmPelsHeight)
        return L"WrongResolution";
    if (candidate.dmBitsPerPel != current.dmBitsPerPel)
        return L"WrongColorDepth";
    if (HasFields(current, DM_DISPLAYORIENTATION) &&
        (!HasFields(candidate, DM_DISPLAYORIENTATION) ||
            candidate.dmDisplayOrientation != current.dmDisplayOrientation))
        return L"WrongOrientation";
    if (HasFields(current, DM_DISPLAYFLAGS) &&
        (!HasFields(candidate, DM_DISPLAYFLAGS) ||
            candidate.dmDisplayFlags != current.dmDisplayFlags))
        return L"WrongScanFlags";
    return {};
}

DEVMODEW RequestedMode(const DEVMODEW& current, const DEVMODEW& enumerated,
    unsigned hz)
{
    auto requested = enumerated;
    requested.dmSize = sizeof(requested);
    requested.dmPosition = current.dmPosition;
    requested.dmPelsWidth = current.dmPelsWidth;
    requested.dmPelsHeight = current.dmPelsHeight;
    requested.dmBitsPerPel = current.dmBitsPerPel;
    requested.dmDisplayOrientation = current.dmDisplayOrientation;
    requested.dmDisplayFlags = current.dmDisplayFlags;
    requested.dmDisplayFrequency = hz;
    // Only refresh is an intended setting change. Retaining the current layout
    // in the structure while omitting those DM fields prevents accidental moves.
    requested.dmFields = DM_DISPLAYFREQUENCY;
    return requested;
}

bool ReadCurrentMode(IWindowsMaxRefreshRateApi& api,
    const std::wstring& deviceName, DEVMODEW& mode)
{
    mode = EmptyMode();
    constexpr DWORD required = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY |
        DM_BITSPERPEL | DM_POSITION;
    return api.EnumDisplaySettings(deviceName, ENUM_CURRENT_SETTINGS, mode) &&
        mode.dmSize >= sizeof(DEVMODEW) && HasFields(mode, required) &&
        mode.dmPelsWidth != 0 && mode.dmPelsHeight != 0;
}

bool RestoreChanges(IWindowsMaxRefreshRateApi& api,
    const std::vector<WindowsMaxRefreshTargetResult>& targets,
    const std::vector<PreparedChange>& changes,
    const std::vector<WindowsActiveDisplayPath>& originalPaths,
    const std::map<std::wstring, DEVMODEW>& originalModes)
{
    bool restored = true;
    std::vector<WindowsActiveDisplayPath> paths;
    const LONG pathStatus = api.ReadActiveDisplayPaths(paths);
    if (pathStatus != ERROR_SUCCESS)
    {
        VrrDiagnosticLogger::WriteWide("RefreshRate",
            L"rollback identity query failed win32=" + std::to_wstring(pathStatus));
        VrrDiagnosticLogger::Write("RefreshRate", "rollbackVerified=0");
        return false;
    }

    std::set<std::wstring> forceRestoreNames;
    for (const auto& change : changes)
        forceRestoreNames.insert(targets[change.targetIndex].gdiDeviceName);

    std::vector<std::pair<std::size_t, DEVMODEW>> restoreModes;
    for (const auto& [deviceName, original] : originalModes)
    {
        DEVMODEW current{};
        const bool currentRead = ReadCurrentMode(api, deviceName, current);
        const bool forceRestore = forceRestoreNames.contains(deviceName);
        if (!currentRead)
        {
            restored = false;
            VrrDiagnosticLogger::WriteWide("RefreshRate",
                L"rollback could not read current mode source=" + deviceName);
            continue;
        }
        if (!forceRestore && SameCurrentMode(current, original))
            continue;

        std::size_t targetIndex = targets.size();
        unsigned matchCount{};
        for (std::size_t index = 0; index < targets.size(); ++index)
        {
            const auto& target = targets[index];
            if (target.gdiDeviceName == deviceName)
            {
                targetIndex = index;
                ++matchCount;
            }
        }
        const auto baselinePath = targetIndex < targets.size()
            ? std::find_if(originalPaths.begin(), originalPaths.end(),
                [&](const auto& path)
                {
                    return SameLuid(path.sourceAdapter, targets[targetIndex].sourceAdapter) &&
                        path.sourceId == targets[targetIndex].sourceId &&
                        SameLuid(path.targetAdapter, targets[targetIndex].targetAdapter) &&
                        path.targetId == targets[targetIndex].targetId &&
                        path.gdiDeviceName == deviceName;
                })
            : originalPaths.end();
        const auto* currentPath = baselinePath == originalPaths.end()
            ? nullptr : FindPath(paths, *baselinePath);
        if (matchCount != 1 || !currentPath || currentPath->gdiDeviceName != deviceName ||
            currentPath->outputTechnology != baselinePath->outputTechnology ||
            currentPath->monitorDevicePath != baselinePath->monitorDevicePath ||
            currentPath->monitorDevicePath.empty() || !currentPath->attachedToDesktop ||
            currentPath->mirroringDriver || currentPath->remoteSession)
        {
            restored = false;
            VrrDiagnosticLogger::WriteWide("RefreshRate",
                L"rollback skipped source=" + deviceName +
                L" reason=TargetIdentityNotUniqueOrAttached");
            continue;
        }

        restoreModes.emplace_back(targetIndex, original);
    }

    const bool batchRestore = restoreModes.size() > 1;
    bool stagedAny{};
    for (const auto& [targetIndex, originalMode] : restoreModes)
    {
        const auto& target = targets[targetIndex];
        auto original = originalMode;
        const DWORD flags = batchRestore
            ? CDS_UPDATEREGISTRY | CDS_NORESET : CDS_UPDATEREGISTRY;
        const LONG status = api.ChangeDisplaySettings(target.gdiDeviceName.c_str(),
            &original, flags);
        LogChange(L"rollback", target.gdiDeviceName, status);
        if (status != DISP_CHANGE_SUCCESSFUL)
            restored = false;
        else if (batchRestore)
            stagedAny = true;
    }
    if (stagedAny)
    {
        const LONG status = api.ChangeDisplaySettings(nullptr, nullptr, 0);
        LogChange(L"rollbackCommit", L"all", status);
        if (status != DISP_CHANGE_SUCCESSFUL)
            restored = false;
    }

    std::vector<WindowsActiveDisplayPath> restoredPaths;
    const LONG restoredPathStatus = api.ReadActiveDisplayPaths(restoredPaths);
    if (restoredPathStatus != ERROR_SUCCESS ||
        !SameTopology(originalPaths, restoredPaths))
        restored = false;
    for (const auto& originalPath : originalPaths)
    {
        const auto* currentPath = FindPath(restoredPaths, originalPath);
        if (originalPath.advancedColorKnown &&
            (!currentPath || !currentPath->advancedColorKnown ||
                currentPath->advancedColorEnabled != originalPath.advancedColorEnabled))
            restored = false;
        if (currentPath && !SameRationalWithinTolerance(
                currentPath->physicalRefresh, originalPath.physicalRefresh))
            restored = false;
    }
    for (const auto& [deviceName, original] : originalModes)
    {
        DEVMODEW current{};
        if (!ReadCurrentMode(api, deviceName, current) ||
            !SameCurrentMode(current, original))
            restored = false;
    }
    VrrDiagnosticLogger::Write("RefreshRate",
        std::string("rollbackVerified=") + (restored ? "1" : "0"));
    return restored;
}

WindowsMaxRefreshResult Run(IWindowsMaxRefreshRateApi& api, bool drrStageSucceeded)
{
    WindowsMaxRefreshResult result{};
    std::vector<WindowsActiveDisplayPath> originalPaths;
    result.win32Error = api.ReadActiveDisplayPaths(originalPaths);
    if (result.win32Error != ERROR_SUCCESS)
    {
        VrrDiagnosticLogger::WriteWide("RefreshRate",
            L"active path query failed win32=" + std::to_wstring(result.win32Error) +
            L" result=Unavailable");
        result.status = WindowsMaxRefreshStatus::Unavailable;
        return result;
    }

    result.activePathCount = static_cast<unsigned>(originalPaths.size());
    if (originalPaths.empty())
    {
        result.status = WindowsMaxRefreshStatus::Unavailable;
        VrrDiagnosticLogger::Write("RefreshRate", "no active Windows display paths result=Unavailable");
        return result;
    }

    result.finalDrrInvariantChecked = drrStageSucceeded;
    if (drrStageSucceeded)
    {
        for (const auto& path : originalPaths)
        {
            if (!IsBoostEnabled(path))
                continue;
            result.finalDrrInvariantFailed = true;
            VrrDiagnosticLogger::WriteWide("DRR",
                L"final invariant=Failed targetAdapter=" + LuidText(path.targetAdapter) +
                L" targetId=" + std::to_wstring(path.targetId) +
                L" reason=BOOST enabled after successful DRR stage");
        }
    }

    std::map<SourceKey, DisplayGroup> groups;
    for (const auto& path : originalPaths)
    {
        LogTargetIdentity(path);
        auto& group = groups[MakeSourceKey(path)];
        group.key = MakeSourceKey(path);
        group.paths.push_back(&path);
        if (group.gdiName.empty())
            group.gdiName = path.gdiDeviceName;
        if (group.monitorName.empty())
            group.monitorName = path.monitorName;
        const auto reason = SkipReason(path);
        if (!reason.empty())
        {
            group.unsafe = true;
            group.unsafeReason = reason;
        }
        else if (group.gdiName != path.gdiDeviceName)
        {
            group.unsafe = true;
            group.unsafeReason = L"ConflictingGdiNamesForSource";
        }
    }

    std::map<std::wstring, unsigned> nameUseCount;
    for (const auto& [key, group] : groups)
    {
        (void)key;
        if (!group.gdiName.empty())
            ++nameUseCount[FoldName(group.gdiName)];
    }

    std::vector<DisplayGroup> eligible;
    for (auto& [key, group] : groups)
    {
        (void)key;
        std::set<TargetKey> targets;
        for (const auto* path : group.paths)
            targets.insert(MakeTargetKey(*path));
        if (targets.size() > 1)
        {
            group.unsafe = true;
            group.unsafeReason = L"ClonedSourceSkipped";
        }
        if (!group.gdiName.empty() && nameUseCount[FoldName(group.gdiName)] > 1)
        {
            group.unsafe = true;
            group.unsafeReason = L"AmbiguousGdiName";
        }

        if (group.unsafe)
        {
            auto target = MakeTargetResult(group);
            target.status = WindowsMaxRefreshStatus::SkippedUnsafeTopology;
            result.targets.push_back(std::move(target));
            ++result.skippedCount;
            VrrDiagnosticLogger::WriteWide("RefreshRate",
                L"source=" + (group.gdiName.empty() ? L"unknown" : group.gdiName) +
                L" result=SkippedUnsafeTopology reason=" + group.unsafeReason);
            continue;
        }
        eligible.push_back(group);
    }
    result.eligibleDisplayCount = static_cast<unsigned>(eligible.size());
    if (eligible.empty())
    {
        result.status = result.skippedCount > 0
            ? WindowsMaxRefreshStatus::SkippedUnsafeTopology
            : WindowsMaxRefreshStatus::Unavailable;
        return result;
    }

    std::vector<PreparedChange> prepared;
    std::map<std::wstring, DEVMODEW> originalModes;
    bool completeModeBaseline = true;
    for (const auto& group : eligible)
    {
        auto target = MakeTargetResult(group);
        const auto& path = *group.paths.front();
        DEVMODEW current{};
        if (!ReadCurrentMode(api, group.gdiName, current))
        {
            completeModeBaseline = false;
            target.status = WindowsMaxRefreshStatus::NoCompatibleMode;
            ++result.failedCount;
            result.targets.push_back(target);
            VrrDiagnosticLogger::WriteWide("RefreshRate",
                L"source=" + group.gdiName + L" result=NoCompatibleMode reason=CurrentModeUnavailable");
            continue;
        }
        originalModes.emplace(group.gdiName, current);
        target.beforeHz = current.dmDisplayFrequency;
        VrrDiagnosticLogger::WriteWide("RefreshRate",
            L"source=" + group.gdiName + L" modeBefore=" +
            std::to_wstring(current.dmPelsWidth) + L"x" +
            std::to_wstring(current.dmPelsHeight) + L" " +
            std::to_wstring(current.dmDisplayFrequency) + L"Hz orientation=" +
            std::to_wstring(current.dmDisplayOrientation) + L" bpp=" +
            std::to_wstring(current.dmBitsPerPel) + L" position=" +
            std::to_wstring(current.dmPosition.x) + L"," +
            std::to_wstring(current.dmPosition.y));

        std::vector<DEVMODEW> enumerated;
        bool enumerationComplete{};
        if (!EnumerateModes(api, group.gdiName, enumerated, enumerationComplete) ||
            !enumerationComplete)
        {
            target.candidateCount = static_cast<unsigned>(enumerated.size());
            target.status = WindowsMaxRefreshStatus::NoCompatibleMode;
            ++result.failedCount;
            result.targets.push_back(target);
            VrrDiagnosticLogger::WriteWide("RefreshRate",
                L"source=" + group.gdiName + L" candidateEnumeration=Incomplete count=" +
                std::to_wstring(enumerated.size()) + L" result=NoCompatibleMode");
            continue;
        }

        std::map<std::wstring, unsigned> skippedReasons;
        std::map<unsigned, DEVMODEW, std::greater<unsigned>> candidatesByHz;
        target.candidateCount = static_cast<unsigned>(enumerated.size());
        for (const auto& candidate : enumerated)
        {
            const auto reason = CandidateSkipReason(current, candidate);
            if (!reason.empty())
            {
                ++skippedReasons[reason];
                continue;
            }
            ++target.compatibleCandidateCount;
            candidatesByHz.try_emplace(candidate.dmDisplayFrequency, candidate);
        }

        std::wostringstream candidateLog;
        candidateLog << L"source=" << group.gdiName << L" candidates=" << target.candidateCount
            << L" compatibleCandidates=" << target.compatibleCandidateCount << L" skippedModes=";
        if (skippedReasons.empty())
            candidateLog << L"none";
        else
        {
            bool first = true;
            for (const auto& [reason, count] : skippedReasons)
            {
                if (!first) candidateLog << L",";
                first = false;
                candidateLog << reason << L":" << count;
            }
        }
        candidateLog << L" enumeratedMaxHz=" <<
            (candidatesByHz.empty() ? 0 : candidatesByHz.begin()->first);
        VrrDiagnosticLogger::WriteWide("RefreshRate", candidateLog.str());

        if (candidatesByHz.empty() || current.dmDisplayFrequency <= 1)
        {
            target.status = WindowsMaxRefreshStatus::NoCompatibleMode;
            ++result.failedCount;
            result.targets.push_back(target);
            VrrDiagnosticLogger::WriteWide("RefreshRate",
                L"source=" + group.gdiName + L" result=NoCompatibleMode reason=" +
                (current.dmDisplayFrequency <= 1 ? L"UnknownCurrentFrequency" : L"NoCompatibleCandidates"));
            continue;
        }

        target.maximumHz = candidatesByHz.begin()->first;
        if (current.dmDisplayFrequency >= target.maximumHz)
        {
            target.status = WindowsMaxRefreshStatus::AlreadyMax;
            ++result.alreadyMaxCount;
            result.targets.push_back(target);
            VrrDiagnosticLogger::WriteWide("RefreshRate",
                L"source=" + group.gdiName + L" result=AlreadyMax modeBefore=" +
                std::to_wstring(current.dmDisplayFrequency) + L"Hz compatibleMax=" +
                std::to_wstring(target.maximumHz) + L"Hz apply=not-called");
            continue;
        }

        bool selected{};
        LONG lastTestStatus = DISP_CHANGE_BADMODE;
        for (const auto& [hz, enumeratedMode] : candidatesByHz)
        {
            if (hz <= current.dmDisplayFrequency)
                break;
            auto requested = RequestedMode(current, enumeratedMode, hz);
            const LONG testStatus = api.ChangeDisplaySettings(
                group.gdiName.c_str(), &requested, CDS_TEST);
            LogChange(L"test", group.gdiName, testStatus);
            if (testStatus == DISP_CHANGE_SUCCESSFUL)
            {
                target.maximumHz = hz;
                target.displayChangeCode = testStatus;
                prepared.push_back({ result.targets.size(), current, requested, hz, false });
                selected = true;
                break;
            }
            lastTestStatus = testStatus;
        }

        if (!selected)
        {
            if (target.maximumHz <= current.dmDisplayFrequency)
            {
                target.status = WindowsMaxRefreshStatus::AlreadyMax;
                ++result.alreadyMaxCount;
            }
            else
            {
                target.status = WindowsMaxRefreshStatus::ValidateFailed;
                target.displayChangeCode = lastTestStatus;
                ++result.failedCount;
            }
            result.targets.push_back(target);
            VrrDiagnosticLogger::WriteWide("RefreshRate",
                L"source=" + group.gdiName + L" result=" +
                WindowsMaxRefreshStatusName(target.status) + L" test=" +
                DisplayChangeName(lastTestStatus));
            continue;
        }
        target.status = WindowsMaxRefreshStatus::Unavailable;
        result.targets.push_back(target);
    }

    if (!completeModeBaseline && !prepared.empty())
    {
        for (const auto& change : prepared)
        {
            auto& target = result.targets[change.targetIndex];
            target.status = WindowsMaxRefreshStatus::SkippedUnsafeTopology;
            ++result.skippedCount;
        }
        result.status = WindowsMaxRefreshStatus::SkippedUnsafeTopology;
        VrrDiagnosticLogger::Write("RefreshRate",
            "incomplete active-source mode baseline; no display changes applied");
        return result;
    }

    if (prepared.empty())
    {
        const auto hasStatus = [&](WindowsMaxRefreshStatus status)
        {
            return std::any_of(result.targets.begin(), result.targets.end(),
                [status](const auto& target) { return target.status == status; });
        };
        if (hasStatus(WindowsMaxRefreshStatus::VerifyFailed))
            result.status = WindowsMaxRefreshStatus::VerifyFailed;
        else if (hasStatus(WindowsMaxRefreshStatus::ApplyFailed))
            result.status = WindowsMaxRefreshStatus::ApplyFailed;
        else if (hasStatus(WindowsMaxRefreshStatus::ValidateFailed))
            result.status = WindowsMaxRefreshStatus::ValidateFailed;
        else if (hasStatus(WindowsMaxRefreshStatus::NoCompatibleMode))
            result.status = WindowsMaxRefreshStatus::NoCompatibleMode;
        else if (result.alreadyMaxCount > 0)
            result.status = result.skippedCount > 0
                ? WindowsMaxRefreshStatus::Partial : WindowsMaxRefreshStatus::AlreadyMax;
        else
            result.status = result.skippedCount > 0
                ? WindowsMaxRefreshStatus::SkippedUnsafeTopology
                : WindowsMaxRefreshStatus::Unavailable;
        return result;
    }

    // Re-query the active graph and current modes immediately before any write.
    std::vector<WindowsActiveDisplayPath> freshPaths;
    LONG freshStatus = api.ReadActiveDisplayPaths(freshPaths);
    bool freshMatches = freshStatus == ERROR_SUCCESS && SameTopology(originalPaths, freshPaths);
    for (const auto& [deviceName, original] : originalModes)
    {
        DEVMODEW current{};
        if (!ReadCurrentMode(api, deviceName, current) ||
            !SameCurrentMode(current, original))
            freshMatches = false;
    }
    if (!freshMatches)
    {
        for (const auto& change : prepared)
        {
            auto& target = result.targets[change.targetIndex];
            target.status = WindowsMaxRefreshStatus::SkippedUnsafeTopology;
            ++result.skippedCount;
        }
        result.status = WindowsMaxRefreshStatus::SkippedUnsafeTopology;
        VrrDiagnosticLogger::WriteWide("RefreshRate",
            L"fresh identity/current-mode check failed win32=" + std::to_wstring(freshStatus) +
            L"; no display changes applied result=SkippedUnsafeTopology");
        return result;
    }

    // A candidate can become unavailable between initial validation and this point.
    for (const auto& change : prepared)
    {
        auto candidate = change.requested;
        const auto& target = result.targets[change.targetIndex];
        const LONG test = api.ChangeDisplaySettings(
            target.gdiDeviceName.c_str(), &candidate, CDS_TEST);
        LogChange(L"finalTest", target.gdiDeviceName, test);
        if (test != DISP_CHANGE_SUCCESSFUL)
        {
            auto& mutableTarget = result.targets[change.targetIndex];
            mutableTarget.status = WindowsMaxRefreshStatus::ValidateFailed;
            mutableTarget.displayChangeCode = test;
            ++result.failedCount;
            for (const auto& pending : prepared)
            {
                if (result.targets[pending.targetIndex].status == WindowsMaxRefreshStatus::Unavailable)
                {
                    result.targets[pending.targetIndex].status = WindowsMaxRefreshStatus::SkippedUnsafeTopology;
                    ++result.skippedCount;
                }
            }
            result.status = WindowsMaxRefreshStatus::ValidateFailed;
            VrrDiagnosticLogger::WriteWide("RefreshRate",
                L"source=" + target.gdiDeviceName + L" candidate changed before apply; no display changes applied");
            return result;
        }
    }

    const bool batch = prepared.size() > 1;
    for (auto& change : prepared)
    {
        auto& target = result.targets[change.targetIndex];
        auto requested = change.requested;
        const DWORD flags = batch
            ? CDS_UPDATEREGISTRY | CDS_NORESET : CDS_UPDATEREGISTRY;
        const LONG status = api.ChangeDisplaySettings(
            target.gdiDeviceName.c_str(), &requested, flags);
        LogChange(batch ? L"stage" : L"apply", target.gdiDeviceName, status);
        if (status != DISP_CHANGE_SUCCESSFUL)
        {
            target.status = WindowsMaxRefreshStatus::ApplyFailed;
            target.displayChangeCode = status;
            ++result.failedCount;
            (void)RestoreChanges(api, result.targets, prepared,
                originalPaths, originalModes);
            for (const auto& pending : prepared)
            {
                auto& pendingTarget = result.targets[pending.targetIndex];
                if (&pendingTarget != &target &&
                    pendingTarget.status == WindowsMaxRefreshStatus::Unavailable)
                {
                    pendingTarget.status = WindowsMaxRefreshStatus::SkippedUnsafeTopology;
                    ++result.skippedCount;
                }
            }
            result.status = WindowsMaxRefreshStatus::ApplyFailed;
            return result;
        }
        change.staged = true;
    }
    if (batch)
    {
        const LONG status = api.ChangeDisplaySettings(nullptr, nullptr, 0);
        LogChange(L"commit", L"all", status);
        if (status != DISP_CHANGE_SUCCESSFUL)
        {
            for (auto& change : prepared)
            {
                result.targets[change.targetIndex].status = WindowsMaxRefreshStatus::ApplyFailed;
                result.targets[change.targetIndex].displayChangeCode = status;
            }
            result.failedCount += static_cast<unsigned>(prepared.size());
            RestoreChanges(api, result.targets, prepared,
                originalPaths, originalModes);
            result.status = WindowsMaxRefreshStatus::ApplyFailed;
            return result;
        }
    }

    std::vector<WindowsActiveDisplayPath> afterPaths;
    const LONG afterStatus = api.ReadActiveDisplayPaths(afterPaths);
    bool topologyPreserved = afterStatus == ERROR_SUCCESS &&
        SameTopology(originalPaths, afterPaths);
    result.finalDrrInvariantChecked = drrStageSucceeded && afterStatus == ERROR_SUCCESS;
    bool verificationPassed = topologyPreserved;
    bool advancedColorPreserved = true;
    for (const auto& originalPath : originalPaths)
    {
        const auto* afterPath = FindPath(afterPaths, originalPath);
        if (originalPath.advancedColorKnown &&
            (!afterPath || !afterPath->advancedColorKnown ||
                afterPath->advancedColorEnabled != originalPath.advancedColorEnabled))
            advancedColorPreserved = false;
        const bool boostUnchanged = afterPath &&
            IsBoostEnabled(*afterPath) == IsBoostEnabled(originalPath);
        if (!boostUnchanged)
        {
            verificationPassed = false;
            if (drrStageSucceeded && afterPath && IsBoostEnabled(*afterPath))
            {
                result.finalDrrInvariantFailed = true;
                VrrDiagnosticLogger::WriteWide("DRR",
                    L"final invariant=Failed targetAdapter=" + LuidText(originalPath.targetAdapter) +
                    L" targetId=" + std::to_wstring(originalPath.targetId) +
                    L" reason=BOOST re-enabled during fixed-refresh apply");
            }
        }
        VrrDiagnosticLogger::WriteWide("RefreshRate",
            L"final path source=" + originalPath.gdiDeviceName +
            L" target=" + LuidText(originalPath.targetAdapter) + L":" +
            std::to_wstring(originalPath.targetId) +
            L" boostStillOff=" + (afterPath && !IsBoostEnabled(*afterPath) ? L"1" : L"0") +
            L" boostUnchanged=" + (boostUnchanged ? L"1" : L"0") +
            L" advancedColorPreserved=" +
            (originalPath.advancedColorKnown
                ? (afterPath && afterPath->advancedColorKnown &&
                    afterPath->advancedColorEnabled == originalPath.advancedColorEnabled ? L"1" : L"0")
                : L"unknown"));
    }
    verificationPassed = verificationPassed && advancedColorPreserved;

    std::map<std::wstring, unsigned> requestedFrequencies;
    for (const auto& change : prepared)
        requestedFrequencies[result.targets[change.targetIndex].gdiDeviceName] =
            change.requestedHz;
    for (const auto& [deviceName, original] : originalModes)
    {
        DEVMODEW current{};
        const bool read = ReadCurrentMode(api, deviceName, current);
        const auto requested = requestedFrequencies.find(deviceName);
        const bool unchangedOrRequested = read && SameLayout(original, current) &&
            (requested == requestedFrequencies.end()
                ? current.dmDisplayFrequency == original.dmDisplayFrequency
                : current.dmDisplayFrequency == requested->second);
        VrrDiagnosticLogger::WriteWide("RefreshRate",
            L"final mode source=" + deviceName + L" readback=" +
            (read ? std::to_wstring(current.dmPelsWidth) + L"x" +
                std::to_wstring(current.dmPelsHeight) + L" " +
                std::to_wstring(current.dmDisplayFrequency) + L"Hz" : L"unavailable") +
            L" layoutPreserved=" + (read && SameLayout(original, current) ? L"1" : L"0"));
        if (!unchangedOrRequested)
            verificationPassed = false;
    }
    for (auto& change : prepared)
    {
        auto& target = result.targets[change.targetIndex];
        DEVMODEW after{};
        const bool modeRead = ReadCurrentMode(api, target.gdiDeviceName, after);
        const bool modePassed = modeRead && SameLayout(change.original, after) &&
            after.dmDisplayFrequency == change.requestedHz;
        const auto originalPath = std::find_if(originalPaths.begin(), originalPaths.end(),
            [&](const auto& path)
            {
                return SameLuid(path.sourceAdapter, target.sourceAdapter) &&
                    path.sourceId == target.sourceId &&
                    SameLuid(path.targetAdapter, target.targetAdapter) &&
                    path.targetId == target.targetId &&
                    path.gdiDeviceName == target.gdiDeviceName;
            });
        const auto* afterPath = originalPath == originalPaths.end()
            ? nullptr : FindPath(afterPaths, *originalPath);
        const bool boostPreserved = afterPath && originalPath != originalPaths.end() &&
            IsBoostEnabled(*afterPath) == IsBoostEnabled(*originalPath);
        const bool colorPreserved = originalPath == originalPaths.end() ||
            !originalPath->advancedColorKnown ||
            (afterPath && afterPath->advancedColorKnown &&
                afterPath->advancedColorEnabled == originalPath->advancedColorEnabled);
        const bool rationalKnown = afterPath && afterPath->physicalRefresh.Denominator != 0;
        const bool rationalPassed = afterPath &&
            RationalWithinTolerance(afterPath->physicalRefresh, change.requestedHz);

        std::wostringstream verification;
        verification << L"source=" << target.gdiDeviceName
            << L" modeAfter=" << (modeRead ? std::to_wstring(after.dmPelsWidth) + L"x" +
                std::to_wstring(after.dmPelsHeight) + L" " +
                std::to_wstring(after.dmDisplayFrequency) + L"Hz" : L"unavailable")
            << L" physicalAfter=" << (afterPath ? RationalText(afterPath->physicalRefresh) : L"unknown")
            << L" virtualAfter=" << (afterPath ? RationalText(afterPath->virtualRefresh) : L"unknown")
            << L" resolutionPreserved=" << (modeRead && SameLayout(change.original, after) ? 1 : 0)
            << L" topologyPreserved=" << (topologyPreserved ? 1 : 0)
            << L" boostStillOff=" << (afterPath && !IsBoostEnabled(*afterPath) ? 1 : 0)
            << L" boostUnchanged=" << (boostPreserved ? 1 : 0)
            << L" advancedColorPreserved=" << (colorPreserved ? 1 : 0)
            << L" rationalWithin0.5Hz=" << (rationalKnown
                ? (rationalPassed ? L"1" : L"0") : L"unknown");
        VrrDiagnosticLogger::WriteWide("RefreshRate", verification.str());

        if (!modePassed || !topologyPreserved || !boostPreserved ||
            !colorPreserved || !rationalPassed)
        {
            target.status = WindowsMaxRefreshStatus::VerifyFailed;
            ++result.failedCount;
            verificationPassed = false;
        }
        else
        {
            target.status = WindowsMaxRefreshStatus::Applied;
            ++result.appliedCount;
        }
    }

    if (!verificationPassed)
    {
        for (const auto& change : prepared)
            result.targets[change.targetIndex].status = WindowsMaxRefreshStatus::VerifyFailed;
        result.failedCount += result.appliedCount;
        result.appliedCount = 0;
        const bool restored = RestoreChanges(api, result.targets, prepared,
            originalPaths, originalModes);
        result.status = restored
            ? WindowsMaxRefreshStatus::VerifyFailed : WindowsMaxRefreshStatus::Partial;
    }
    else if (result.failedCount > 0 || result.skippedCount > 0)
    {
        result.status = WindowsMaxRefreshStatus::Partial;
    }
    else
        result.status = WindowsMaxRefreshStatus::Applied;

    std::wostringstream summary;
    summary << L"result=" << WindowsMaxRefreshStatusName(result.status)
        << L" activePaths=" << result.activePathCount
        << L" eligible=" << result.eligibleDisplayCount
        << L" applied=" << result.appliedCount
        << L" alreadyMax=" << result.alreadyMaxCount
        << L" skipped=" << result.skippedCount
        << L" failed=" << result.failedCount;
    VrrDiagnosticLogger::WriteWide("RefreshRate", summary.str());
    return result;
}
}

const wchar_t* WindowsMaxRefreshStatusName(WindowsMaxRefreshStatus status) noexcept
{
    switch (status)
    {
    case WindowsMaxRefreshStatus::Applied: return L"Applied";
    case WindowsMaxRefreshStatus::AlreadyMax: return L"AlreadyMax";
    case WindowsMaxRefreshStatus::NoCompatibleMode: return L"NoCompatibleMode";
    case WindowsMaxRefreshStatus::SkippedUnsafeTopology: return L"SkippedUnsafeTopology";
    case WindowsMaxRefreshStatus::ValidateFailed: return L"ValidateFailed";
    case WindowsMaxRefreshStatus::ApplyFailed: return L"ApplyFailed";
    case WindowsMaxRefreshStatus::VerifyFailed: return L"VerifyFailed";
    case WindowsMaxRefreshStatus::Partial: return L"Partial";
    default: return L"Unavailable";
    }
}

WindowsMaxRefreshResult MaximizeFixedRefreshRateForActiveDisplays()
{
    return MaximizeFixedRefreshRateForActiveDisplays(false);
}

WindowsMaxRefreshResult MaximizeFixedRefreshRateForActiveDisplays(
    bool drrStageSucceeded)
{
    Win32MaxRefreshRateApi api;
    return MaximizeFixedRefreshRateForActiveDisplays(api, drrStageSucceeded);
}

WindowsMaxRefreshResult MaximizeFixedRefreshRateForActiveDisplays(
    IWindowsMaxRefreshRateApi& api, bool drrStageSucceeded)
{
    try
    {
        auto result = Run(api, drrStageSucceeded);
        LogFinalResult(result);
        return result;
    }
    catch (...)
    {
        VrrDiagnosticLogger::Write("RefreshRate",
            "unexpected exception during maximum fixed-refresh stage result=Unavailable");
        return { WindowsMaxRefreshStatus::Unavailable };
    }
}
}
