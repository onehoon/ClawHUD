#pragma once

#include <windows.h>

#include <string_view>

namespace clawhud
{
bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept;
bool ShouldEvaluateForegroundCandidate(DWORD committedProcessId,
    DWORD foregroundProcessId) noexcept;
bool ShouldReplacePendingCandidate(DWORD pendingProcessId,
    DWORD foregroundProcessId) noexcept;
DWORD SelectProductionSamplingProcess(DWORD trackedProcessId,
    DWORD pendingProcessId) noexcept;
bool ShouldSampleProductionPresentMon(DWORD committedProcessId,
    DWORD pendingProcessId, bool committedProcessAlive) noexcept;
bool ShouldRetainCommittedProductionTarget(DWORD committedProcessId,
    bool processAlive) noexcept;
bool ShouldPreservePendingProductionValidation(DWORD pendingProcessId,
    DWORD presentMonProcessId, bool presentMonRunning) noexcept;
bool ShouldDeferPendingProductionValidation(DWORD pendingProcessId,
    DWORD foregroundProcessId, bool foregroundUsable,
    bool candidateProcessAlive) noexcept;
bool ShouldRetryProductionPresentMon(DWORD retryProcessId,
    unsigned retryAttempts, DWORD processId) noexcept;
bool ShouldAllowProductionPresentMonStart(DWORD committedProcessId,
    DWORD processId, DWORD retryProcessId, unsigned retryAttempts,
    bool recoveryStart) noexcept;
bool ShouldReevaluateForegroundAfterResume(bool hudEnabled,
    bool recoveryCompleted) noexcept;
bool ShouldReevaluateForegroundAfterDiagnostic(bool hudEnabled,
    bool diagnosticRunning, bool suspended) noexcept;
bool ShouldCancelPendingCandidateOnCommittedReturn(DWORD committedProcessId,
    DWORD pendingProcessId, DWORD foregroundProcessId) noexcept;
bool ShouldRestartGraphicsApiProbe(DWORD probedProcessId,
    DWORD committedProcessId) noexcept;
bool ShouldConfirmProductionTarget(DWORD pendingProcessId, DWORD observedProcessId,
    DWORD foregroundProcessId, bool displayedFpsAvailable) noexcept;
bool ShouldConsiderForegroundProductionTarget(bool hudEnabled, bool diagnosticRunning,
    bool suspended) noexcept;
}
