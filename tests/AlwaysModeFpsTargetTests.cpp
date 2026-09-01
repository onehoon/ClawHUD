#include "AlwaysModeFpsTarget.h"

#include <iostream>
#include <optional>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

// B: the foreground PID is the FPS target, nothing else overrides it.
void ForegroundPidIsTarget(bool& ok)
{
    AlwaysModeFpsTarget target;
    ok &= Check(target.TargetProcessId() == 0, "initial target is zero");
    ok &= Check(target.SetForegroundProcess(9124), "first foreground PID changes target");
    ok &= Check(target.TargetProcessId() == 9124, "target is the foreground PID");
    ok &= Check(!target.SetForegroundProcess(9124), "same PID is not a change");
}

// C: a foreground PID change immediately invalidates the previous FPS.
void ForegroundChangeInvalidatesFps(bool& ok)
{
    AlwaysModeFpsTarget target;
    target.SetForegroundProcess(9124);
    target.AcceptSample(9124, 120.0);
    ok &= Check(target.DisplayedFps() == 120.0, "foreground FPS is published");
    ok &= Check(target.SetForegroundProcess(8908), "foreground switch changes target");
    ok &= Check(!target.DisplayedFps().has_value(),
        "previous FPS is invalidated before the new PID result arrives");
}

// D / stale: results for a non-current PID are never published.
void NoBackgroundFallbackOrStaleResult(bool& ok)
{
    AlwaysModeFpsTarget target;
    target.SetForegroundProcess(8908);
    // A late result from the previous foreground PID must be rejected (K).
    target.AcceptSample(9124, 120.0);
    ok &= Check(!target.DisplayedFps().has_value(),
        "background/stale PID result is not published");
    // The foreground PID itself has no renderer result.
    target.AcceptSample(8908, std::nullopt);
    ok &= Check(!target.DisplayedFps().has_value(),
        "foreground PID without a renderer result stays unavailable");
}

// E: a foreground app with FPS publishes it.
void ForegroundAppWithFps(bool& ok)
{
    AlwaysModeFpsTarget target;
    target.SetForegroundProcess(5000);
    target.AcceptSample(5000, 60.0);
    ok &= Check(target.DisplayedFps() == 60.0, "foreground FPS publishes");
}

// H: PID 0 produces unavailable FPS.
void ZeroPid(bool& ok)
{
    AlwaysModeFpsTarget target;
    target.SetForegroundProcess(9124);
    target.AcceptSample(9124, 120.0);
    ok &= Check(target.SetForegroundProcess(0), "missing foreground clears target");
    ok &= Check(target.TargetProcessId() == 0, "target PID is zero");
    target.AcceptSample(0, std::nullopt);
    ok &= Check(!target.DisplayedFps().has_value(), "PID 0 FPS is unavailable");
}

// I / J: mode switching adopts the current foreground PID and releases authority.
void ModeSwitching(bool& ok)
{
    AlwaysModeFpsTarget target;
    // Enter Always with a known foreground PID (8908); the In-Game Only game PID
    // (9124) must never become the target.
    target.SetForegroundProcess(8908);
    target.AcceptSample(8908, 75.0);
    ok &= Check(target.DisplayedFps() == 75.0 && target.TargetProcessId() == 8908,
        "Always adopts the current foreground PID, not the In-Game game PID");

    // Leave Always mode.
    target.Release();
    ok &= Check(target.TargetProcessId() == 0 && !target.DisplayedFps().has_value(),
        "leaving Always releases foreground-PID FPS authority");
    // A stray sample after release is ignored.
    target.AcceptSample(8908, 75.0);
    ok &= Check(!target.DisplayedFps().has_value(),
        "released target does not accept samples");

    // Re-enter Always: current foreground PID becomes the target immediately.
    ok &= Check(target.SetForegroundProcess(8908),
        "re-entering Always re-adopts the foreground PID");
}

// Mode-dependent target selection inside the shared production FPS sampler.
void SharedSamplerTargetSelection(bool& ok)
{
    constexpr DWORD foregroundPid = 8908;
    constexpr DWORD inGamePid = 9124;

    // Always targets the foreground PID, never the In-Game Only game PID.
    ok &= Check(ResolveProductionFpsTargetPid(HudVisibilityMode::Always,
        foregroundPid, inGamePid) == foregroundPid,
        "Always mode selects the foreground PID");

    // In-Game Only targets the current eligible foreground game PID.
    ok &= Check(ResolveProductionFpsTargetPid(HudVisibilityMode::InGameOnly,
        foregroundPid, inGamePid) == inGamePid,
        "In-Game Only mode selects the current foreground game PID");

    // Always never falls back to the game PID when the foreground PID is 0.
    ok &= Check(ResolveProductionFpsTargetPid(HudVisibilityMode::Always,
        0, inGamePid) == 0,
        "Always mode does not fall back to the In-Game game PID");

    // In-Game Only with no current game (foreground is not a game) has no
    // target; it never falls back to a background game or the foreground PID.
    ok &= Check(ResolveProductionFpsTargetPid(HudVisibilityMode::InGameOnly,
        foregroundPid, 0) == 0,
        "In-Game Only mode does not fall back to the foreground PID");
}
}

int main()
{
    bool ok = true;
    ForegroundPidIsTarget(ok);
    ForegroundChangeInvalidatesFps(ok);
    NoBackgroundFallbackOrStaleResult(ok);
    ForegroundAppWithFps(ok);
    ZeroPid(ok);
    ModeSwitching(ok);
    SharedSamplerTargetSelection(ok);
    return ok ? 0 : 1;
}
