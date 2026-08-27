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

bool ShouldEvaluateForegroundCandidate(DWORD committedProcessId,
    DWORD foregroundProcessId) noexcept
{
    return committedProcessId == 0 && foregroundProcessId != 0;
}

bool ShouldReplacePendingCandidate(DWORD pendingProcessId,
    DWORD foregroundProcessId) noexcept
{
    return foregroundProcessId != 0 && foregroundProcessId != pendingProcessId;
}

DWORD SelectProductionSamplingProcess(DWORD trackedProcessId,
    DWORD pendingProcessId) noexcept
{
    return trackedProcessId ? trackedProcessId : pendingProcessId;
}

bool ShouldSampleProductionPresentMon(DWORD committedProcessId,
    DWORD pendingProcessId, bool committedProcessAlive) noexcept
{
    return (committedProcessId != 0 && committedProcessAlive) ||
        (committedProcessId == 0 && pendingProcessId != 0);
}

bool ShouldRetainCommittedProductionTarget(DWORD committedProcessId,
    bool processAlive) noexcept
{
    return committedProcessId != 0 && processAlive;
}

bool ShouldPreservePendingProductionValidation(DWORD pendingProcessId,
    DWORD presentMonProcessId, bool presentMonRunning) noexcept
{
    return pendingProcessId != 0 && pendingProcessId == presentMonProcessId &&
        presentMonRunning;
}

bool ShouldDeferPendingProductionValidation(DWORD pendingProcessId,
    DWORD foregroundProcessId, bool foregroundUsable,
    bool candidateProcessAlive) noexcept
{
    return pendingProcessId != 0 && candidateProcessAlive &&
        (foregroundProcessId == 0 || !foregroundUsable);
}

bool ShouldRetryProductionPresentMon(DWORD retryProcessId,
    unsigned retryAttempts, DWORD processId) noexcept
{
    return processId != 0 && (retryProcessId != processId || retryAttempts == 0);
}

bool ShouldAllowProductionPresentMonStart(DWORD committedProcessId,
    DWORD processId, DWORD retryProcessId, unsigned retryAttempts,
    bool recoveryStart) noexcept
{
    return committedProcessId != processId || recoveryStart ||
        ShouldRetryProductionPresentMon(retryProcessId, retryAttempts, processId);
}

bool ShouldReevaluateForegroundAfterResume(bool hudEnabled,
    bool recoveryCompleted) noexcept
{
    return hudEnabled && recoveryCompleted;
}

bool ShouldReevaluateForegroundAfterDiagnostic(bool hudEnabled,
    bool diagnosticRunning, bool suspended) noexcept
{
    return hudEnabled && !diagnosticRunning && !suspended;
}

bool ShouldCancelPendingCandidateOnCommittedReturn(DWORD committedProcessId,
    DWORD pendingProcessId, DWORD foregroundProcessId) noexcept
{
    return committedProcessId != 0 && committedProcessId == foregroundProcessId &&
        pendingProcessId != 0 && pendingProcessId != committedProcessId;
}

bool ShouldRestartGraphicsApiProbe(DWORD probedProcessId,
    DWORD committedProcessId) noexcept
{
    return committedProcessId != 0 && probedProcessId != committedProcessId;
}

bool ShouldConfirmProductionTarget(DWORD pendingProcessId, DWORD observedProcessId,
    DWORD foregroundProcessId, bool displayedFpsAvailable) noexcept
{
    return pendingProcessId != 0 && pendingProcessId == observedProcessId &&
        foregroundProcessId == observedProcessId && displayedFpsAvailable;
}

bool ShouldConsiderForegroundProductionTarget(bool hudEnabled, bool diagnosticRunning,
    bool suspended) noexcept
{
    return hudEnabled && !diagnosticRunning && !suspended;
}
}
