#include "GameDetection/GameRenderVerifier.h"

#include "PresentMonTelemetryProvider.h"

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

GameRenderVerifierEvent Event(DWORD processId, std::uint64_t generation)
{
    return MakeGameRenderVerifierEvent(processId, generation,
        GameRenderVerifierEventType::FirstDisplayedFrame);
}

GameDetectionCoordinator Candidate(DWORD processId, HWND window = nullptr)
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveCandidate(processId, window,
        GameDetectionTrigger::GenericForeground);
    return coordinator;
}
}

int main()
{
    bool ok = true;

    const auto first = Event(6008, 7);
    ok &= Check(first.processId == 6008 && first.generation == 7 &&
        first.type == GameRenderVerifierEventType::FirstDisplayedFrame,
        "FirstDisplayedFrame keeps PID and generation");

    auto ready = Candidate(6008);
    const auto generation = ready.Context().generation;
    ok &= Check(GameRenderVerifier::ApplyRendererEvidence(
        ready, Event(6008, generation)) &&
        ready.Context().state == GameDetectionState::Ready &&
        ready.Context().rendererObserved &&
        ready.Context().state != GameDetectionState::Committed,
        "valid renderer evidence reaches Ready without commit");

    auto wrongPid = Candidate(6008);
    const auto wrongPidGeneration = wrongPid.Context().generation;
    ok &= Check(!GameRenderVerifier::ApplyRendererEvidence(wrongPid,
        Event(7000, wrongPidGeneration)) &&
        wrongPid.Context().state == GameDetectionState::Verifying,
        "wrong PID is rejected");

    auto wrongGeneration = Candidate(6008);
    const auto currentGeneration = wrongGeneration.Context().generation;
    ok &= Check(!GameRenderVerifier::ApplyRendererEvidence(wrongGeneration,
        Event(6008, currentGeneration + 1)) &&
        wrongGeneration.Context().state == GameDetectionState::Verifying,
        "wrong generation is rejected");

    auto replaced = Candidate(6008);
    const auto oldGeneration = replaced.Context().generation;
    replaced.ReplaceCandidate(11532, nullptr,
        GameDetectionTrigger::GenericForeground);
    ok &= Check(!GameRenderVerifier::ApplyRendererEvidence(replaced,
        Event(6008, oldGeneration)) &&
        replaced.Context().candidateProcessId == 11532 &&
        replaced.Context().state == GameDetectionState::Verifying,
        "old PID and generation after replacement are rejected");

    auto reusedPid = Candidate(6008);
    const auto reusedOldGeneration = reusedPid.Context().generation;
    reusedPid.Reset();
    reusedPid.ObserveCandidate(6008, nullptr,
        GameDetectionTrigger::GenericForeground);
    ok &= Check(reusedPid.Context().generation != reusedOldGeneration &&
        !GameRenderVerifier::ApplyRendererEvidence(reusedPid,
            Event(6008, reusedOldGeneration)) &&
        reusedPid.Context().state == GameDetectionState::Verifying,
        "same PID with a newer generation rejects old evidence");

    auto duplicate = Candidate(6008);
    const auto duplicateGeneration = duplicate.Context().generation;
    const auto duplicateEvent = Event(6008, duplicateGeneration);
    ok &= Check(GameRenderVerifier::ApplyRendererEvidence(duplicate, duplicateEvent) &&
        GameRenderVerifier::ApplyRendererEvidence(duplicate, duplicateEvent) &&
        duplicate.Context().state == GameDetectionState::Ready &&
        duplicate.Context().generation == duplicateGeneration,
        "duplicate renderer evidence is idempotent");

    // An uninitialized provider cannot lease the target, so Start fails cleanly.
    PresentMonTelemetryProvider provider;
    GameRenderVerifier verifier(provider);
    ok &= Check(!verifier.Start(0, 0, {}) && !verifier.Running() &&
        verifier.ProcessId() == 0 && verifier.Generation() == 0,
        "invalid start leaves the verifier stopped");
    ok &= Check(!verifier.Start(1234, 9,
        [](const GameRenderVerifierEvent&) {}) && !verifier.Running(),
        "start fails when the shared session cannot lease the process");
    verifier.Stop();
    ok &= Check(!verifier.Running() && verifier.ProcessId() == 0 &&
        verifier.Generation() == 0, "repeated stop is safe");

    return ok ? 0 : 1;
}
