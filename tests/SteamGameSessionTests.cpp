#include "GpuEngineActivity.h"
#include "PresentMonHudTelemetry.h"
#include "SteamGameSession.h"

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
    check(!SteamOwnsGameSession(0), "zero app id uses generic authority");
    check(SteamOwnsGameSession(2344520), "nonzero app id owns session");
    check(ResolveSteamGameState(2344520, 0) == SteamGameState::Resolving,
        "nonzero app id starts resolving");
    check(ResolveSteamGameState(2344520, 1234) == SteamGameState::Active,
        "renderer pid activates session");
    check(ResolveSteamGameState(0, 1234) == SteamGameState::None,
        "zero app id ends session");
    check(DecodeSteamRunningAppId(0xF1234567u) == 0xF1234567u,
        "high-bit app id remains uint32");
    const auto initialIdle = EvaluateSteamAppIdTransition(false, 0, 0);
    check(initialIdle.shouldHandle && initialIdle.firstObservation &&
        initialIdle.allowBaselineRenderer && initialIdle.state == SteamGameState::None,
        "initial zero app id is recorded as an observation");
    const auto freshLaunch = EvaluateSteamAppIdTransition(true, 0, 1234);
    check(freshLaunch.shouldHandle && freshLaunch.freshLaunch &&
        !freshLaunch.allowBaselineRenderer && freshLaunch.state == SteamGameState::Resolving,
        "zero to nonzero app id is a fresh Steam launch");
    const auto replacement = EvaluateSteamAppIdTransition(true, 1234, 5678);
    check(replacement.freshLaunch && !replacement.allowBaselineRenderer &&
        replacement.state == SteamGameState::Resolving,
        "nonzero app id replacement creates a new session boundary");
    check(!EvaluateSteamAppIdTransition(true, 1234, 1234).shouldHandle,
        "unchanged app id is ignored");
    check(ShouldRunSteamRendererResolution(true, SteamGameState::Resolving,
        1234, false, false), "enabled Steam session can resolve renderer");
    check(!ShouldRunSteamRendererResolution(false, SteamGameState::Resolving,
        1234, false, false), "disabled HUD stops Steam renderer resolution");
    check(!ShouldRunSteamRendererResolution(true, SteamGameState::Active,
        1234, false, false), "active Steam session ignores foreground changes");
    check(ShouldRunSteamRendererResolution(true, SteamGameState::Resolving,
        1234, false, false), "resume can restart Steam renderer resolution");

    const std::vector<GpuEngineActivity> activity{
        {100, 2.0, false, L"3D"},
        {200, 1.0, true, L"3D"},
        {300, 5.0, true, L"3D"},
    };
    const auto candidates = SelectGpuActiveProcessIds(activity, 200);
    check(candidates.size() == 3 && candidates[0] == 200 && candidates[1] == 300,
        "foreground and Intel GPU candidates rank first");

    const std::vector<DWORD> baseline{100};
    const auto freshLaunchCandidates = SelectGpuActiveProcessIds(
        activity, 100, baseline, false);
    check(freshLaunchCandidates.size() == 2 && freshLaunchCandidates[0] == 300 &&
        freshLaunchCandidates[1] == 200,
        "fresh Steam launch excludes pre-session renderer candidates");
    const auto startupCandidates = SelectGpuActiveProcessIds(
        activity, 100, baseline, true);
    check(startupCandidates.size() == 3 && startupCandidates[0] == 100,
        "startup with existing AppID allows baseline renderer candidates");
    const std::vector<GpuEngineActivity> handoffActivity{
        {200, 3.0, true, L"3D"}};
    const auto handoffCandidates = SelectGpuActiveProcessIds(
        handoffActivity, 0, baseline, false);
    check(handoffCandidates.size() == 1 && handoffCandidates[0] == 200,
        "renderer handoff keeps a new process eligible within the same session");
    const auto noGpuEvidenceCandidates = SelectGpuActiveProcessIds({}, 400);
    check(noGpuEvidenceCandidates.empty(),
        "foreground-only process is not a Steam renderer candidate");

    const std::vector<std::wstring> paths{L"pid_100_eng_3D", L"pid_200_eng_3D"};
    const std::unordered_set<std::wstring> bound{L"pid_100_eng_3D"};
    const auto newPaths = SelectUnboundGpuEnginePaths(paths, bound);
    check(newPaths.size() == 1 && newPaths[0] == L"pid_200_eng_3D",
        "new GPU Engine instances are discoverable after initialization");

    const PresentMonHudSample firstFrame{true, std::nullopt, false};
    check(firstFrame.hasDisplayedFrame && !firstFrame.displayedFps,
        "first displayed frame is independent from fps readiness");
    return ok ? 0 : 1;
}
