#pragma once

#include <windows.h>

// Pure, stateless suspend/resume recovery policy. Extracted from App.h so the
// decisions and their tests do not depend on the whole App class. The runtime
// orchestration (App::HandleSystemSuspend / HandleSystemResume / TryResumeRecovery
// and the suspended_ / resumeRecoveryActive_ / resumeRecoveryAttempts_ state)
// stays in App. The Win32 timer id (kResumeRecoveryTimerId) is message-loop
// wiring and stays with the other runtime ids in App.h.
namespace clawhud
{
inline constexpr UINT kResumeRecoveryIntervalMs = 500;
inline constexpr unsigned kResumeRecoveryMaxAttempts = 6;

constexpr bool ResumeRecoveryShouldStart(bool active) noexcept
{
    return !active;
}

constexpr bool ResumeRecoveryNeedsSuspendFallback(bool suspended) noexcept
{
    return !suspended;
}

constexpr bool ResumeRecoveryHasAttemptsRemaining(unsigned attempts) noexcept
{
    return attempts < kResumeRecoveryMaxAttempts;
}

constexpr bool ResumeRecoveryCanRetainVerifier(
    DWORD trackedProcessId, DWORD verifierProcessId, bool running) noexcept
{
    return trackedProcessId != 0 && trackedProcessId == verifierProcessId && running;
}

constexpr bool ResumeRecoveryShouldWaitForForeground(
    bool hudEnabled, bool visibilityUsesForeground, bool processAlive,
    bool foregroundMatches, unsigned attempts) noexcept
{
    return hudEnabled && visibilityUsesForeground && processAlive &&
        !foregroundMatches && ResumeRecoveryHasAttemptsRemaining(attempts);
}

constexpr bool ResumeRecoveryMayShowHud(bool expectedVisible, bool freshFrameReady) noexcept
{
    return !expectedVisible || freshFrameReady;
}

constexpr bool ResumeRecoveryFrameWasPresented(HRESULT renderResult) noexcept
{
    return renderResult == S_OK;
}
}
