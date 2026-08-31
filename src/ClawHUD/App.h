#pragma once

#include <windows.h>

#include <memory>
#include <optional>
#include <string>

#include "TrayIcon.h"
#include "HudModel.h"
#include "GameDetection/GameSessionController.h"
#include "PresentMonTelemetryProvider.h"
#include "ProductionTelemetryController.h"
#include "HudController.h"
#include "HudSettingsStore.h"
#include "Tweaks/TweakStartupCoordinator.h"
#include "Tweaks/IntelVrr/IntelVrrRunResult.h"

class SettingsWindow;

namespace clawhud
{
class DebugObservationController;
}

constexpr int kHudToggleHotkeyId = 1;
// The production telemetry timer ids (2, 3, 4, 6) live in
// ProductionTelemetryController.h and the resume-recovery timer id (5) is
// App-internal in App.cpp; the numeric values stay globally distinct because
// they all target the one application message window.

class App
{
public:
    explicit App(HINSTANCE instance);
    ~App();

    int Run();

    // Tray shell entry points.
    void OpenSettings();
    void Exit();
    void HandleSystemSuspend();
    void HandleSystemResume();
    void HandleTimer(UINT_PTR timerId);
    void HandleHudToggleHotkey();

    // Asynchronous notification that the lazy SettingsWindow's HWND is gone.
    void PostSettingsDestroyed();

    // SettingsWindow facade.
    bool StartWithWindows() const noexcept { return startWithWindows_; }
    void SetStartWithWindows(bool enabled);
    bool HudEnabled() const noexcept { return hudController_.Enabled(); }
    int HudSizeOffset() const noexcept { return hudController_.SizeOffset(); }
    bool SetHudEnabled(bool enabled);
    const clawhud::HudLayoutOptions& HudOptions() const noexcept { return hudController_.Options(); }
    clawhud::HudFont HudFont() const noexcept { return hudController_.Font(); }
    void SetHudAlignment(clawhud::HudAlignment alignment);
    void SetHudFont(clawhud::HudFont font);
    void SetHudBackgroundMode(clawhud::HudBackgroundMode mode);
    bool SetHudOpacity(float opacity, bool persist = true);
    void SetHudSizeOffset(int offset);
    void SetHudVisibilityMode(clawhud::HudVisibilityMode mode);
    bool IntelVrrRangeFixEnabled() const noexcept { return intelVrrRangeFixEnabled_; }
    void SetIntelVrrRangeFixEnabled(bool enabled);
    std::optional<clawhud::IntelVrrRunResult> IntelVrrLastResult() const;

private:
    bool AcquireSingleInstance();
    void CheckForUpdates();
    int ProcessMessages();
    void LoadHudSettings();
    void SaveHudEnabledSetting(bool enabled) const;
    void SaveHudSettings() const;
    bool ApplyStartupRegistration() const;
    clawhud::GameSessionHooks MakeGameSessionHooks();
    void ReconcileHudVisibility();
    void StartProductionSampling();
    void PauseProductionSamplingForSuspend();
    void CancelResumeRecovery();
    void StopProductionSampling(bool stopRenderVerification = true,
        const wchar_t* reason = L"explicit-reset");
    // Shared runtime-source stop sequence for ~App() and Exit(). Same effective
    // order both callers used inline before R7.
    void StopRuntimeSources();
    void SettingsDestroyed();
    void TryResumeRecovery();
    void StopHud();
    void RenderProductionHud(bool allowHidden = false);
    bool HudVisible() const noexcept { return hudController_.Visible(); }

    HINSTANCE instance_{};
    HANDLE instanceMutex_{};
    clawhud::HudSettingsStore hudSettingsStore_;
    TrayIcon tray_;
    // Owns HUD user state + the existing concrete HudPresentation object and
    // every Initialize / Render / Show / Hide / Shutdown call site. Presentation
    // stays lazily allocated.
    clawhud::HudController hudController_{instance_};
    clawhud::PresentMonTelemetryProvider presentMonTelemetryProvider_;
    // Owns EC / system / battery / FPS / graphics-API telemetry, retention,
    // target state, and the sampling timer lifecycle. Holds a non-owning
    // reference to the shared provider above.
    clawhud::ProductionTelemetryController productionTelemetry_{
        presentMonTelemetryProvider_};
    // Owns the production game-session detector: triggers, the coordinator
    // state machine, the event sources, GameRenderVerifier, the Steam
    // RunningAppID session context, and the game-session WM_APP plumbing.
    // Holds a non-owning reference to the shared provider (verifier only).
    clawhud::GameSessionController gameSession_{presentMonTelemetryProvider_};
    // Debug-only observation sources. Constructed lazily in Run() only when
    // debugLoggingEnabled_; stays null (and none of the sources exist) for a
    // normal DebugLog=0 run. See GameDetection/DebugObservationController.h.
    std::unique_ptr<clawhud::DebugObservationController> debugObservation_;
    std::unique_ptr<SettingsWindow> settings_;
    bool exiting_{};
    std::wstring executablePath_;
    bool hudHotkeyRegistered_{};
    bool intelVrrRangeFixEnabled_{ true };
    // Suspend/resume is deliberately kept as top-level App orchestration (R5):
    // App is the single authority for this state, and HandleSystemSuspend /
    // HandleSystemResume / TryResumeRecovery are cross-domain recovery flows that
    // drive HudController, ProductionTelemetryController and GameSessionController
    // through their narrow APIs. The pure decisions live in SuspendResumePolicy.h.
    bool suspended_{};
    bool resumeRecoveryActive_{};
    unsigned resumeRecoveryAttempts_{};
    clawhud::TweakStartupCoordinator tweakStartupCoordinator_;
    bool startWithWindows_{true};
    // Developer-only. Read once from [Developer] DebugLog in settings.ini at
    // startup; never written by the app and never toggled at runtime.
    bool debugLoggingEnabled_{};
};
