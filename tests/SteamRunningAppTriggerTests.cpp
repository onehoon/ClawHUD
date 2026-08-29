#include "GameDetection/SteamRunningAppTrigger.h"

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
    SteamRunningAppTrigger trigger(coordinator);
    check(trigger.Initialize(0).transition == GameDetectionTransition::None &&
        coordinator.Context().state == GameDetectionState::Idle &&
        coordinator.Context().steamAppId == 0,
        "startup zero leaves coordinator idle");
    check(trigger.Initialize(5010190).transition == GameDetectionTransition::Armed &&
        coordinator.Context().state == GameDetectionState::Armed &&
        coordinator.Context().steamAppId == 5010190 &&
        coordinator.Context().candidateProcessId == 0 &&
        coordinator.Context().generation == 0 &&
        coordinator.Context().evidence.steamSession,
        "startup active AppID arms without a PID");

    const auto beforeSame = coordinator.Context();
    check(trigger.ObserveChange(5010190, 5010190).transition ==
        GameDetectionTransition::None &&
        coordinator.Context().state == beforeSame.state &&
        coordinator.Context().generation == beforeSame.generation &&
        coordinator.Context().steamAppId == beforeSame.steamAppId,
        "unchanged AppID does not mutate coordinator");

    check(trigger.ObserveChange(5010190, 0).transition ==
        GameDetectionTransition::None &&
        coordinator.Context().state == GameDetectionState::Idle &&
        coordinator.Context().steamAppId == 0 &&
        !coordinator.Context().evidence.steamSession,
        "ending an armed session normalizes to idle");

    trigger.Initialize(5010190);
    check(trigger.ObserveChange(5010190, 1234560).transition ==
        GameDetectionTransition::Armed &&
        coordinator.Context().state == GameDetectionState::Armed &&
        coordinator.Context().candidateProcessId == 0 &&
        coordinator.Context().steamAppId == 1234560,
        "changed AppID arms a new session while discovery is idle");

    GameDetectionCoordinator candidateCoordinator;
    SteamRunningAppTrigger candidateTrigger(candidateCoordinator);
    candidateTrigger.Initialize(5010190);
    candidateCoordinator.ObserveCandidate(18812, nullptr,
        GameDetectionTrigger::GenericForeground);
    const auto generation = candidateCoordinator.Context().generation;
    check(candidateTrigger.ObserveChange(5010190, 1234560).transition ==
        GameDetectionTransition::None &&
        candidateCoordinator.Context().state == GameDetectionState::Verifying &&
        candidateCoordinator.Context().candidateProcessId == 18812 &&
        candidateCoordinator.Context().generation == generation &&
        candidateCoordinator.Context().steamAppId == 0,
        "changed AppID does not attach to an active candidate");

    candidateCoordinator.MarkRendererReady(18812, generation);
    candidateCoordinator.CommitCandidate(18812, generation);
    const auto committedGeneration = candidateCoordinator.Context().generation;
    check(candidateTrigger.ObserveChange(0, 0).transition ==
        GameDetectionTransition::None &&
        candidateCoordinator.Context().state == GameDetectionState::Committed &&
        candidateCoordinator.Context().candidateProcessId == 18812 &&
        candidateCoordinator.Context().generation == committedGeneration &&
        candidateCoordinator.Context().steamAppId == 0,
        "unchanged zero AppID does not affect committed target");

    GameDetectionCoordinator committedCoordinator;
    SteamRunningAppTrigger committedTrigger(committedCoordinator);
    committedTrigger.Initialize(5010190);
    committedCoordinator.ObserveCandidate(18812, nullptr,
        GameDetectionTrigger::GenericForeground);
    const auto committedSteamGeneration = committedCoordinator.Context().generation;
    committedCoordinator.MarkRendererReady(18812, committedSteamGeneration);
    committedCoordinator.CommitCandidate(18812, committedSteamGeneration);
    check(committedTrigger.ObserveChange(5010190, 0).transition ==
        GameDetectionTransition::None &&
        committedCoordinator.Context().state == GameDetectionState::Committed &&
        committedCoordinator.Context().candidateProcessId == 18812 &&
        committedCoordinator.Context().generation == committedSteamGeneration &&
        committedCoordinator.Context().steamAppId == 0,
        "clearing Steam does not release committed target");
    check(committedTrigger.ObserveChange(0, 7654321).transition ==
        GameDetectionTransition::None &&
        committedCoordinator.Context().state == GameDetectionState::Committed &&
        committedCoordinator.Context().candidateProcessId == 18812 &&
        committedCoordinator.Context().steamAppId == 0,
        "new Steam wake does not attach to committed target");

    return ok ? 0 : 1;
}
