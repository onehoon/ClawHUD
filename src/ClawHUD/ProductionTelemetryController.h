#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "AlwaysModeFpsTarget.h"
#include "BatteryPowerEstimator.h"
#include "EcHelperClient.h"
#include "FpsStaleHold.h"
#include "HudModel.h"
#include "HudTelemetryAggregator.h"
#include "IntelGraphicsApiProbe.h"
#include "MsiEcHudTelemetry.h"
#include "PresentMonTelemetryProvider.h"
#include "WindowsPowerTelemetry.h"

namespace clawhud
{
// Win32 timer ids owned by the production telemetry controller. They share the
// application message window with App's own ids (kHudToggleHotkeyId in App.h,
// the resume-recovery timer id in App.cpp): the numeric values stay globally
// distinct.
inline constexpr UINT_PTR kEcHudTimerId = 2;
inline constexpr UINT_PTR kBatteryHudTimerId = 3;
inline constexpr UINT_PTR kGraphicsApiRetryTimerId = 4;
inline constexpr UINT_PTR kPresentMonFpsTimerId = 6;

// Owns production HUD telemetry: EC / system / battery / FPS / graphics-API
// sampling, retention, target state, and the sampling timer lifecycle.
//
// It holds a non-owning reference to the one shared PresentMonTelemetryProvider
// (App is the composition root for that; there is exactly one production API2
// session). It never touches HudPresentation, game detection, or
// GameRenderVerifier: App supplies the FPS target inputs explicitly and passes a
// narrow requestRender callback that a fresh sample invokes to reach the HUD.
class ProductionTelemetryController
{
public:
    explicit ProductionTelemetryController(PresentMonTelemetryProvider& provider);

    // Supplies the application message window (SetTimer/KillTimer host) and the
    // render-request callback. Must be called once, after the tray window
    // exists and before any sampling starts.
    void Bind(HWND messageWindow, std::function<void()> requestRender);

    // --- snapshot ---------------------------------------------------------
    void FillSnapshot(HudTelemetrySnapshot& snapshot) const;

    // --- per-timer sampling (App applies the suspended / HUD-visible guard
    //     before delegating; these run unconditionally) --------------------
    void SampleSystemEc();
    void SampleBattery();
    void SampleFps();

    // --- FPS target inputs (App is the authority; these only cache) -------
    // Startup sync: sets the cached mode with no reset side effects.
    void SyncVisibilityMode(HudVisibilityMode mode) noexcept { visibilityMode_ = mode; }
    // Runtime mode change: releases foreground authority, clears FPS/stale
    // state, and (for Always) adopts the supplied foreground PID.
    void SetVisibilityMode(HudVisibilityMode mode, DWORD currentForegroundProcessId);
    void OnForegroundProcessChanged(DWORD processId);
    // In-Game Only FPS target: exactly the current eligible foreground game PID
    // (GameSessionController authority). Never a long-lived/background process.
    // Changing the target PID or clearing it invalidates any retained FPS
    // immediately so an old game's FPS never shows for a new target.
    void SetInGameForegroundProcess(DWORD processId);
    void ClearInGameForegroundProcess();
    DWORD InGameForegroundProcessId() const noexcept { return inGameForegroundProcessId_; }

    // --- graphics-API probe --------------------------------------------
    void StartGraphicsApiProbe(DWORD processId);
    void EnsureGraphicsApiProbe(DWORD processId);
    void StopGraphicsApiProbe();
    void StopGraphicsApiProbeIfTarget(DWORD processId);
    void ReconcileGraphicsApiTargetLiveness();
    void TryGraphicsApiProbe();
    DWORD GraphicsApiProcessId() const noexcept { return graphicsApiProcessId_; }

    // --- sampling lifecycle ---------------------------------------------
    bool SamplingActive() const noexcept { return samplingActive_; }
    // Marks sampling active, logs, takes an immediate EC + battery sample, and
    // arms the 1 s / 5 s timers. No-op when already active.
    void StartBaseSampling();
    void StartFpsSampling();
    void StopFpsSampling(bool clearTarget = true);
    // Two-phase stop so App can keep its GameRenderVerifier stop at the current
    // insertion point (between the timer/FPS teardown and the state reset).
    void StopSamplingTimersAndFps();
    void ResetSamplingState(const wchar_t* reason);

    // ReadHudEcTelemetry moved verbatim.
    MsiEcHudTelemetry ReadEcTelemetry();

private:
    PresentMonTelemetryProvider& provider_;
    HWND messageWindow_{};
    std::function<void()> requestRender_;

    std::unique_ptr<EcHelperClient> ecClient_;
    HudTelemetryAggregator aggregator_;
    std::optional<WindowsPowerTelemetry> latestPower_;
    BatteryPowerEstimator batteryEstimator_;
    bool batteryEcOnDc_{};
    bool batteryEcReadyLogged_{};

    // Retains the last valid FPS across short same-PID API2 misses (2 s window).
    std::optional<double> latestProcessFps_;
    FpsStaleHold fpsStaleHold_;
    // Rate limiter for the once-per-second Displayed vs Presented FPS debug log.
    std::uint64_t lastFpsCompareLogTick_{};
    // Always mode: FPS target authority is the current foreground PID only,
    // fully decoupled from game detection. In-Game Only is unaffected.
    AlwaysModeFpsTarget alwaysFpsTarget_;

    IntelGraphicsApiProbe graphicsApiProbe_;
    std::optional<std::wstring> latestGraphicsApi_;
    DWORD graphicsApiProcessId_{};
    unsigned graphicsApiAttempts_{};

    bool samplingActive_{};

    // Explicit FPS target inputs cached from App (never read from game detection).
    HudVisibilityMode visibilityMode_{HudVisibilityMode::Always};
    // Current eligible foreground game PID for In-Game Only mode, or 0.
    DWORD inGameForegroundProcessId_{};
};
}
