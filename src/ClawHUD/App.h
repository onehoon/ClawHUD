#pragma once

#include <windows.h>

#include <memory>
#include <optional>
#include <string>

#include "TrayIcon.h"
#include "RuntimeMessageWindow.h"
#include "RuntimeControl.h"
#include "RuntimeControlDispatchBridge.h"
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
// they all target the one runtime message window.
//
// Runtime-control dispatch wake-up on the runtime message window. WM_APP + 1 is
// the Settings-destroyed message and WM_APP + 2/5/6/7/8/9 are game-session
// messages, so this sits clear of both.
constexpr UINT kRuntimeControlDispatchMessage = WM_APP + 11;

class App : public clawhud::IRuntimeControl
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

    // Main-thread wake handler for the runtime-control dispatch bridge.
    void HandleRuntimeControlDispatch();

    // clawhud::IRuntimeControl — the semantic control boundary the legacy Win32
    // Settings frontend (and future frontends) use. App stays the implementation
    // authority; these delegate to the existing product methods below.
    clawhud::RuntimeSettingsSnapshot GetSettingsSnapshot() const override;
    void SetStartWithWindows(bool enabled) override;
    bool SetHudEnabled(bool enabled) override;
    void SetHudVisibilityMode(clawhud::HudVisibilityMode mode) override;
    void SetHudSizeOffset(int offset) override;
    void SetHudFont(clawhud::HudFont font) override;
    void SetHudAlignment(clawhud::HudAlignment alignment) override;
    void SetHudBackgroundMode(clawhud::HudBackgroundMode mode) override;
    bool PreviewHudOpacity(float opacity) override;
    bool CommitHudOpacity(float opacity) override;
    void SetIntelVrrRangeFixEnabled(bool enabled) override;

private:
    std::optional<clawhud::IntelVrrRunResult> IntelVrrLastResult() const;
    bool SetHudOpacity(float opacity, bool persist);
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
    // Runtime-owned hidden message window: F8 hotkey, suspend/resume power
    // notifications and the production WM_TIMER stream. Independent of the tray
    // so a future no-tray mode keeps this infrastructure. Created before any
    // runtime component is bound to an HWND.
    RuntimeMessageWindow runtimeMessageWindow_;
    TrayIcon tray_;
    // Moves validated Control requests from a background producer to this
    // thread and runs them through the IRuntimeControl path. Started near the
    // end of Run(); stopped first in StopRuntimeSources(). No transport here.
    clawhud::RuntimeControlDispatchBridge runtimeControlBridge_;
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
    // normal DebugLoggingEnabled=false run. See GameDetection/DebugObservationController.h.
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
    // Developer-only. Read once from [Developer] DebugLoggingEnabled in
    // settings.ini at startup; never written by the app and never toggled at runtime.
    bool debugLoggingEnabled_{};
};
