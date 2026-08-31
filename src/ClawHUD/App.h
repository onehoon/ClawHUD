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
#include "ForegroundTracker.h"
#include "GameDetection/WindowsGameIdentitySource.h"
#include "GameDetection/ProcessLifecycleSource.h"
#include "GameDetection/PresentActivitySource.h"
#include "GameDetection/WindowLifecycleSource.h"
#include "GameDetection/GameDetectionCoordinator.h"
#include "GameDetection/GameRenderVerifier.h"
#include "GameDetection/GenericForegroundTrigger.h"
#include "GameDetection/MicrosoftGameTrigger.h"
#include "GameDetection/ProductionGameWindowSource.h"
#include "GameDetection/ProductionProcessLifetime.h"
#include "GameDetection/SteamRunningAppTrigger.h"
#include "PresentMonTelemetryProvider.h"
#include "ProductionTelemetryController.h"
#include "HudController.h"
#include "HudSettingsStore.h"
#include "SteamRunningAppIdSource.h"
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
    void ReconcileHudVisibility();
    void ReleaseCommittedProductionTarget(const wchar_t* reason);
    void ReevaluateProductionGameDetection();
    void HandleProductionForegroundChanged(HWND window, DWORD processId);
    void HandleProductionWindowEvent(
        const clawhud::ProductionWindowEvent& event);
    void HandleProductionProcessExit(DWORD processId,
        std::uint64_t generation);
    void HandleMicrosoftGameEvidence(
        const clawhud::MicrosoftGameTriggerEvidence& evidence);
    void HandleGameDetectionTransition(
        const clawhud::GameDetectionTransitionResult& transition,
        clawhud::GameDetectionTrigger trigger =
            clawhud::GameDetectionTrigger::GenericForeground,
        DWORD previousProcessId = 0,
        std::uint64_t previousGeneration = 0);
    void StartCandidateRenderVerification();
    void ArmProductionProcessLifetime(DWORD processId,
        std::uint64_t generation);
    void HandleGameRenderVerifierEvent(
        const clawhud::GameRenderVerifierEvent& event);
    bool TryCommitReadyCandidateFromForeground(
        HWND foreground, DWORD foregroundProcessId);
    void ReleaseProductionGameCandidate(const wchar_t* reason);
    void ClearProductionCandidate(const wchar_t* reason);
    void ApplyProductionEvidence(clawhud::GameDetectionTrigger trigger,
        HWND window, DWORD processId);
    void StartProductionSampling();
    void PauseProductionSamplingForSuspend();
    void CancelResumeRecovery();
    void StopProductionSampling(bool stopRenderVerification = true,
        const wchar_t* reason = L"explicit-reset");
    void StartGameRenderVerification();
    void StopGameRenderVerification(const wchar_t* reason = L"explicit-reset",
        bool clearLatestFps = false);
    void DiscardPendingMicrosoftGameEvidence();
    void DiscardPendingProductionWindowEvents();
    void DiscardPendingProductionProcessExitEvents();
    void DiscardPendingGameRenderVerifierEvents();

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
    ForegroundTracker foregroundTracker_;
    clawhud::WindowsGameIdentitySource windowsGameIdentitySource_;
    std::unique_ptr<SettingsWindow> settings_;
    bool exiting_{};
    std::wstring executablePath_;
    bool hudHotkeyRegistered_{};
    bool intelVrrRangeFixEnabled_{ true };
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
    clawhud::GameDetectionCoordinator gameDetectionCoordinator_;
    clawhud::SteamRunningAppTrigger steamRunningAppTrigger_;
    clawhud::GenericForegroundTrigger genericForegroundTrigger_;
    clawhud::MicrosoftGameTrigger microsoftGameTrigger_;
    clawhud::ProductionGameWindowSource productionGameWindowSource_;
    clawhud::ProductionProcessLifetimeWatcher productionProcessLifetimeWatcher_;
    clawhud::GameRenderVerifier gameRenderVerifier_{presentMonTelemetryProvider_};
    SteamRunningAppIdSource steamRunningAppIdSource_;
    std::uint32_t steamRunningAppId_{};
};
