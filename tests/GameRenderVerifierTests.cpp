#include "GameDetection/GameRenderVerifier.h"

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

GameRenderVerifierEvent Event(DWORD processId, std::uint64_t generation,
    PresentMonHudEventType type, std::optional<double> fps = {})
{
    return StampPresentMonHudEvent(processId, generation, {type, fps});
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
    const auto first = Event(6008, 7,
        PresentMonHudEventType::FirstDisplayedFrame);
    ok &= Check(first.processId == 6008 && first.generation == 7 &&
        first.event.type == PresentMonHudEventType::FirstDisplayedFrame,
        "FirstDisplayedFrame keeps PID and generation");

    const auto fps = Event(6008, 7, PresentMonHudEventType::FpsUpdate, 59.94);
    ok &= Check(fps.processId == 6008 && fps.generation == 7 &&
        fps.event.displayedFps && *fps.event.displayedFps == 59.94,
        "FpsUpdate keeps PID, generation, and value");

    const auto ended = Event(6008, 7, PresentMonHudEventType::StreamEnded);
    ok &= Check(ended.processId == 6008 && ended.generation == 7 &&
        ended.event.type == PresentMonHudEventType::StreamEnded,
        "StreamEnded keeps PID and generation");

    auto ready = Candidate(6008);
    const auto generation = ready.Context().generation;
    ok &= Check(GameRenderVerifier::ApplyRendererEvidence(
        ready, Event(6008, generation, PresentMonHudEventType::FirstDisplayedFrame)) &&
        ready.Context().state == GameDetectionState::Ready &&
        ready.Context().rendererObserved &&
        ready.Context().state != GameDetectionState::Committed,
        "valid renderer evidence reaches Ready without commit");

    auto fpsOnly = Candidate(6008);
    const auto fpsGeneration = fpsOnly.Context().generation;
    ok &= Check(!GameRenderVerifier::ApplyRendererEvidence(fpsOnly,
        Event(6008, fpsGeneration, PresentMonHudEventType::FpsUpdate, 59.94)) &&
        fpsOnly.Context().state == GameDetectionState::Verifying &&
        !fpsOnly.Context().rendererObserved,
        "FpsUpdate does not change coordinator state");

    auto streamEnded = Candidate(6008);
    const auto endedGeneration = streamEnded.Context().generation;
    ok &= Check(!GameRenderVerifier::ApplyRendererEvidence(streamEnded,
        Event(6008, endedGeneration, PresentMonHudEventType::StreamEnded)) &&
        streamEnded.Context().state == GameDetectionState::Verifying,
        "StreamEnded does not mark Ready");

    auto wrongPid = Candidate(6008);
    const auto wrongPidGeneration = wrongPid.Context().generation;
    ok &= Check(!GameRenderVerifier::ApplyRendererEvidence(wrongPid,
        Event(7000, wrongPidGeneration, PresentMonHudEventType::FirstDisplayedFrame)) &&
        wrongPid.Context().state == GameDetectionState::Verifying,
        "wrong PID is rejected");

    auto wrongGeneration = Candidate(6008);
    const auto currentGeneration = wrongGeneration.Context().generation;
    ok &= Check(!GameRenderVerifier::ApplyRendererEvidence(wrongGeneration,
        Event(6008, currentGeneration + 1,
            PresentMonHudEventType::FirstDisplayedFrame)) &&
        wrongGeneration.Context().state == GameDetectionState::Verifying,
        "wrong generation is rejected");

    auto replaced = Candidate(6008);
    const auto oldGeneration = replaced.Context().generation;
    replaced.ReplaceCandidate(11532, nullptr,
        GameDetectionTrigger::GenericForeground);
    ok &= Check(!GameRenderVerifier::ApplyRendererEvidence(replaced,
        Event(6008, oldGeneration, PresentMonHudEventType::FirstDisplayedFrame)) &&
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
            Event(6008, reusedOldGeneration,
                PresentMonHudEventType::FirstDisplayedFrame)) &&
        reusedPid.Context().state == GameDetectionState::Verifying,
        "same PID with a newer generation rejects old evidence");

    auto duplicate = Candidate(6008);
    const auto duplicateGeneration = duplicate.Context().generation;
    const auto duplicateEvent = Event(6008, duplicateGeneration,
        PresentMonHudEventType::FirstDisplayedFrame);
    ok &= Check(GameRenderVerifier::ApplyRendererEvidence(duplicate, duplicateEvent) &&
        GameRenderVerifier::ApplyRendererEvidence(duplicate, duplicateEvent) &&
        duplicate.Context().state == GameDetectionState::Ready &&
        duplicate.Context().generation == duplicateGeneration,
        "duplicate renderer evidence is idempotent");

    GameRenderVerifier verifier;
    ok &= Check(!verifier.Start({}, 0, 0, {}) && !verifier.Running() &&
        verifier.ProcessId() == 0 && verifier.Generation() == 0,
        "invalid start leaves verifier stopped");
    verifier.Stop();
    ok &= Check(!verifier.Running() && verifier.ProcessId() == 0 &&
        verifier.Generation() == 0, "repeated stop is safe");

    return ok ? 0 : 1;
}
