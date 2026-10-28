#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace clawhud
{
struct WindowsActiveDisplayPath
{
    LUID sourceAdapter{};
    UINT32 sourceId{};
    LUID targetAdapter{};
    UINT32 targetId{};
    UINT32 outputTechnology{};
    UINT32 flags{};
    DISPLAYCONFIG_RATIONAL virtualRefresh{};
    DISPLAYCONFIG_RATIONAL physicalRefresh{};
    std::wstring gdiDeviceName;
    std::wstring monitorDevicePath;
    std::wstring monitorName;
    bool sourceNameValid{};
    bool attachedToDesktop{};
    bool mirroringDriver{};
    bool remoteSession{};
    bool advancedColorKnown{};
    bool advancedColorEnabled{};
};

enum class WindowsMaxRefreshStatus
{
    Applied,
    AlreadyMax,
    Partial,
    NoCompatibleMode,
    SkippedUnsafeTopology,
    ValidateFailed,
    ApplyFailed,
    VerifyFailed,
    Unavailable,
};

struct WindowsMaxRefreshTargetResult
{
    LUID sourceAdapter{};
    std::wstring gdiDeviceName;
    std::wstring monitorName;
    UINT32 sourceId{};
    LUID targetAdapter{};
    UINT32 targetId{};
    unsigned candidateCount{};
    unsigned compatibleCandidateCount{};
    unsigned beforeHz{};
    unsigned maximumHz{};
    WindowsMaxRefreshStatus status{ WindowsMaxRefreshStatus::Unavailable };
    LONG displayChangeCode{};
};

struct WindowsMaxRefreshResult
{
    WindowsMaxRefreshStatus status{ WindowsMaxRefreshStatus::Unavailable };
    unsigned activePathCount{};
    unsigned eligibleDisplayCount{};
    unsigned appliedCount{};
    unsigned alreadyMaxCount{};
    unsigned skippedCount{};
    unsigned failedCount{};
    bool finalDrrInvariantChecked{};
    bool finalDrrInvariantFailed{};
    LONG win32Error{};
    std::vector<WindowsMaxRefreshTargetResult> targets;
};

class IWindowsMaxRefreshRateApi
{
public:
    virtual ~IWindowsMaxRefreshRateApi() = default;

    virtual LONG ReadActiveDisplayPaths(
        std::vector<WindowsActiveDisplayPath>& paths) = 0;
    virtual bool EnumDisplaySettings(const std::wstring& deviceName,
        DWORD modeIndex, DEVMODEW& mode) = 0;
    virtual LONG ChangeDisplaySettings(const wchar_t* deviceName,
        DEVMODEW* mode, DWORD flags) = 0;
};

WindowsMaxRefreshResult MaximizeFixedRefreshRateForActiveDisplays();
WindowsMaxRefreshResult MaximizeFixedRefreshRateForActiveDisplays(
    bool drrStageSucceeded);
WindowsMaxRefreshResult MaximizeFixedRefreshRateForActiveDisplays(
    IWindowsMaxRefreshRateApi& api, bool drrStageSucceeded = false);
const wchar_t* WindowsMaxRefreshStatusName(WindowsMaxRefreshStatus status) noexcept;
}
