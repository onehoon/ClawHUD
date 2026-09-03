#pragma once

#include <optional>

#include "ForegroundGameDetector.h"
#include "ProductionGameWindowSource.h"

namespace clawhud
{
enum class ForegroundGameTargetAction
{
    None,
    SetEligible,
    Clear,
};

// Maps a fresh foreground-first evaluation onto the current In-Game Only game
// target. The target is only ever the currently eligible foreground game
// process generation; it is never a retained game lifetime. A different exact
// process generation (PID reuse included) is a target change, not a keep.
inline ForegroundGameTargetAction PlanForegroundGameTargetAction(
    const std::optional<GameProcessInstance>& prior,
    const CurrentForegroundGame& current) noexcept
{
    if (current.decision == ForegroundGameDecision::Eligible && current.process)
        return !prior || *prior != *current.process
            ? ForegroundGameTargetAction::SetEligible
            : ForegroundGameTargetAction::None;
    return prior ? ForegroundGameTargetAction::Clear : ForegroundGameTargetAction::None;
}

// An already-posted renderer completion can arrive after the controller has
// handed the single verifier worker to a newer foreground request. The exact
// process generation carried by that completion is still trustworthy cache
// evidence (R2 TryMarkRendererVerified is generation-safe), so it must always
// be applied; but only the *matching* active adapter request may be cleared.
// A stale completion must never clear or replace a newer active verification.
inline bool RendererCompletionClearsActiveRequest(
    const std::optional<RendererVerificationRequest>& active,
    const RendererVerificationRequest& completed) noexcept
{
    return active && *active == completed;
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

// A permanently excluded executable (ExcludedExecutable) can never become
// eligible on a bounds/layout change. WPF Settings, once classified, emits a
// storm of EVENT_OBJECT_LOCATIONCHANGE while its visual tree animates; running
// full game admission for every one of those is pure waste. Suppress ONLY the
// LOCATIONCHANGE for the same window/PID that is both the authoritative current
// foreground evaluation and the live foreground — every other event (SHOW /
// HIDE / DESTROY / foreground change) and every other decision/reason
// (NotFullscreenLike especially — that is how a windowed game goes fullscreen)
// still reevaluates.
inline bool WindowEventIsRedundantExcludedForegroundLocationChange(
    const ProductionWindowEvent& event, const CurrentForegroundGame& current,
    HWND foregroundWindow, DWORD foregroundProcessId) noexcept
{
    return event.type == ProductionWindowEventType::LocationChange &&
        current.decision == ForegroundGameDecision::Hidden &&
        current.admissionReason == GameScreenAdmissionReason::ExcludedExecutable &&
        current.window != nullptr &&
        event.window == current.window &&
        event.processId == current.processId &&
        event.window == foregroundWindow &&
        event.processId == foregroundProcessId;
}

inline bool ProcessInstanceStillMatches(
    const std::optional<GameProcessInstance>& expected,
    const std::optional<GameProcessInstance>& actual) noexcept
{
    return !expected || (actual && *actual == *expected);
}
}
