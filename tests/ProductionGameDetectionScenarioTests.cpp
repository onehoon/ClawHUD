#include "GameDetection/GameDetectionCoordinator.h"
#include "GameDetection/GameRenderVerifier.h"
#include "GameDetection/GenericForegroundTrigger.h"
#include "GameDetection/MicrosoftGameTrigger.h"
#include "GameDetection/SteamRunningAppTrigger.h"
#include "GameDetection/ProductionGameWindowSource.h"
#include "ProductionTargetPolicy.h"

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

GameRenderVerifierEvent FirstFrame(DWORD processId, std::uint64_t generation)
{
    return StampPresentMonHudEvent(processId, generation,
        {PresentMonHudEventType::FirstDisplayedFrame, {}});
}

bool Commit(GameDetectionCoordinator& coordinator, DWORD processId)
{
    const auto generation = coordinator.Context().generation;
    return ShouldCommitReadyCandidate(
        coordinator.Context(), processId, true) &&
        coordinator.CommitCandidate(processId, generation);
}

bool GenericScenario()
{
    GameDetectionCoordinator coordinator;
    GenericForegroundTrigger::ApplyEvidence(coordinator, {nullptr, 11532});
    const auto generation = coordinator.Context().generation;
    const bool ready = GameRenderVerifier::ApplyRendererEvidence(
        coordinator, FirstFrame(11532, generation));
    return Check(ready && coordinator.Context().state == GameDetectionState::Ready &&
        coordinator.Context().candidateProcessId == 11532 &&
        coordinator.Context().evidence.genericForeground &&
        !coordinator.Context().evidence.steamSession &&
        !coordinator.Context().evidence.microsoftGameIdentity &&
        Commit(coordinator, 11532) &&
        coordinator.Context().generation == generation &&
        coordinator.Context().state == GameDetectionState::Committed,
        "generic Win32 game reaches Committed");
}

bool GenericReplacementScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveCandidate(100, nullptr, GameDetectionTrigger::GenericForeground);
    const auto oldGeneration = coordinator.Context().generation;
    const auto oldEvent = FirstFrame(100, oldGeneration);
    coordinator.ReplaceCandidate(200, nullptr, GameDetectionTrigger::GenericForeground);
    const auto newGeneration = coordinator.Context().generation;
    const bool stale = !GameRenderVerifier::ApplyRendererEvidence(coordinator, oldEvent);
    const bool ready = GameRenderVerifier::ApplyRendererEvidence(
        coordinator, FirstFrame(200, newGeneration));
    return Check(newGeneration != oldGeneration && stale && ready &&
        coordinator.Context().candidateProcessId == 200 &&
        Commit(coordinator, 200), "generic launcher replacement");
}

bool SteamScenario()
{
    GameDetectionCoordinator coordinator;
    SteamRunningAppTrigger steam(coordinator);
    const bool armed = steam.Initialize(5010190).transition == GameDetectionTransition::Armed;
    GenericForegroundTrigger::ApplyEvidence(coordinator, {nullptr, 18812});
    const auto generation = coordinator.Context().generation;
    GameRenderVerifier::ApplyRendererEvidence(
        coordinator, FirstFrame(18812, generation));
    return Check(armed && coordinator.Context().steamAppId == 5010190 &&
        coordinator.Context().evidence.steamSession &&
        coordinator.Context().evidence.genericForeground &&
        coordinator.Context().state == GameDetectionState::Ready &&
        Commit(coordinator, 18812), "Steam Armed to Committed");
}

bool SteamClearScenario()
{
    GameDetectionCoordinator coordinator;
    SteamRunningAppTrigger steam(coordinator);
    steam.Initialize(5010190);
    coordinator.ObserveCandidate(18812, nullptr, GameDetectionTrigger::GenericForeground);
    const auto generation = coordinator.Context().generation;
    coordinator.MarkRendererReady(18812, generation);
    coordinator.CommitCandidate(18812, generation);
    steam.ObserveChange(5010190, 0);
    return Check(coordinator.Context().state == GameDetectionState::Committed &&
        coordinator.Context().candidateProcessId == 18812 &&
        coordinator.Context().generation == generation &&
        coordinator.Context().steamAppId == 0 &&
        !coordinator.Context().evidence.steamSession,
        "Steam AppID clear retains committed target");
}

bool AltTabScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveCandidate(100, nullptr, GameDetectionTrigger::GenericForeground);
    const auto generation = coordinator.Context().generation;
    coordinator.MarkRendererReady(100, generation);
    coordinator.CommitCandidate(100, generation);
    const auto leave = DecideCandidateDisposition(
        coordinator.Context(), GameDetectionTrigger::GenericForeground, 200);
    const auto back = DecideCandidateDisposition(
        coordinator.Context(), GameDetectionTrigger::GenericForeground, 100);
    return Check(leave == CandidateDisposition::Ignore &&
        back == CandidateDisposition::Merge &&
        coordinator.Context().candidateProcessId == 100 &&
        coordinator.Context().generation == generation &&
        coordinator.Context().state == GameDetectionState::Committed,
        "Alt+Tab retains committed target");
}

bool MicrosoftEarlyScenario()
{
    GameDetectionCoordinator coordinator;
    MicrosoftGameTrigger::ApplyEvidence(coordinator, {1, nullptr, 6008});
    const auto generation = coordinator.Context().generation;
    const bool ready = GameRenderVerifier::ApplyRendererEvidence(
        coordinator, FirstFrame(6008, generation));
    const bool helperIgnored = DecideCandidateDisposition(
        coordinator.Context(), GameDetectionTrigger::GenericForeground, 7000) ==
        CandidateDisposition::Ignore;
    return Check(ready && helperIgnored && coordinator.Context().state == GameDetectionState::Ready &&
        coordinator.Context().candidateProcessId == 6008 &&
        coordinator.Context().evidence.microsoftGameIdentity &&
        Commit(coordinator, 6008), "MicrosoftGame early identity and commit");
}

bool MicrosoftReplacementScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveCandidate(100, nullptr, GameDetectionTrigger::GenericForeground);
    const auto oldGeneration = coordinator.Context().generation;
    const bool replace = DecideCandidateDisposition(
        coordinator.Context(), GameDetectionTrigger::MicrosoftGameIdentity, 200) ==
        CandidateDisposition::Replace;
    coordinator.ReplaceCandidate(200, nullptr, GameDetectionTrigger::MicrosoftGameIdentity);
    return Check(replace && coordinator.Context().candidateProcessId == 200 &&
        coordinator.Context().generation != oldGeneration &&
        coordinator.Context().evidence.microsoftGameIdentity &&
        !GameRenderVerifier::ApplyRendererEvidence(
            coordinator, FirstFrame(100, oldGeneration)),
        "MicrosoftGame replaces generic helper");
}

bool MicrosoftProtectionScenario()
{
    GameDetectionCoordinator coordinator;
    MicrosoftGameTrigger::ApplyEvidence(coordinator, {1, nullptr, 100});
    const auto generation = coordinator.Context().generation;
    return Check(DecideCandidateDisposition(
        coordinator.Context(), GameDetectionTrigger::GenericForeground, 200) ==
        CandidateDisposition::Ignore &&
        DecideCandidateDisposition(
            coordinator.Context(), GameDetectionTrigger::MicrosoftGameIdentity, 200) ==
        CandidateDisposition::Ignore &&
        coordinator.Context().candidateProcessId == 100 &&
        coordinator.Context().generation == generation,
        "MicrosoftGame candidate is protected");
}

bool ReadyProtectionScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveCandidate(100, nullptr, GameDetectionTrigger::MicrosoftGameIdentity);
    const auto generation = coordinator.Context().generation;
    coordinator.MarkRendererReady(100, generation);
    const bool protectedCandidate = DecideCandidateDisposition(
        coordinator.Context(), GameDetectionTrigger::GenericForeground, 200) ==
        CandidateDisposition::Ignore;
    return Check(protectedCandidate && coordinator.Context().state == GameDetectionState::Ready &&
        Commit(coordinator, 100), "Ready candidate is protected");
}

bool ClearScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveCandidate(100, nullptr, GameDetectionTrigger::GenericForeground);
    const auto generation = coordinator.Context().generation;
    coordinator.MarkRendererReady(100, generation);
    coordinator.CommitCandidate(100, generation);
    coordinator.ClearCandidatePreservingSession();
    return Check(coordinator.Context().state == GameDetectionState::Idle &&
        coordinator.Context().candidateProcessId == 0 &&
        coordinator.Context().generation != generation &&
        !coordinator.MarkRendererReady(100, generation),
        "committed process exit clears target and stale events");
}

bool SteamCandidateExitScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveWake({GameDetectionTrigger::SteamRunningAppId, 0,
        nullptr, 5010190, false});
    coordinator.ObserveCandidate(100, nullptr, GameDetectionTrigger::GenericForeground);
    coordinator.ClearCandidatePreservingSession();
    return Check(coordinator.Context().state == GameDetectionState::Armed &&
        coordinator.Context().steamAppId == 5010190 &&
        coordinator.Context().candidateProcessId == 0 &&
        coordinator.Context().evidence.steamSession,
        "candidate exit preserves Steam session");
}

bool SteamWindowCandidateScenario(ProductionWindowEventType eventType)
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveWake({GameDetectionTrigger::SteamRunningAppId, 0,
        nullptr, 1868140, false});
    const ProductionWindowEvent event{
        1, eventType, reinterpret_cast<HWND>(0x1234), 8856, 0,
        reinterpret_cast<HWND>(0x1234), true, 0, 0};
    const auto transition = coordinator.ObserveWake({
        GameDetectionTrigger::SteamRunningAppId, event.processId,
        event.window, coordinator.Context().steamAppId, false});
    return Check(transition.transition == GameDetectionTransition::CandidateStarted &&
        coordinator.Context().state == GameDetectionState::Verifying &&
        coordinator.Context().candidateProcessId == event.processId &&
        coordinator.Context().steamAppId == 1868140 &&
        coordinator.Context().evidence.steamSession &&
        !coordinator.Context().evidence.genericForeground &&
        coordinator.Context().generation != 0,
        eventType == ProductionWindowEventType::Create
            ? "Steam Armed CREATE seeds a Steam candidate"
            : "Steam Armed SHOW seeds a Steam candidate");
}

bool SteamWindowCandidateProtectionScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveWake({GameDetectionTrigger::SteamRunningAppId, 0,
        nullptr, 1868140, false});
    coordinator.ObserveWake({GameDetectionTrigger::SteamRunningAppId, 100,
        reinterpret_cast<HWND>(0x100), 1868140, false});
    const auto generation = coordinator.Context().generation;
    coordinator.ObserveWake({GameDetectionTrigger::SteamRunningAppId, 200,
        reinterpret_cast<HWND>(0x200), 1868140, false});
    const bool verifyingProtected = coordinator.Context().candidateProcessId == 100 &&
        coordinator.Context().generation == generation;
    coordinator.MarkRendererReady(100, generation);
    coordinator.ObserveWake({GameDetectionTrigger::SteamRunningAppId, 200,
        reinterpret_cast<HWND>(0x200), 1868140, false});
    const bool readyProtected = coordinator.Context().candidateProcessId == 100;
    coordinator.CommitCandidate(100, generation);
    coordinator.ObserveWake({GameDetectionTrigger::SteamRunningAppId, 200,
        reinterpret_cast<HWND>(0x200), 1868140, false});
    return Check(verifyingProtected && readyProtected &&
        coordinator.Context().candidateProcessId == 100 &&
        coordinator.Context().state == GameDetectionState::Committed,
        "Steam window discovery never replaces an active candidate");
}

bool SteamWindowGenericMergeScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveWake({GameDetectionTrigger::SteamRunningAppId, 0,
        nullptr, 1868140, false});
    coordinator.ObserveWake({GameDetectionTrigger::SteamRunningAppId, 8856,
        reinterpret_cast<HWND>(0x1234), 1868140, false});
    const auto generation = coordinator.Context().generation;
    GenericForegroundTrigger::ApplyEvidence(coordinator,
        {reinterpret_cast<HWND>(0x1234), 8856});
    return Check(coordinator.Context().generation == generation &&
        coordinator.Context().evidence.steamSession &&
        coordinator.Context().evidence.genericForeground,
        "same-PID Generic evidence merges with Steam candidate");
}

bool SteamSessionArmRequiresAppIdScenario()
{
    GameDetectionCoordinator coordinator;
    const auto transition = coordinator.ObserveWake({
        GameDetectionTrigger::SteamRunningAppId, 0, nullptr, 0, false});
    return Check(transition.transition == GameDetectionTransition::None &&
        coordinator.Context().state == GameDetectionState::Idle &&
        coordinator.Context().candidateProcessId == 0,
        "Steam session arming requires a nonzero AppID");
}

bool SteamWindowAdmissionRegressionScenario()
{
    return Check(IsRejectedProductionTargetImage(L"steam.exe") &&
        IsRejectedProductionTargetImage(L"steamwebhelper.exe") &&
        IsRejectedProductionTargetImage(L"steamservice.exe") &&
        IsRejectedProductionTargetImage(L"gameoverlayui.exe") &&
        IsRejectedProductionTargetImage(L"steaminputaddonforclaw.ui.exe") &&
        IsRejectedProductionTargetImage(L"msi center m.exe") &&
        IsRejectedProductionTargetImage(L"mcmosdinfo.exe") &&
        IsRejectedProductionTargetImage(L"xboxpcapp.exe") &&
        IsRejectedProductionTargetImage(L"chrome.exe") &&
        IsRejectedProductionTargetImage(L"msedge.exe"),
        "central admission rejects representative infrastructure targets");
}

bool MergeScenario()
{
    GameDetectionCoordinator coordinator;
    GenericForegroundTrigger::ApplyEvidence(coordinator, {nullptr, 100});
    const auto generation = coordinator.Context().generation;
    MicrosoftGameTrigger::ApplyEvidence(coordinator, {2, nullptr, 100});
    return Check(coordinator.Context().generation == generation &&
        coordinator.Context().evidence.genericForeground &&
        coordinator.Context().evidence.microsoftGameIdentity &&
        coordinator.Context().candidateProcessId == 100,
        "same PID Generic and Microsoft evidence merges");
}

bool SamePidUpdatePreservesStateScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveCandidate(6008, nullptr,
        GameDetectionTrigger::MicrosoftGameIdentity);
    const auto generation = coordinator.Context().generation;
    coordinator.MarkRendererReady(6008, generation);

    const auto readyUpdate = coordinator.ObserveCandidate(
        6008, reinterpret_cast<HWND>(0x100),
        GameDetectionTrigger::MicrosoftGameIdentity);
    const bool readyPreserved = readyUpdate.transition ==
            GameDetectionTransition::CandidateUpdated &&
        coordinator.Context().state == GameDetectionState::Ready &&
        coordinator.Context().generation == generation;

    coordinator.CommitCandidate(6008, generation);
    const auto committedUpdate = coordinator.ObserveCandidate(
        6008, reinterpret_cast<HWND>(0x200),
        GameDetectionTrigger::MicrosoftGameIdentity);
    return Check(readyPreserved && committedUpdate.transition ==
            GameDetectionTransition::CandidateUpdated &&
        coordinator.Context().state == GameDetectionState::Committed &&
        coordinator.Context().generation == generation,
        "same-PID evidence updates preserve Ready and Committed state");
}

bool RendererSignalScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveCandidate(100, nullptr, GameDetectionTrigger::GenericForeground);
    const auto generation = coordinator.Context().generation;
    const bool firstFrame = GameRenderVerifier::ApplyRendererEvidence(
        coordinator, FirstFrame(100, generation));
    return Check(firstFrame &&
        coordinator.Context().state == GameDetectionState::Ready,
        "FirstDisplayedFrame is the readiness signal");
}

bool ReadyCommitScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveCandidate(100, nullptr, GameDetectionTrigger::GenericForeground);
    const auto generation = coordinator.Context().generation;
    coordinator.MarkRendererReady(100, generation);
    const bool noCommitWithoutForeground = !ShouldCommitReadyCandidate(
        coordinator.Context(), 200, true);
    const bool commitWithForeground = ShouldCommitReadyCandidate(
        coordinator.Context(), 100, true);
    return Check(noCommitWithoutForeground && commitWithForeground &&
        coordinator.Context().state == GameDetectionState::Ready &&
        coordinator.Context().generation == generation,
        "Ready does not commit without foreground revalidation");
}

bool StaleGenerationScenario()
{
    GameDetectionCoordinator coordinator;
    coordinator.ObserveCandidate(100, nullptr, GameDetectionTrigger::GenericForeground);
    const auto oldGeneration = coordinator.Context().generation;
    coordinator.ReplaceCandidate(200, nullptr, GameDetectionTrigger::GenericForeground);
    const auto newGeneration = coordinator.Context().generation;
    coordinator.ClearCandidatePreservingSession();
    coordinator.ObserveCandidate(100, nullptr, GameDetectionTrigger::GenericForeground);
    return Check(newGeneration != oldGeneration &&
        coordinator.Context().generation != oldGeneration &&
        !GameRenderVerifier::ApplyRendererEvidence(
            coordinator, FirstFrame(100, oldGeneration)) &&
        coordinator.Context().candidateProcessId == 100,
        "stale PID and generation evidence is rejected");
}
}

int main()
{
    bool ok = true;
    ok &= GenericScenario();
    ok &= GenericReplacementScenario();
    ok &= SteamScenario();
    ok &= SteamClearScenario();
    ok &= AltTabScenario();
    ok &= MicrosoftEarlyScenario();
    ok &= MicrosoftReplacementScenario();
    ok &= MicrosoftProtectionScenario();
    ok &= ReadyProtectionScenario();
    ok &= ClearScenario();
    ok &= SteamCandidateExitScenario();
    ok &= SteamWindowCandidateScenario(ProductionWindowEventType::Create);
    ok &= SteamWindowCandidateScenario(ProductionWindowEventType::Show);
    ok &= SteamWindowCandidateProtectionScenario();
    ok &= SteamWindowGenericMergeScenario();
    ok &= SteamSessionArmRequiresAppIdScenario();
    ok &= SteamWindowAdmissionRegressionScenario();
    ok &= MergeScenario();
    ok &= SamePidUpdatePreservesStateScenario();
    ok &= RendererSignalScenario();
    ok &= ReadyCommitScenario();
    ok &= StaleGenerationScenario();
    return ok ? 0 : 1;
}
