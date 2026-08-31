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
#include "AlwaysModeFpsTarget.h"
#include "FpsStaleHold.h"
#include "EcHelperClient.h"
#include "HudSettingsStore.h"
#include "HudTelemetryAggregator.h"
#include "MsiEcHudTelemetry.h"
#include "WindowsPowerTelemetry.h"
#include "BatteryPowerEstimator.h"
#include "SteamRunningAppIdSource.h"
#include "IntelGraphicsApiProbe.h"
#include "Tweaks/TweakStartupCoordinator.h"
#include "Tweaks/IntelVrr/IntelVrrRunResult.h"

class SettingsWindow;
namespace clawhud { class HudPresentation; struct HudRenderOptions; }

constexpr int kHudToggleHotkeyId = 1;
constexpr UINT_PTR kEcHudTimerId = 2;
constexpr UINT_PTR kBatteryHudTimerId = 3;
constexpr UINT_PTR kGraphicsApiRetryTimerId = 4;
constexpr UINT_PTR kResumeRecoveryTimerId = 5;
constexpr UINT_PTR kPresentMonFpsTimerId = 6;

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
    bool HudVisible() const noexcept;
    bool HudEnabled() const noexcept { return hudEnabled_; }
    int HudSizeOffset() const noexcept { return hudSizeOffset_; }
    bool SetHudEnabled(bool enabled);
    const clawhud::HudLayoutOptions& HudOptions() const noexcept { return hudOptions_; }
    clawhud::HudFont HudFont() const noexcept { return hudFont_; }
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
    clawhud::HudRenderOptions BuildHudRenderOptions() const;
    bool RecreateHudPresentation(bool restoreVisible);
    bool ApplyStartupRegistration() const;
    void RefreshHud();
    bool EnsureHud();
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
    clawhud::MsiEcHudTelemetry ReadHudEcTelemetry();
    void StartProductionSampling();
    void PauseProductionSamplingForSuspend();
    void CancelResumeRecovery();
    void StopProductionSampling(bool stopRenderVerification = true,
        const wchar_t* reason = L"explicit-reset");
    void StartGameRenderVerification();
    void StopGameRenderVerification(const wchar_t* reason = L"explicit-reset",
        bool clearLatestFps = false);
    void StartProductionFpsSampling();
    void StopProductionFpsSampling(bool clearTarget = true);
    void StartGraphicsApiProbe(DWORD processId);
    void StopGraphicsApiProbe();
    void DiscardPendingMicrosoftGameEvidence();
    void DiscardPendingProductionWindowEvents();
    void DiscardPendingProductionProcessExitEvents();
    void DiscardPendingGameRenderVerifierEvents();

    HINSTANCE instance_{};
    HANDLE instanceMutex_{};
    clawhud::HudSettingsStore hudSettingsStore_;
    TrayIcon tray_;
    std::unique_ptr<clawhud::HudPresentation> hudPresentation_;
    std::unique_ptr<EcHelperClient> ecHudClient_;
    clawhud::HudTelemetryAggregator telemetryAggregator_;
    clawhud::PresentMonTelemetryProvider presentMonTelemetryProvider_;
    std::optional<double> latestProcessFps_;
    // Retains the last valid FPS across short same-PID API2 misses (2 s window).
    clawhud::FpsStaleHold fpsStaleHold_;
    // Rate limiter for the once-per-second Displayed vs Presented FPS debug log.
    std::uint64_t lastFpsCompareLogTick_{};
    // Always mode: FPS target authority is the current foreground PID only,
    // fully decoupled from game detection. In-Game Only is unaffected.
    clawhud::AlwaysModeFpsTarget alwaysFpsTarget_;
    std::optional<clawhud::WindowsPowerTelemetry> latestPowerTelemetry_;
    clawhud::BatteryPowerEstimator batteryPowerEstimator_;
    bool batteryEcOnDc_{};
    bool batteryEcReadyLogged_{};
    clawhud::IntelGraphicsApiProbe graphicsApiProbe_;
    std::optional<std::wstring> latestGraphicsApi_;
    DWORD graphicsApiProcessId_{};
    unsigned graphicsApiAttempts_{};
    ForegroundTracker foregroundTracker_;
    clawhud::WindowsGameIdentitySource windowsGameIdentitySource_;
    clawhud::HudLayoutOptions hudOptions_{};
    clawhud::HudFont hudFont_{clawhud::HudFont::Unispace};
    std::optional<bool> manualHudVisibilityOverride_;
    bool hudEnabled_{};
    std::unique_ptr<SettingsWindow> settings_;
    bool exiting_{};
    std::wstring executablePath_;
    int hudSizeOffset_{};
    bool hudHotkeyRegistered_{};
    bool productionSamplingActive_{};
    bool hudInitializedLogged_{};
    bool hudRenderFailureLogged_{};
    bool hudShowFailureLogged_{};
    bool hudHideFailureLogged_{};
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
