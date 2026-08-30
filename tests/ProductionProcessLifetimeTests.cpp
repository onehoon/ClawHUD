#include "GameDetection/ProductionProcessLifetime.h"

#include <iostream>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

GameDetectionContext Context(GameDetectionState state, DWORD processId,
    std::uint64_t generation)
{
    GameDetectionContext context{};
    context.state = state;
    context.candidateProcessId = processId;
    context.generation = generation;
    return context;
}
}

int main()
{
    bool ok = true;
    for (const auto state : {GameDetectionState::Verifying,
        GameDetectionState::Ready})
    {
        ok &= Check(DecideProductionProcessExit(Context(state, 8856, 17),
            8856, 17) == ProductionProcessExitAction::ReleaseCandidate,
            "Verifying and Ready exits release the candidate");
    }
    ok &= Check(DecideProductionProcessExit(
        Context(GameDetectionState::Committed, 8856, 17), 8856, 17) ==
        ProductionProcessExitAction::ReleaseCommitted,
        "Committed exit releases the committed target");
    ok &= Check(DecideProductionProcessExit(
        Context(GameDetectionState::Verifying, 9000, 17), 8856, 17) ==
        ProductionProcessExitAction::Ignore &&
        DecideProductionProcessExit(
            Context(GameDetectionState::Verifying, 8856, 18), 8856, 17) ==
        ProductionProcessExitAction::Ignore,
        "stale PID and generation exits are ignored");
    ok &= Check(DecideProductionProcessExit(
        Context(GameDetectionState::Armed, 0, 17), 8856, 17) ==
        ProductionProcessExitAction::Ignore &&
        DecideProductionProcessExit(
            Context(GameDetectionState::Idle, 0, 0), 8856, 0) ==
        ProductionProcessExitAction::Ignore,
        "Idle and Armed without a candidate ignore exits");

    ProductionProcessLifetimeWatcher watcher;
    ok &= Check(!watcher.Arm(0, 1, [](DWORD, std::uint64_t) {}),
        "invalid process cannot arm the watcher");
    ok &= Check(watcher.Arm(GetCurrentProcessId(), 23,
        [](DWORD, std::uint64_t) {}),
        "watcher arms a retained current-process handle");
    ok &= Check(watcher.Armed(), "watcher reports its active wait");
    watcher.Disarm();
    ok &= Check(!watcher.Armed(), "disarm removes the active wait");

    GameDetectionCoordinator steamCoordinator;
    steamCoordinator.ObserveWake({GameDetectionTrigger::SteamRunningAppId,
        0, nullptr, 1868140, false});
    steamCoordinator.ObserveCandidate(8856, nullptr,
        GameDetectionTrigger::SteamRunningAppId);
    const auto steamGeneration = steamCoordinator.Context().generation;
    steamCoordinator.ClearCandidatePreservingSession();
    ok &= Check(steamCoordinator.Context().state == GameDetectionState::Armed &&
        steamCoordinator.Context().steamAppId == 1868140 &&
        steamCoordinator.Context().candidateProcessId == 0 &&
        steamCoordinator.Context().generation != steamGeneration,
        "Steam candidate exit cleanup preserves the Armed session");

    GameDetectionCoordinator genericCoordinator;
    genericCoordinator.ObserveCandidate(8856, nullptr,
        GameDetectionTrigger::GenericForeground);
    genericCoordinator.ClearCandidatePreservingSession();
    ok &= Check(genericCoordinator.Context().state == GameDetectionState::Idle &&
        genericCoordinator.Context().candidateProcessId == 0,
        "non-Steam candidate exit cleanup returns to Idle");
    return ok ? 0 : 1;
}
