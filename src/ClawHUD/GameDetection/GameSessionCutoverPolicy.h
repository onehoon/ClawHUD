#pragma once

#include <optional>

#include "ForegroundGameDetector.h"
#include "ProductionGameWindowSource.h"

namespace clawhud
{
enum class CompatibilityTargetAction
{
    None,
    SetEligible,
    Clear,
};

// R4 keeps the old App hooks temporarily, but their target is only the
// currently eligible foreground game; it is never a retained game lifetime.
inline CompatibilityTargetAction PlanCompatibilityTargetAction(
    const std::optional<GameProcessInstance>& prior,
    const CurrentForegroundGame& current) noexcept
{
    if (current.decision == ForegroundGameDecision::Eligible && current.process)
        return !prior || *prior != *current.process
            ? CompatibilityTargetAction::SetEligible
            : CompatibilityTargetAction::None;
    return prior ? CompatibilityTargetAction::Clear : CompatibilityTargetAction::None;
}

inline bool ShouldStartRendererVerification(
    const std::optional<RendererVerificationRequest>& active,
    bool verifierRunning,
    const std::optional<RendererVerificationRequest>& requested) noexcept
{
    return requested && (!active || *active != *requested || !verifierRunning);
}

inline bool WindowEventAffectsCurrentScreen(const ProductionWindowEvent& event,
    HWND foregroundWindow, DWORD foregroundProcessId,
    HWND currentWindow, DWORD currentProcessId) noexcept
{
    if (event.type == ProductionWindowEventType::Create)
        return false;
    return (event.window == foregroundWindow && event.processId == foregroundProcessId) ||
        (event.window == currentWindow && event.processId == currentProcessId);
}

inline bool ProcessInstanceStillMatches(
    const std::optional<GameProcessInstance>& expected,
    const std::optional<GameProcessInstance>& actual) noexcept
{
    return !expected || (actual && *actual == *expected);
}
}
