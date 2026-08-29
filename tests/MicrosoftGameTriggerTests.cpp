#include "GameDetection/MicrosoftGameTrigger.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

clawhud::ProductionWindowEvent WindowEvent(
    clawhud::ProductionWindowEventType type, DWORD processId = 6008,
    bool immediateTopLevel = true)
{
    clawhud::ProductionWindowEvent event;
    event.sequence = 123;
    event.type = type;
    event.window = reinterpret_cast<HWND>(0x1234);
    event.processId = processId;
    event.immediateRoot = event.window;
    event.immediateTopLevel = immediateTopLevel;
    return event;
}

clawhud::WindowsGameIdentityProbeResult IdentityResult(
    bool readable, bool evaluated, bool matched)
{
    clawhud::WindowsGameIdentityProbeResult result;
    result.microsoftGameConfigs.push_back({
        1, L"package", L"package\\MicrosoftGame.config", true, true, 0,
        true, readable, 0, {}, evaluated, matched});
    return result;
}
}

int main()
{
    using namespace clawhud;
    Check(ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::Create)), "CREATE is eligible");
    Check(ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::Show)), "SHOW is eligible");
    Check(!ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::Destroy)), "DESTROY is ignored");
    Check(!ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::Create, 0)), "zero PID is ignored");
    Check(!ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::Create, 6008, false)),
        "non-top-level event is ignored");
    auto vanished = WindowEvent(ProductionWindowEventType::Create);
    vanished.window = reinterpret_cast<HWND>(0xDEAD);
    Check(ShouldInspectMicrosoftGameWindowEvent(vanished),
        "callback-time top-level evidence does not require a live HWND");

    Check(!ShouldEmitMicrosoftGameTrigger(IdentityResult(false, true, true)),
        "unreadable config is not evidence");
    Check(!ShouldEmitMicrosoftGameTrigger(IdentityResult(true, false, true)),
        "unevaluated match is not evidence");
    Check(!ShouldEmitMicrosoftGameTrigger(IdentityResult(true, true, false)),
        "non-matching executable is not evidence");
    Check(ShouldEmitMicrosoftGameTrigger(IdentityResult(true, true, true)),
        "readable exact executable match is evidence");

    const MicrosoftGameTriggerEvidence first{
        10, reinterpret_cast<HWND>(0x1234), 6008};
    GameDetectionCoordinator coordinator;
    auto transition = MicrosoftGameTrigger::ApplyEvidence(coordinator, first);
    const auto generation = coordinator.Context().generation;
    Check(transition.transition == GameDetectionTransition::CandidateStarted &&
        coordinator.Context().state == GameDetectionState::Verifying &&
        coordinator.Context().candidateProcessId == 6008 &&
        coordinator.Context().candidateWindow == first.window &&
        coordinator.Context().microsoftGameIdentity &&
        coordinator.Context().evidence.microsoftGameIdentity && generation != 0,
        "positive evidence starts a verifying MicrosoftGame candidate");

    auto repeated = first;
    repeated.sourceSequence = 11;
    repeated.window = reinterpret_cast<HWND>(0x5678);
    transition = MicrosoftGameTrigger::ApplyEvidence(coordinator, repeated);
    Check(transition.transition == GameDetectionTransition::CandidateUpdated &&
        coordinator.Context().generation == generation &&
        coordinator.Context().candidateProcessId == 6008 &&
        coordinator.Context().candidateWindow == repeated.window,
        "same PID evidence merges without restart");

    const auto different = MicrosoftGameTrigger::ApplyEvidence(coordinator,
        {12, reinterpret_cast<HWND>(0x9999), 7000});
    Check(different.transition == GameDetectionTransition::None &&
        coordinator.Context().candidateProcessId == 6008 &&
        coordinator.Context().generation == generation,
        "different PID evidence does not replace candidate");

    GameDetectionCoordinator committed;
    MicrosoftGameTrigger::ApplyEvidence(committed, first);
    const auto committedGeneration = committed.Context().generation;
    Check(committed.MarkRendererReady(6008, committedGeneration),
        "candidate becomes ready for committed-target test");
    Check(committed.CommitCandidate(6008, committedGeneration),
        "candidate becomes committed for committed-target test");
    MicrosoftGameTrigger::ApplyEvidence(committed,
        {13, reinterpret_cast<HWND>(0xAAAA), 7000});
    Check(committed.Context().state == GameDetectionState::Committed &&
        committed.Context().candidateProcessId == 6008 &&
        committed.Context().generation == committedGeneration,
        "committed target is not replaced");

    MicrosoftGameTrigger source;
    Check(!source.InspectWindowEvent(WindowEvent(
        ProductionWindowEventType::Destroy)), "destroy does not probe");
    std::cout << "PASS\n";
}
