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

    const auto replacement = coordinator.ObserveCandidate(
        200, nullptr, GameDetectionTrigger::GenericForeground);
    const auto replacementGeneration = coordinator.Context().generation;
    check(replacement.transition == GameDetectionTransition::CandidateReplaced &&
        replacementGeneration > generation &&
        coordinator.Context().candidateProcessId == 200 &&
        coordinator.Context().state == GameDetectionState::Verifying &&
        !coordinator.Context().rendererObserved,
        "candidate replacement increments generation");
    check(!coordinator.CommitCandidate(18812, generation) &&
        !coordinator.MarkRendererReady(18812, generation),
        "stale events cannot affect replacement");

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
