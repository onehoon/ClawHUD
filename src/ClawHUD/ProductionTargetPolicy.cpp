#include "ProductionTargetPolicy.h"

#include <array>

namespace clawhud
{
CandidateDisposition DecideCandidateDisposition(
    const GameDetectionContext& context,
    GameDetectionTrigger incomingTrigger, DWORD incomingProcessId) noexcept
{
    if (incomingProcessId == 0)
        return CandidateDisposition::Ignore;
    if (context.candidateProcessId == incomingProcessId)
        return CandidateDisposition::Merge;
    if (context.state == GameDetectionState::Committed)
        return CandidateDisposition::Ignore;
    if (context.state == GameDetectionState::Ready)
        return CandidateDisposition::Ignore;
    if (context.candidateProcessId == 0)
        return CandidateDisposition::Replace;
    if (context.evidence.microsoftGameIdentity)
        return CandidateDisposition::Ignore;
    if (context.state == GameDetectionState::Verifying &&
        (incomingTrigger == GameDetectionTrigger::MicrosoftGameIdentity ||
            incomingTrigger == GameDetectionTrigger::GenericForeground))
        return CandidateDisposition::Replace;
    return CandidateDisposition::Ignore;
}

bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept
{
    constexpr std::array<std::wstring_view, 21> rejected{
        L"explorer.exe", L"searchhost.exe", L"shellexperiencehost.exe",
        L"startmenuexperiencehost.exe", L"applicationframehost.exe",
        L"steam.exe", L"steamwebhelper.exe", L"gamebar.exe",
        L"gamebarftserver.exe", L"xboxpcappft.exe", L"gamingservices.exe",
        L"gamingservicesui.exe", L"pickerhost.exe", L"mongmode.exe",
        L"textinputhost.exe", L"chrome.exe", L"msedge.exe",
        L"firefox.exe", L"brave.exe", L"opera.exe", L"vivaldi.exe"};
    for (const auto candidate : rejected)
        if (candidate == image)
            return true;
    return false;
}

bool ShouldRetainCommittedProductionTarget(DWORD committedProcessId,
    bool processAlive) noexcept
{
    return committedProcessId != 0 && processAlive;
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

bool ShouldRestartGraphicsApiProbe(DWORD probedProcessId,
    DWORD committedProcessId) noexcept
{
    return committedProcessId != 0 && probedProcessId != committedProcessId;
}

bool ShouldConsiderForegroundProductionTarget(bool hudEnabled, bool diagnosticRunning,
    bool suspended) noexcept
{
    return hudEnabled && !diagnosticRunning && !suspended;
}
}
