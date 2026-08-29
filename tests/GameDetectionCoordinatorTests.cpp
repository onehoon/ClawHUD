#include "GameDetection/GameDetectionCoordinator.h"

#include <iostream>

using namespace clawhud;

int main()
{
    bool ok = true;
    const auto check = [&](bool value, const char* name)
    {
        if (!value)
        {
            std::cerr << "FAILED: " << name << '\n';
            ok = false;
        }
    };

    GameDetectionCoordinator coordinator;
    check(coordinator.Context().state == GameDetectionState::Idle &&
        coordinator.Context().candidateProcessId == 0 &&
        coordinator.Context().generation == 0, "initial state");

    auto transition = coordinator.ObserveWake({
        GameDetectionTrigger::SteamRunningAppId, 0, nullptr, 5010190, false});
    check(transition.transition == GameDetectionTransition::Armed &&
        coordinator.Context().state == GameDetectionState::Armed &&
        coordinator.Context().steamAppId == 5010190 &&
        coordinator.Context().candidateProcessId == 0,
        "Steam wake arms without a PID");

    transition = coordinator.ObserveWake({
        GameDetectionTrigger::GenericForeground, 18812,
        reinterpret_cast<HWND>(0x1234), 0, false});
    const auto generation = coordinator.Context().generation;
    check(transition.transition == GameDetectionTransition::CandidateStarted &&
        coordinator.Context().state == GameDetectionState::Verifying &&
        coordinator.Context().candidateProcessId == 18812 &&
        coordinator.Context().steamAppId == 5010190 &&
        coordinator.Context().evidence.genericForeground &&
        coordinator.Context().evidence.steamSession,
        "Armed wake acquires a candidate");

    transition = coordinator.ObserveCandidate(
        18812, reinterpret_cast<HWND>(0x5678), GameDetectionTrigger::MicrosoftGameIdentity);
    check(transition.transition == GameDetectionTransition::CandidateUpdated &&
        coordinator.Context().generation == generation &&
        coordinator.Context().candidateWindow == reinterpret_cast<HWND>(0x5678) &&
        coordinator.Context().evidence.microsoftGameIdentity &&
        coordinator.Context().microsoftGameIdentity,
        "same PID evidence merges without restart");

    check(!coordinator.MarkRendererReady(18812, generation - 1) &&
        coordinator.Context().state == GameDetectionState::Verifying,
        "wrong generation renderer event is rejected");
    check(!coordinator.MarkRendererReady(100, generation) &&
        coordinator.Context().state == GameDetectionState::Verifying,
        "wrong PID renderer event is rejected");
    check(coordinator.MarkRendererReady(18812, generation) &&
        coordinator.Context().state == GameDetectionState::Ready &&
        coordinator.Context().rendererObserved,
        "renderer ready transition");
    check(coordinator.MarkRendererReady(18812, generation) &&
        coordinator.Context().state == GameDetectionState::Ready,
        "duplicate renderer event is idempotent");
    check(!coordinator.CommitCandidate(18812, generation - 1) &&
        coordinator.Context().state == GameDetectionState::Ready,
        "stale commit is rejected");
    check(coordinator.CommitCandidate(18812, generation) &&
        coordinator.Context().state == GameDetectionState::Committed,
        "ready candidate commits");

    coordinator.ClearSteamSession();
    check(coordinator.Context().state == GameDetectionState::Committed &&
        coordinator.Context().candidateProcessId == 18812 &&
        coordinator.Context().steamAppId == 0,
        "Steam session clears without releasing committed target");

    const auto ignored = coordinator.ObserveCandidate(
        200, nullptr, GameDetectionTrigger::GenericForeground);
    check(ignored.transition == GameDetectionTransition::None &&
        coordinator.Context().candidateProcessId == 18812 &&
        coordinator.Context().generation == generation &&
        coordinator.Context().state == GameDetectionState::Committed,
        "different PID observation does not replace active candidate");

    const auto returned = coordinator.ObserveCandidate(
        18812, reinterpret_cast<HWND>(0x1234),
        GameDetectionTrigger::GenericForeground);
    check(returned.transition == GameDetectionTransition::CandidateUpdated &&
        coordinator.Context().candidateProcessId == 18812 &&
        coordinator.Context().generation == generation &&
        coordinator.Context().state == GameDetectionState::Committed,
        "committed target survives foreground return without a new generation");

    const auto replacement = coordinator.ReplaceCandidate(
        200, nullptr, GameDetectionTrigger::GenericForeground);
    const auto replacementGeneration = coordinator.Context().generation;
    check(replacement.transition == GameDetectionTransition::CandidateReplaced &&
        replacementGeneration > generation &&
        coordinator.Context().candidateProcessId == 200 &&
        coordinator.Context().state == GameDetectionState::Verifying &&
        !coordinator.Context().rendererObserved,
        "explicit candidate replacement increments generation");
    check(!coordinator.Context().microsoftGameIdentity &&
        !coordinator.Context().evidence.microsoftGameIdentity &&
        coordinator.Context().evidence.genericForeground,
        "replacement clears prior candidate evidence");
    check(!coordinator.CommitCandidate(18812, generation) &&
        !coordinator.MarkRendererReady(18812, generation),
        "stale events cannot affect replacement");

    GameDetectionCoordinator clearWithoutSteam;
    clearWithoutSteam.ObserveCandidate(300, nullptr,
        GameDetectionTrigger::GenericForeground);
    const auto clearGeneration = clearWithoutSteam.Context().generation;
    clearWithoutSteam.ClearCandidatePreservingSession();
    check(clearWithoutSteam.Context().state == GameDetectionState::Idle &&
        clearWithoutSteam.Context().candidateProcessId == 0 &&
        clearWithoutSteam.Context().generation != clearGeneration,
        "clear candidate without Steam returns to Idle");

    GameDetectionCoordinator clearWithSteam;
    clearWithSteam.ObserveWake({GameDetectionTrigger::SteamRunningAppId, 0,
        nullptr, 5010190, false});
    clearWithSteam.ObserveCandidate(301, nullptr,
        GameDetectionTrigger::GenericForeground);
    const auto steamGeneration = clearWithSteam.Context().generation;
    clearWithSteam.ClearCandidatePreservingSession();
    check(clearWithSteam.Context().state == GameDetectionState::Armed &&
        clearWithSteam.Context().candidateProcessId == 0 &&
        clearWithSteam.Context().steamAppId == 5010190 &&
        clearWithSteam.Context().evidence.steamSession &&
        clearWithSteam.Context().generation != steamGeneration,
        "clear candidate preserves Steam session");

    GameDetectionCoordinator clearReady;
    clearReady.ObserveCandidate(302, nullptr,
        GameDetectionTrigger::GenericForeground);
    const auto readyGeneration = clearReady.Context().generation;
    clearReady.MarkRendererReady(302, readyGeneration);
    clearReady.ObserveCandidate(302, reinterpret_cast<HWND>(0x6789),
        GameDetectionTrigger::MicrosoftGameIdentity);
    clearReady.ClearCandidatePreservingSession();
    check(clearReady.Context().state == GameDetectionState::Idle &&
        clearReady.Context().candidateProcessId == 0 &&
        clearReady.Context().candidateWindow == nullptr &&
        !clearReady.Context().microsoftGameIdentity &&
        !clearReady.Context().evidence.genericForeground &&
        !clearReady.Context().evidence.microsoftGameIdentity &&
        clearReady.Context().generation != readyGeneration,
        "clear Ready candidate invalidates generation and evidence");

    GameDetectionCoordinator clearCommitted;
    clearCommitted.ObserveCandidate(303, nullptr,
        GameDetectionTrigger::GenericForeground);
    const auto committedGeneration = clearCommitted.Context().generation;
    clearCommitted.MarkRendererReady(303, committedGeneration);
    clearCommitted.CommitCandidate(303, committedGeneration);
    clearCommitted.ClearCandidatePreservingSession();
    check(clearCommitted.Context().state == GameDetectionState::Idle &&
        clearCommitted.Context().candidateProcessId == 0,
        "clear Committed candidate releases ownership");
    check(!clearCommitted.MarkRendererReady(303, committedGeneration),
        "old verifier event is rejected after clear");

    GameDetectionCoordinator invalidWake;
    const auto invalidBefore = invalidWake.Context();
    const auto sameContext = [](const auto& left, const auto& right)
    {
        return left.state == right.state && left.generation == right.generation &&
            left.candidateProcessId == right.candidateProcessId &&
            left.candidateWindow == right.candidateWindow &&
            left.steamAppId == right.steamAppId &&
            left.microsoftGameIdentity == right.microsoftGameIdentity &&
            left.rendererObserved == right.rendererObserved &&
            left.primaryTrigger == right.primaryTrigger &&
            left.evidence.genericForeground == right.evidence.genericForeground &&
            left.evidence.steamSession == right.evidence.steamSession &&
            left.evidence.microsoftGameIdentity == right.evidence.microsoftGameIdentity;
    };
    check(invalidWake.ObserveWake({GameDetectionTrigger::GenericForeground, 0,
        nullptr, 5010190, false}).transition == GameDetectionTransition::None &&
        sameContext(invalidWake.Context(), invalidBefore),
        "zero-PID generic wake leaves context unchanged");
    check(invalidWake.ObserveWake({GameDetectionTrigger::MicrosoftGameIdentity, 0,
        nullptr, 0, true}).transition == GameDetectionTransition::None &&
        sameContext(invalidWake.Context(), invalidBefore),
        "zero-PID MicrosoftGame wake leaves context unchanged");

    const auto reset = coordinator.Reset();
    check(reset.transition == GameDetectionTransition::Reset &&
        coordinator.Context().state == GameDetectionState::Idle &&
        coordinator.Context().candidateProcessId == 0 &&
        coordinator.Context().candidateWindow == nullptr &&
        coordinator.Context().steamAppId == 0 &&
        !coordinator.Context().microsoftGameIdentity &&
        !coordinator.Context().rendererObserved &&
        coordinator.Context().generation > replacementGeneration,
        "reset clears context and invalidates generation");

    return ok ? 0 : 1;
}
