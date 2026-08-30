#include "GameDetection/GameDetectionCoordinator.h"
#include "GameDetection/GenericForegroundTrigger.h"
#include "ProductionTargetPolicy.h"

#include <cstdlib>
#include <iostream>

namespace
{
using namespace clawhud;

const HWND WindowA = reinterpret_cast<HWND>(0x1234);
const HWND WindowB = reinterpret_cast<HWND>(0x5678);

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

GenericForegroundEvidence Evidence(DWORD processId, HWND window = WindowA)
{
    return {window, processId};
}
}

int main()
{
    using namespace clawhud;
    GenericForegroundTrigger trigger;

    Check(!trigger.Inspect(nullptr, 11532).has_value(), "null HWND is rejected");
    Check(!trigger.Inspect(WindowA, 0).has_value(), "zero PID is rejected");
    Check(!trigger.Inspect(WindowA, GetCurrentProcessId()).has_value(),
        "current process is rejected");

    Check(!IsEligibleProductionTargetImage(L"C:\\Windows\\explorer.exe"),
        "explorer is rejected");
    Check(!IsEligibleProductionTargetImage(L"steam.exe"), "Steam is rejected");
    Check(!IsEligibleProductionTargetImage(L"steamwebhelper.exe"),
        "Steam helper is rejected");
    Check(!IsEligibleProductionTargetImage(L"gamingservicesui.exe"),
        "Gaming Services UI helper is rejected");
    Check(!IsEligibleProductionTargetImage(L"PickerHost.exe"),
        "PickerHost helper is rejected case-insensitively");
    Check(!IsEligibleProductionTargetImage(L"mongmode.exe"),
        "mongmode helper is rejected");
    Check(!IsEligibleProductionTargetImage(L"Chrome.EXE"),
        "browser rejection is case insensitive");
    Check(!IsEligibleProductionTargetImage(
        L"C:\\Users\\onehoon\\AppData\\Local\\SteamInputAddonforClaw\\current\\ui\\SteamInputAddonforClaw.UI.exe"),
        "Steam Input Addon UI path is rejected");
    Check(!IsEligibleProductionTargetImage(L"STEAMINPUTADDONFORCLAW.UI.EXE"),
        "Steam Input Addon UI rejection is case insensitive");
    Check(!IsEligibleProductionTargetImage(
        L"C:\\Program Files\\MSI\\MSI Center M\\MSI Center M.exe"),
        "MSI Center M path is rejected");
    Check(!IsEligibleProductionTargetImage(
        L"C:\\Program Files\\MSI\\MSI Center M\\Resources\\OSDInfo\\MCMOSDInfo.EXE"),
        "MCMOSDInfo path is rejected");
    Check(!IsEligibleProductionTargetImage(
        L"C:\\Program Files (x86)\\Steam\\GameOverlayUI.EXE"),
        "Steam GameOverlayUI path is rejected");
    Check(IsEligibleProductionTargetImage(L"C:\\Games\\sora_2nd.exe"),
        "game-like image is eligible");
    Check(IsEligibleProductionTargetImage(L"beastofreincarnation.exe"),
        "non-Steam image is eligible");

    GameDetectionCoordinator coordinator;
    auto transition = GenericForegroundTrigger::ApplyEvidence(coordinator,
        Evidence(11532));
    const auto generation = coordinator.Context().generation;
    Check(transition.transition == GameDetectionTransition::CandidateStarted &&
        coordinator.Context().state == GameDetectionState::Verifying &&
        coordinator.Context().candidateProcessId == 11532 &&
        coordinator.Context().candidateWindow == WindowA &&
        coordinator.Context().evidence.genericForeground && generation != 0,
        "generic evidence starts a verifying candidate");

    transition = GenericForegroundTrigger::ApplyEvidence(coordinator,
        Evidence(11532, WindowB));
    Check(transition.transition == GameDetectionTransition::CandidateUpdated &&
        coordinator.Context().generation == generation &&
        coordinator.Context().candidateWindow == WindowB,
        "same PID merges and updates HWND");

    Check(GenericForegroundTrigger::ApplyEvidence(coordinator,
        Evidence(20000)).transition == GameDetectionTransition::None &&
        coordinator.Context().candidateProcessId == 11532 &&
        coordinator.Context().generation == generation,
        "different PID does not replace verifying candidate");

    GameDetectionCoordinator steam;
    Check(steam.ObserveWake({GameDetectionTrigger::SteamRunningAppId, 0,
        nullptr, 5010190, false}).transition == GameDetectionTransition::Armed,
        "Steam session is armed");
    GenericForegroundTrigger::ApplyEvidence(steam, Evidence(18812));
    Check(steam.Context().state == GameDetectionState::Verifying &&
        steam.Context().candidateProcessId == 18812 &&
        steam.Context().steamAppId == 5010190 &&
        steam.Context().evidence.steamSession &&
        steam.Context().evidence.genericForeground,
        "generic evidence joins an armed Steam session");

    GameDetectionCoordinator microsoft;
    microsoft.ObserveCandidate(6008, WindowA,
        GameDetectionTrigger::MicrosoftGameIdentity);
    const auto microsoftGeneration = microsoft.Context().generation;
    GenericForegroundTrigger::ApplyEvidence(microsoft, Evidence(6008, WindowB));
    Check(microsoft.Context().generation == microsoftGeneration &&
        microsoft.Context().candidateProcessId == 6008 &&
        microsoft.Context().candidateWindow == WindowB &&
        microsoft.Context().evidence.microsoftGameIdentity &&
        microsoft.Context().evidence.genericForeground,
        "same PID merges with MicrosoftGame evidence");

    GameDetectionCoordinator ready;
    GenericForegroundTrigger::ApplyEvidence(ready, Evidence(6008));
    const auto readyGeneration = ready.Context().generation;
    Check(ready.MarkRendererReady(6008, readyGeneration), "candidate becomes ready");
    Check(GenericForegroundTrigger::ApplyEvidence(ready, Evidence(11532)).transition ==
        GameDetectionTransition::None &&
        ready.Context().state == GameDetectionState::Ready &&
        ready.Context().candidateProcessId == 6008,
        "ready candidate is not displaced");
    Check(ready.CommitCandidate(6008, readyGeneration), "candidate becomes committed");
    Check(GenericForegroundTrigger::ApplyEvidence(ready, Evidence(11532)).transition ==
        GameDetectionTransition::None &&
        ready.Context().state == GameDetectionState::Committed &&
        ready.Context().candidateProcessId == 6008,
        "committed candidate is not displaced");

    std::cout << "PASS\n";
}
