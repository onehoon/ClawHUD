#include "GameDetection/ForegroundGameDetector.h"

#include <cstdlib>
#include <iostream>
#include <optional>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}

clawhud::GameProcessInstance Process(DWORD pid, ULONGLONG creation)
{
    return {pid, creation};
}

clawhud::GameScreenObservation AdmittedScreen(HWND window, DWORD processId)
{
    clawhud::GameScreenObservation screen;
    screen.window = window;
    screen.processId = processId;
    screen.windowExists = true;
    screen.topLevel = true;
    screen.visible = true;
    screen.cloakKnown = true;
    screen.processInspected = true;
    screen.monitorResolved = true;
    screen.boundsResolved = true;
    screen.windowBounds = {0, 0, 1920, 1200};
    screen.monitorBounds = {0, 0, 1920, 1200};
    return screen;
}

clawhud::ForegroundGameDetector Detector(
    clawhud::KnownGameProcessCache& cache, ULONGLONG& generation)
{
    return clawhud::ForegroundGameDetector(cache,
        [&generation](DWORD processId) -> std::optional<clawhud::GameProcessInstance>
        {
            return clawhud::GameProcessInstance{processId, generation};
        });
}
}

int main()
{
    using namespace clawhud;
    const HWND gameWindow = reinterpret_cast<HWND>(0x1000);
    const HWND explorerWindow = reinterpret_cast<HWND>(0x2000);

    ULONGLONG generation = 10;
    KnownGameProcessCache cache;
    auto detector = Detector(cache, generation);

    auto terminal = AdmittedScreen(gameWindow, 100);
    terminal.executableExcluded = true;
    cache.MarkRendererVerified(Process(100, 10));
    auto evaluation = detector.Evaluate(terminal);
    Check(evaluation.current.decision == ForegroundGameDecision::Hidden &&
        !evaluation.verificationRequest, "known excluded foreground remains hidden");

    auto gameA = AdmittedScreen(gameWindow, 101);
    generation = 11;
    evaluation = detector.Evaluate(gameA);
    Check(evaluation.current.decision == ForegroundGameDecision::NeedsRendererVerification &&
        evaluation.verificationRequest, "unknown admitted game requests verification");
    const auto requestA = *evaluation.verificationRequest;
    detector.CompleteRendererVerification({requestA, true});
    Check(detector.Current().decision == ForegroundGameDecision::NeedsRendererVerification &&
        cache.Lookup(Process(101, 11))->rendererVerified,
        "completion caches exact process without changing current decision");
    evaluation = detector.Evaluate(gameA);
    Check(evaluation.current.decision == ForegroundGameDecision::Eligible &&
        !evaluation.verificationRequest, "renderer-known admitted game is eligible");

    generation = 12;
    auto unknown = AdmittedScreen(gameWindow, 102);
    const auto repeatedA = detector.Evaluate(unknown).verificationRequest;
    const auto repeatedB = detector.Evaluate(unknown).verificationRequest;
    const auto repeatedC = detector.Evaluate(unknown).verificationRequest;
    Check(repeatedA && repeatedB && repeatedC && *repeatedA == *repeatedB &&
        *repeatedB == *repeatedC && repeatedA->requestId != 0,
        "same unknown generation reuses verification request");
    generation = 13;
    auto gameB = AdmittedScreen(reinterpret_cast<HWND>(0x3000), 103);
    const auto requestB = detector.Evaluate(gameB).verificationRequest;
    Check(requestB && requestB->requestId != repeatedA->requestId,
        "different process generation receives a distinct request");

    generation = 14;
    const auto microsoft = Process(104, generation);
    cache.MarkMicrosoftGame(microsoft);
    evaluation = detector.Evaluate(AdmittedScreen(gameWindow, 104));
    Check(evaluation.current.decision == ForegroundGameDecision::Eligible &&
        !evaluation.verificationRequest, "Microsoft-known screen is immediately eligible");
    auto minimizedMicrosoft = AdmittedScreen(gameWindow, 104);
    minimizedMicrosoft.minimized = true;
    Check(detector.Evaluate(minimizedMicrosoft).current.decision ==
        ForegroundGameDecision::Hidden, "minimized known game remains hidden");

    generation = 15;
    const auto knownA = Process(105, generation);
    cache.MarkRendererVerified(knownA);
    Check(detector.Evaluate(AdmittedScreen(gameWindow, 105)).current.decision ==
        ForegroundGameDecision::Eligible, "renderer-known game supports Alt+Tab return");
    auto explorer = AdmittedScreen(explorerWindow, 106);
    explorer.executableExcluded = true;
    evaluation = detector.Evaluate(explorer);
    Check(evaluation.current.decision == ForegroundGameDecision::Hidden &&
        detector.Current().processId == 106, "hidden foreground replaces previous current game");
    generation = 15;
    Check(detector.Evaluate(AdmittedScreen(gameWindow, 105)).current.decision ==
        ForegroundGameDecision::Eligible, "known background game becomes eligible only on return");

    generation = 16;
    const auto multiA = Process(107, generation);
    cache.MarkMicrosoftGame(multiA);
    Check(detector.Evaluate(AdmittedScreen(gameWindow, 107)).current.decision ==
        ForegroundGameDecision::Eligible, "first known game is eligible");
    generation = 17;
    const auto multiB = detector.Evaluate(AdmittedScreen(
        reinterpret_cast<HWND>(0x4000), 108));
    Check(multiB.current.decision == ForegroundGameDecision::NeedsRendererVerification &&
        multiB.verificationRequest, "known background game does not block new foreground");
    detector.CompleteRendererVerification({*multiB.verificationRequest, true});
    Check(cache.IsKnownGame(multiA) && cache.IsKnownGame(Process(108, 17)) &&
        detector.Evaluate(AdmittedScreen(reinterpret_cast<HWND>(0x4000), 108)).current.decision ==
        ForegroundGameDecision::Eligible, "multiple known games coexist independently");

    generation = 18;
    const auto staleA = detector.Evaluate(AdmittedScreen(gameWindow, 109));
    Check(staleA.verificationRequest.has_value(), "stale-completion setup requests A");
    evaluation = detector.Evaluate(explorer);
    detector.CompleteRendererVerification({*staleA.verificationRequest, true});
    Check(detector.Current().decision == ForegroundGameDecision::Hidden &&
        cache.IsKnownGame(Process(109, 18)),
        "late completion caches A without replacing hidden current foreground");

    generation = 19;
    const auto staleB = detector.Evaluate(AdmittedScreen(gameWindow, 110));
    generation = 20;
    const auto currentB = detector.Evaluate(AdmittedScreen(
        reinterpret_cast<HWND>(0x5000), 111));
    detector.CompleteRendererVerification({*staleB.verificationRequest, true});
    Check(detector.Current().processId == 111 &&
        detector.Current().decision == ForegroundGameDecision::NeedsRendererVerification &&
        cache.IsKnownGame(Process(110, 19)),
        "completion for A does not switch current B");

    generation = 21;
    cache.MarkRendererVerified(Process(7000, 21));
    generation = 22;
    const auto reused = detector.Evaluate(AdmittedScreen(gameWindow, 7000));
    Check(reused.current.decision == ForegroundGameDecision::NeedsRendererVerification &&
        reused.verificationRequest, "reused PID does not inherit prior generation evidence");
    detector.CompleteRendererVerification({RendererVerificationRequest{999, Process(7000, 21)}, true});
    Check(!cache.IsKnownGame(Process(7000, 22)),
        "old generation completion cannot mark reused PID generation");

    KnownGameProcessCache steamCache;
    generation = 23;
    auto steamDetector = Detector(steamCache, generation);
    Check(steamDetector.SteamSession().generation == 0, "Steam context starts empty");
    steamDetector.UpdateSteamSession(10);
    steamDetector.UpdateSteamSession(10);
    steamDetector.UpdateSteamSession(20);
    steamDetector.UpdateSteamSession(0);
    Check(steamDetector.SteamSession().generation == 3 && !steamDetector.SteamSession().Active(),
        "Steam context generation changes only on semantic transitions");
    steamDetector.UpdateSteamSession(30);
    evaluation = steamDetector.Evaluate(AdmittedScreen(gameWindow, 112));
    Check(evaluation.current.decision == ForegroundGameDecision::NeedsRendererVerification &&
        steamCache.Lookup(Process(112, 23))->observedDuringSteamSession,
        "Steam-only context remains verification-needed");
    steamCache.MarkMicrosoftGame(Process(112, 23));
    Check(steamDetector.Evaluate(AdmittedScreen(gameWindow, 112)).current.decision ==
        ForegroundGameDecision::Eligible, "positive evidence, not Steam, makes screen eligible");

    auto cloaked = AdmittedScreen(gameWindow, 104);
    cloaked.cloaked = true;
    auto workArea = AdmittedScreen(gameWindow, 104);
    workArea.windowBounds.bottom = 1128;
    auto tolerance = AdmittedScreen(gameWindow, 104);
    tolerance.windowBounds = {-3, -3, 1923, 1203};
    auto queryFailure = AdmittedScreen(gameWindow, 113);
    auto processUnavailable = AdmittedScreen(gameWindow, 104);
    processUnavailable.processInspected = false;
    generation = 14;
    ForegroundGameDetector failingQuery(cache,
        [](DWORD) -> std::optional<GameProcessInstance> { return std::nullopt; });
    Check(detector.Evaluate(cloaked).current.decision == ForegroundGameDecision::Hidden &&
        detector.Evaluate(workArea).current.decision == ForegroundGameDecision::Hidden &&
        detector.Evaluate(tolerance).current.decision == ForegroundGameDecision::Eligible &&
        detector.Evaluate(processUnavailable).current.decision == ForegroundGameDecision::Hidden &&
        failingQuery.Evaluate(queryFailure).current.decision == ForegroundGameDecision::Hidden,
        "R1 admission and exact process identity remain authoritative");
    std::cout << "PASS\n";
}
