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

    const std::vector<GpuEngineActivity> activity{
        {100, 2.0, false, L"3D"},
        {200, 1.0, true, L"3D"},
        {300, 5.0, true, L"3D"},
    };
    const auto candidates = SelectGpuActiveProcessIds(activity, 200);
    check(candidates.size() == 3 && candidates[0] == 200 && candidates[1] == 300,
        "foreground and Intel GPU candidates rank first");

    const PresentMonHudSample firstFrame{true, std::nullopt, false};
    check(firstFrame.hasDisplayedFrame && !firstFrame.displayedFps,
        "first displayed frame is independent from fps readiness");
    return ok ? 0 : 1;
}
