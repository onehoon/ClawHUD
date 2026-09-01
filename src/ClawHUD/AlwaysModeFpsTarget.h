#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>

#include "HudModel.h"

namespace clawhud
{
// Chooses the production FPS target PID for the active visibility mode. This is
// the only mode-dependent part of the shared PresentMon API2 FPS sampler:
//   - Always     -> the current foreground PID (no game-detection input),
//   - InGameOnly -> the current eligible foreground game PID.
// The two modes never share a fallback: an unavailable target yields PID 0, so
// a background game is never used when the current foreground is not a game.
inline DWORD ResolveProductionFpsTargetPid(HudVisibilityMode mode,
    DWORD foregroundProcessId, DWORD inGameForegroundProcessId) noexcept
{
    return mode == HudVisibilityMode::Always
        ? foregroundProcessId
        : inGameForegroundProcessId;
}

// The cached In-Game Only target (GameSessionController authority) keeps
// changing under Always mode too - renderer verification, admission, and
// foreground transitions all keep running regardless of visibility mode. That
// must never disturb the independent Always-mode FPS query/value: only
// InGameOnly is the shared FPS state's active authority, so only InGameOnly
// may invalidate it on a target change/clear.
inline bool InGameTargetChangeInvalidatesFps(HudVisibilityMode mode) noexcept
{
    return mode == HudVisibilityMode::InGameOnly;
}

// FPS result explicitly associated with the PID it was queried for.
struct PublishedProcessFps
{
    DWORD processId{};
    std::optional<double> displayedFps;
};

// Owns the foreground-PID FPS target authority for HUD Visibility Mode = Always.
//
// This type is deliberately independent from the game-detection pipeline. It
// only knows a PID and the most recent PresentMon API2 result that was queried
// for that exact PID. It has no knowledge of game identity, Steam, Microsoft
// Store, the In-Game Only game target, RendererTargetSelector, or fullscreen
// state, and it never falls back to any other PID.
class AlwaysModeFpsTarget
{
public:
    // Records the current foreground PID. When the target PID changes, any
    // previously published FPS is invalidated immediately (a stale FPS must
    // never leak across a foreground target change). Returns true when the
    // target PID actually changed.
    bool SetForegroundProcess(DWORD processId) noexcept;

    // Current FPS target PID. Zero when there is no foreground process.
    DWORD TargetProcessId() const noexcept { return targetProcessId_; }

    // Accepts a PresentMon API2 sample. The sample is ignored unless it was
    // queried for the current target PID, which rejects late results belonging
    // to a previous foreground PID after a rapid foreground switch.
    void AcceptSample(DWORD processId,
        std::optional<double> displayedFps) noexcept;

    // Displayed FPS for the current target PID, or nullopt when unavailable.
    // Never returns a value associated with a different PID.
    std::optional<double> DisplayedFps() const noexcept;

    // Releases foreground-PID FPS authority. Used when leaving Always mode so
    // the foreground PID is no longer an authoritative FPS source.
    void Release() noexcept;

private:
    DWORD targetProcessId_{};
    PublishedProcessFps published_{};
};
}
