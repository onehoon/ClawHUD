#include "ProductionTargetPolicy.h"

#include <array>

namespace clawhud
{
bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept
{
    constexpr std::array<std::wstring_view, 18> rejected{
        L"explorer.exe", L"searchhost.exe", L"shellexperiencehost.exe",
        L"startmenuexperiencehost.exe", L"applicationframehost.exe",
        L"steam.exe", L"steamwebhelper.exe", L"gamebar.exe",
        L"gamebarftserver.exe", L"xboxpcappft.exe", L"gamingservices.exe",
        L"textinputhost.exe", L"chrome.exe", L"msedge.exe",
        L"firefox.exe", L"brave.exe", L"opera.exe", L"vivaldi.exe"};
    for (const auto candidate : rejected)
        if (candidate == image)
            return true;
    return false;
}

bool ShouldRetainCommittedProductionTarget(DWORD processId, bool alive) noexcept
{
    return processId != 0 && alive;
}

bool ShouldConfirmProductionTarget(DWORD pendingProcessId, DWORD observedProcessId,
    bool displayedFpsAvailable) noexcept
{
    return pendingProcessId != 0 && pendingProcessId == observedProcessId &&
        displayedFpsAvailable;
}

bool ShouldConsiderForegroundProductionTarget(bool hudEnabled, bool diagnosticRunning,
    bool suspended) noexcept
{
    return hudEnabled && !diagnosticRunning && !suspended;
}
}
