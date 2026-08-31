#pragma once

#include <windows.h>

#include <memory>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "TrayIcon.h"
#include "HudModel.h"
#include "GameDetection/WindowsGameIdentitySource.h"
#include "GameDetection/ProcessLifecycleSource.h"
#include "GameDetection/PresentActivitySource.h"
#include "GameDetection/WindowLifecycleSource.h"
#include "GameDetection/GameSessionController.h"
#include "PresentMonTelemetryProvider.h"
#include "ProductionTelemetryController.h"
#include "HudController.h"
#include "HudSettingsStore.h"
#include "Tweaks/TweakStartupCoordinator.h"
#include "Tweaks/IntelVrr/IntelVrrRunResult.h"

class SettingsWindow;

constexpr int kHudToggleHotkeyId = 1;
constexpr UINT_PTR kResumeRecoveryTimerId = 5;
// The production telemetry timer ids (2, 3, 4, 6) live in
// ProductionTelemetryController.h; the numeric values stay globally distinct
// because they all target the one application message window.

class App
{
public:
    explicit App(HINSTANCE instance);
    ~App();

    int Run();
    void OpenSettings();
    void Exit();
    void SettingsDestroyed();
    bool StartWithWindows() const noexcept { return startWithWindows_; }
    void SetStartWithWindows(bool enabled);
    HWND MessageWindow() const { return tray_.Window(); }
    const std::wstring& ExecutablePath() const { return executablePath_; }
    void HandleSystemSuspend();
    void HandleSystemResume();
    void TryResumeRecovery();
    void StopHud();
    void SampleProductionTelemetry();
    void SampleProductionBatteryTelemetry();
    void SampleProductionFpsTelemetry();
    void RenderProductionHud(bool allowHidden = false);
    void TryGraphicsApiProbe();
    bool HudVisible() const noexcept { return hudController_.Visible(); }
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
    void HandleHudToggleHotkey();
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
    clawhud::WindowsGameIdentitySource windowsGameIdentitySource_;
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
    clawhud::ProcessLifecycleSource processLifecycleSource_;
    clawhud::PresentActivitySource presentActivitySource_;
    clawhud::WindowLifecycleSource windowLifecycleSource_;
};
