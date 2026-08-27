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
bool ShouldSampleProductionPresentMon(DWORD pendingProcessId,
    bool foregroundIsTrackedProcess) noexcept;
bool ShouldPreservePendingProductionValidation(DWORD pendingProcessId,
    DWORD presentMonProcessId, bool presentMonRunning) noexcept;
bool ShouldCancelPendingCandidateOnCommittedReturn(DWORD committedProcessId,
    DWORD pendingProcessId, DWORD foregroundProcessId) noexcept;
bool ShouldConfirmProductionTarget(DWORD pendingProcessId, DWORD observedProcessId,
    DWORD foregroundProcessId, bool displayedFpsAvailable) noexcept;
bool ShouldConsiderForegroundProductionTarget(bool hudEnabled, bool diagnosticRunning,
    bool suspended) noexcept;
}
