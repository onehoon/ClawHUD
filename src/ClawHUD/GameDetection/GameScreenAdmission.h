#pragma once

#include <windows.h>

namespace clawhud
{
inline constexpr LONG kGameFullscreenTolerancePx = 8;

enum class GameScreenAdmissionReason
{
    Admitted,
    NoWindow,
    NotTopLevel,
    NotVisible,
    Minimized,
    CloakUnavailable,
    Cloaked,
    ProcessUnavailable,
    ExcludedExecutable,
    NoMonitor,
    BoundsUnavailable,
    NotFullscreenLike,
};

struct GameScreenObservation
{
    HWND window{};
    DWORD processId{};
    bool windowExists{};
    bool topLevel{};
    bool visible{};
    bool minimized{};
    bool cloakKnown{};
    bool cloaked{};
    bool processInspected{};
    bool executableExcluded{};
    bool monitorResolved{};
    bool boundsResolved{};
    RECT windowBounds{};
    RECT monitorBounds{};
};

struct GameScreenAdmissionResult
{
    bool admitted{};
    GameScreenAdmissionReason reason{GameScreenAdmissionReason::NoWindow};
};

bool CoversMonitorBounds(const RECT& windowBounds, const RECT& monitorBounds,
    LONG tolerancePx = kGameFullscreenTolerancePx) noexcept;
GameScreenAdmissionResult EvaluateGameScreenAdmission(
    const GameScreenObservation& observation) noexcept;
GameScreenObservation ObserveGameScreen(HWND window, DWORD processId) noexcept;
}
