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

clawhud::ProductionWindowEvent InstanceEvent(
    std::uint64_t sequence, HWND window = reinterpret_cast<HWND>(0x1234))
{
    auto event = WindowEvent(clawhud::ProductionWindowEventType::Show);
    event.sequence = sequence;
    event.window = window;
    return event;
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

    Check(IsSameMicrosoftGameProcessInstance({100, 1000}, {100, 1000}),
        "equal PID and creation time identify one process instance");
    Check(!IsSameMicrosoftGameProcessInstance({100, 1000}, {101, 1000}) &&
        !IsSameMicrosoftGameProcessInstance({100, 1000}, {100, 2000}),
        "PID or creation-time changes identify a different process instance");
    const auto currentProcessIdentity =
        QueryMicrosoftGameProcessIdentity(GetCurrentProcessId());
    Check(currentProcessIdentity &&
        currentProcessIdentity->processId == GetCurrentProcessId() &&
        currentProcessIdentity->creationTime != 0,
        "process identity query reads creation time from the process handle");

    int positiveProbeCalls = 0;
    MicrosoftGameTrigger cached(
        [&](DWORD) { ++positiveProbeCalls; return IdentityResult(true, true, true); },
        [](DWORD processId) -> std::optional<MicrosoftGameProcessIdentity>
        {
            return MicrosoftGameProcessIdentity{processId, 1000};
        });
    const auto firstEvidence = cached.InspectWindowEvent(InstanceEvent(10));
    const auto secondEvidence = cached.InspectWindowEvent(
        InstanceEvent(11, reinterpret_cast<HWND>(0x5678)));
    Check(firstEvidence && secondEvidence && positiveProbeCalls == 1 &&
        secondEvidence->sourceSequence == 11 &&
        secondEvidence->window == reinterpret_cast<HWND>(0x5678),
        "positive process identity cache skips repeated probe and keeps event evidence");

    int reusedProbeCalls = 0;
    std::uint64_t creationTime = 1000;
    MicrosoftGameTrigger reused(
        [&](DWORD) { ++reusedProbeCalls; return IdentityResult(true, true, true); },
        [&](DWORD processId) -> std::optional<MicrosoftGameProcessIdentity>
        {
            return MicrosoftGameProcessIdentity{processId, creationTime};
        });
    Check(reused.InspectWindowEvent(InstanceEvent(20)) &&
        (creationTime = 2000, reused.InspectWindowEvent(InstanceEvent(21))) &&
        reusedProbeCalls == 2,
        "PID reuse with a different creation time requires a new probe");

    int negativeProbeCalls = 0;
    MicrosoftGameTrigger negative(
        [&](DWORD) { ++negativeProbeCalls; return IdentityResult(true, true, false); },
        [](DWORD processId) -> std::optional<MicrosoftGameProcessIdentity>
        {
            return MicrosoftGameProcessIdentity{processId, 3000};
        });
    Check(!negative.InspectWindowEvent(InstanceEvent(30)) &&
        !negative.InspectWindowEvent(InstanceEvent(31)) &&
        negativeProbeCalls == 2,
        "negative Microsoft identity results are not cached");

    int identityFailureProbeCalls = 0;
    MicrosoftGameTrigger identityFailure(
        [&](DWORD) { ++identityFailureProbeCalls; return IdentityResult(true, true, true); },
        [](DWORD) -> std::optional<MicrosoftGameProcessIdentity>
        {
            return std::nullopt;
        });
    Check(identityFailure.InspectWindowEvent(InstanceEvent(40)) &&
        identityFailure.InspectWindowEvent(InstanceEvent(41)) &&
        identityFailureProbeCalls == 2,
        "identity query failure falls back without unsafe caching");

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
