#pragma once

#include <windows.h>

#include <memory>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "TrayIcon.h"
#include "EcDiagnostic.h"
#include "VrrDiagnostic.h"
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
#include "MsiEcHudTelemetry.h"
#include "WindowsPowerTelemetry.h"
#include "BatteryPowerEstimator.h"
#include "SteamRunningAppIdSource.h"
#include "IntelGraphicsApiProbe.h"
#include "IgclTelemetryDiagnostic.h"
#include "PresentMonApi2Diagnostic.h"
#include "Tweaks/TweakStartupCoordinator.h"
#include "Tweaks/IntelVrr/IntelVrrRunResult.h"

class SettingsWindow;
namespace clawhud { class HudPresentation; struct HudRenderOptions; }

constexpr int kHudToggleHotkeyId = 1;
constexpr UINT_PTR kMockHudTimerId = 1;
constexpr UINT_PTR kEcHudTimerId = 2;
constexpr UINT_PTR kBatteryHudTimerId = 3;
constexpr UINT_PTR kGraphicsApiRetryTimerId = 4;
constexpr UINT_PTR kResumeRecoveryTimerId = 5;
constexpr UINT_PTR kPresentMonFpsTimerId = 6;
constexpr UINT kResumeRecoveryIntervalMs = 500;
constexpr unsigned kResumeRecoveryMaxAttempts = 6;

constexpr bool ShouldRestorePersistedHud(bool enabled) noexcept
{
    return enabled;
}

constexpr bool ResumeRecoveryShouldStart(bool active) noexcept
{
    return !active;
}

constexpr bool ResumeRecoveryNeedsSuspendFallback(bool suspended) noexcept
{
    return !suspended;
}

constexpr bool ResumeRecoveryHasAttemptsRemaining(unsigned attempts) noexcept
{
    return attempts < kResumeRecoveryMaxAttempts;
}

constexpr bool ResumeRecoveryCanRetainPresentMon(
    DWORD trackedProcessId, DWORD presentMonProcessId, bool running) noexcept
{
    return trackedProcessId != 0 && trackedProcessId == presentMonProcessId && running;
}

constexpr bool ResumeRecoveryShouldWaitForForeground(
    bool hudEnabled, bool visibilityUsesForeground, bool processAlive,
    bool foregroundMatches, unsigned attempts) noexcept
{
    return hudEnabled && visibilityUsesForeground && processAlive &&
        !foregroundMatches && ResumeRecoveryHasAttemptsRemaining(attempts);
}

constexpr bool ResumeRecoveryMayShowHud(bool expectedVisible, bool freshFrameReady) noexcept
{
    return !expectedVisible || freshFrameReady;
}

constexpr bool ResumeRecoveryFrameWasPresented(HRESULT renderResult) noexcept
{
    return renderResult == S_OK;
}

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
    bool DiagnosticsTabEnabled() const noexcept { return diagnosticsTabEnabled_; }
    void SetStartWithWindows(bool enabled);
    HWND MessageWindow() const { return tray_.Window(); }
    const std::wstring& ExecutablePath() const { return executablePath_; }
    bool StartEcDiagnostic();
    void StopEcDiagnostic();
    bool EcDiagnosticRunning() const;
    const std::wstring& EcStatus() const { return ecStatus_; }
    void OpenDiagnosticLogFolder();
    bool StartVrrDiagnostic();
    void StopVrrDiagnostic();
    bool VrrDiagnosticRunning() const;
    bool DiagnosticRunning() const;
    bool StartIgclDiagnostic();
    void StopIgclDiagnostic();
    bool IgclDiagnosticRunning() const;
    const std::wstring& IgclStatus() const noexcept { return igclStatus_; }
    bool StartPresentMonApi2Diagnostic();
    void StopPresentMonApi2Diagnostic();
    bool PresentMonApi2DiagnosticRunning() const;
    const std::wstring& PresentMonApi2Status() const noexcept { return presentMonApi2Status_; }
    void StopDiagnostic();
    void FinishIgclDiagnostic(bool success);
    void HandleSystemSuspend();
    void HandleSystemResume();
    void TryResumeRecovery();
    const std::wstring& VrrStatus() const { return vrrStatus_; }
    void StopMockHud();
    void RenderMockHud(bool allowHidden = false);
    void SampleProductionTelemetry();
    void SampleProductionBatteryTelemetry();
    void SampleProductionFpsTelemetry();
    void RenderProductionHud(bool allowHidden = false);
    void TryGraphicsApiProbe();
    bool MockHudVisible() const noexcept;
    bool MockHudEnabled() const noexcept { return mockHudEnabled_; }
    int HudSizeOffset() const noexcept { return hudSizeOffset_; }
    bool SetHudEnabled(bool enabled);
    const clawhud::HudLayoutOptions& HudOptions() const noexcept { return hudOptions_; }
    clawhud::HudFont HudFont() const noexcept { return hudFont_; }
    void SetHudAlignment(clawhud::HudAlignment alignment);
    void SetHudFont(clawhud::HudFont font);
    void SetHudBackgroundMode(clawhud::HudBackgroundMode mode);
    bool SetHudOpacity(float opacity, bool persist = true);
    void SetHudSizeOffset(int offset);
    void TrackMockGameWindow(HWND window);
    void SetHudVisibilityMode(clawhud::HudVisibilityMode mode);
    void HandleHudToggleHotkey();
    HudVisibilityState CaptureHudVisibilityState() const noexcept;
    bool RestoreHudVisibilityState(const HudVisibilityState& state);
    bool RequestDiagnosticHudVisibility(bool visible, DWORD timeoutMs = 5000);
    bool RequestDiagnosticHudVisibilityMatches(bool expected, DWORD timeoutMs = 5000);
    bool RequestDiagnosticHudMode(DiagnosticHudMode mode, DWORD timeoutMs = 5000);
    bool RequestDiagnosticHudState(const HudVisibilityState& state, DWORD timeoutMs = 5000);
    void CancelPendingHudVisibilityRequests();
    bool IntelVrrRangeFixEnabled() const noexcept { return intelVrrRangeFixEnabled_; }
    void SetIntelVrrRangeFixEnabled(bool enabled);
    bool DebugLoggingEnabled() const noexcept { return debugLoggingEnabled_; }
    void SetDebugLoggingEnabled(bool enabled);
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
    void RefreshMockHud();
    bool EnsureMockHud();
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
    bool ApplyDiagnosticHudVisibility(bool visible);
    bool ApplyDiagnosticHudMode(DiagnosticHudMode mode);
    clawhud::MsiEcHudTelemetry ReadHudEcTelemetry();
    void StartProductionEcSampling();
    void PauseProductionSamplingForSuspend();
    void CancelResumeRecovery();
    void StopProductionEcSampling(bool stopPresentMon = true,
        const wchar_t* reason = L"explicit-reset");
    void StartProductionPresentMonSampling(bool recoveryStart = false);
    void StopProductionPresentMonSampling(const wchar_t* reason = L"explicit-reset",
        bool clearLatestFps = false);
    void StartProductionFpsSampling();
    void StopProductionFpsSampling(bool clearTarget = true);
    void StartGraphicsApiProbe(DWORD processId);
    void StopGraphicsApiProbe();
    bool RequestHudOnUiThread(bool visible, const HudVisibilityState* restore, DWORD timeoutMs);
    void DiscardPendingHudVisibilityRequests();
    void DiscardPendingMicrosoftGameEvidence();
    void DiscardPendingProductionWindowEvents();
    void DiscardPendingProductionProcessExitEvents();
    void DiscardPendingGameRenderVerifierEvents();

    HINSTANCE instance_{};
    HANDLE instanceMutex_{};
    clawhud::HudSettingsStore hudSettingsStore_;
    TrayIcon tray_;
    std::unique_ptr<EcDiagnostic> ecDiagnostic_;
    std::unique_ptr<clawhud::IgclTelemetryDiagnostic> igclDiagnostic_;
    std::unique_ptr<clawhud::PresentMonApi2Diagnostic> presentMonApi2Diagnostic_;
    std::unique_ptr<VrrDiagnostic> vrrDiagnostic_;
    std::unique_ptr<clawhud::HudPresentation> hudPresentation_;
    std::unique_ptr<EcHelperClient> ecHudClient_;
    clawhud::MsiEcHudTelemetry ecHudTelemetry_{};
    clawhud::PresentMonTelemetryProvider presentMonTelemetryProvider_;
    std::optional<double> latestProcessFps_;
    // Retains the last valid FPS across short same-PID API2 misses (2 s window).
    clawhud::FpsStaleHold fpsStaleHold_;
    // Rate limiter for the once-per-second Displayed vs Presented FPS debug log.
    std::uint64_t lastFpsCompareLogTick_{};
    // Always mode: FPS target authority is the current foreground PID only,
    // fully decoupled from game detection. In-Game Only is unaffected.
    clawhud::AlwaysModeFpsTarget alwaysFpsTarget_;
    DWORD presentMonRestartPid_{};
    unsigned presentMonRestartAttempts_{};
    std::optional<clawhud::WindowsPowerTelemetry> latestPowerTelemetry_;
    clawhud::BatteryPowerEstimator batteryPowerEstimator_;
    bool batteryEcOnDc_{};
    bool batteryEcReadyLogged_{};
    std::optional<double> latestCpuUsagePercent_;
    std::optional<double> latestGpuUsagePercent_;
    std::optional<double> latestGpuClockMHz_;
    std::optional<std::uint64_t> latestGpuMemoryUsedBytes_;
    std::optional<std::uint64_t> latestSystemMemoryUsedBytes_;
    clawhud::IntelGraphicsApiProbe graphicsApiProbe_;
    std::optional<std::wstring> latestGraphicsApi_;
    DWORD graphicsApiProcessId_{};
    unsigned graphicsApiAttempts_{};
    ForegroundTracker foregroundTracker_;
    clawhud::WindowsGameIdentitySource windowsGameIdentitySource_;
    clawhud::HudLayoutOptions hudOptions_{};
    clawhud::HudFont hudFont_{clawhud::HudFont::Unispace};
    std::optional<bool> manualHudVisibilityOverride_;
    std::optional<DiagnosticHudMode> diagnosticHudMode_;
    bool mockHudEnabled_{};
    std::size_t mockFrameIndex_{};
    std::unique_ptr<SettingsWindow> settings_;
    bool exiting_{};
    std::wstring ecStatus_{ L"Idle" };
    std::wstring vrrStatus_{ L"Idle" };
    std::wstring igclStatus_{ L"Idle" };
    std::wstring presentMonApi2Status_{ L"Idle" };
    std::wstring executablePath_;
    int hudSizeOffset_{};
    bool hudHotkeyRegistered_{};
    bool ecHudSamplingActive_{};
    bool hudInitializedLogged_{};
    bool hudRenderFailureLogged_{};
    bool hudShowFailureLogged_{};
    bool hudHideFailureLogged_{};
    unsigned cpuUsageMissingCount_{};
    unsigned gpuUsageMissingCount_{};
    unsigned gpuClockMissingCount_{};
    unsigned gpuMemoryMissingCount_{};
    unsigned systemMemoryMissingCount_{};
    unsigned ecCpuTempMissingCount_{};
    unsigned ecFan1MissingCount_{};
    unsigned ecFan2MissingCount_{};
    unsigned ecTdpMissingCount_{};
    bool intelVrrRangeFixEnabled_{ true };
    bool suspended_{};
    bool resumeRecoveryActive_{};
    unsigned resumeRecoveryAttempts_{};
    clawhud::TweakStartupCoordinator tweakStartupCoordinator_;
    bool startWithWindows_{true};
    bool diagnosticsTabEnabled_{};
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
    clawhud::GameRenderVerifier gameRenderVerifier_;
    SteamRunningAppIdSource steamRunningAppIdSource_;
    std::uint32_t steamRunningAppId_{};
};
