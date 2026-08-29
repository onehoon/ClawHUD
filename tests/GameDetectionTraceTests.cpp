#include "GameDetection/GameDetectionTrace.h"

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

    check(GameDetectionStateName(GameDetectionState::Idle) == L"Idle" &&
        GameDetectionStateName(GameDetectionState::Committed) == L"Committed",
        "state names");
    check(GameDetectionTriggerName(GameDetectionTrigger::GenericForeground) == L"Generic" &&
        GameDetectionTriggerName(GameDetectionTrigger::SteamRunningAppId) == L"Steam" &&
        GameDetectionTriggerName(GameDetectionTrigger::MicrosoftGameIdentity) == L"MicrosoftGame",
        "trigger names");
    check(GameDetectionTransitionName(GameDetectionTransition::CandidateStarted) ==
            L"CandidateStarted" &&
        GameDetectionTransitionName(GameDetectionTransition::RendererReady) ==
            L"RendererReady" &&
        GameDetectionTransitionName(GameDetectionTransition::Reset) == L"Reset",
        "transition names");

    return ok ? 0 : 1;
}
