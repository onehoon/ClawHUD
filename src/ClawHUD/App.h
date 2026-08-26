#pragma once

#include <windows.h>

#include <memory>
#include <cstddef>
#include <optional>
#include <string>

#include "TrayIcon.h"
#include "EcDiagnostic.h"
#include "VrrDiagnostic.h"
#include "HudModel.h"
#include "ForegroundTracker.h"
#include "EcHelperClient.h"
#include "MsiEcHudTelemetry.h"
#include "PresentMonHudTelemetry.h"
#include "WindowsPowerTelemetry.h"
#include "WindowsUsageTelemetry.h"
#include "IntelGraphicsApiProbe.h"
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
constexpr UINT kResumeRecoveryIntervalMs = 500;
constexpr unsigned kResumeRecoveryMaxAttempts = 6;

constexpr bool ResumeRecoveryShouldStart(bool suspended, bool active) noexcept
{
    return suspended && !active;
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
    void StopDiagnostic();
    void HandleSystemSuspend();
    void HandleSystemResume();
    void TryResumeRecovery();
    const std::wstring& VrrStatus() const { return vrrStatus_; }
    bool StartMockHud();
    void StopMockHud();
    void RenderMockHud(bool allowHidden = false);
    void SampleProductionTelemetry();
    void SampleProductionBatteryTelemetry();
    void RenderProductionHud(bool allowHidden = false);
    void TryGraphicsApiProbe();
    bool MockHudVisible() const noexcept;
    bool MockHudEnabled() const noexcept { return mockHudEnabled_; }
    int HudSizeOffset() const noexcept { return hudSizeOffset_; }
    bool SetHudEnabled(bool enabled);
    const clawhud::HudLayoutOptions& HudOptions() const noexcept { return hudOptions_; }
    void SetHudAlignment(clawhud::HudAlignment alignment);
    void SetHudBackgroundMode(clawhud::HudBackgroundMode mode);
    void SetHudBackgroundOpacity(float opacity, bool persist = true);
    void SetHudSizeOffset(int offset);
    void TrackMockGameWindow(HWND window);
    void SetHudVisibilityMode(clawhud::HudVisibilityMode mode);
    bool IsHudAlwaysVisible() const noexcept;
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
    std::optional<clawhud::IntelVrrRunResult> IntelVrrLastResult() const;

private:
    bool AcquireSingleInstance();
    void CheckForUpdates();
    int ProcessMessages();
    void LoadHudSettings();
    void SaveHudSettings() const;
    clawhud::HudRenderOptions BuildHudRenderOptions() const;
    bool RecreateHudPresentation(bool restoreVisible);
    bool ApplyStartupRegistration() const;
    void RefreshMockHud();
    bool EnsureMockHud();
    void ReconcileHudVisibility();
    bool ApplyDiagnosticHudVisibility(bool visible);
    bool ApplyDiagnosticHudMode(DiagnosticHudMode mode);
    clawhud::MsiEcHudTelemetry ReadHudEcTelemetry();
    void StartProductionEcSampling();
    void PauseProductionSamplingForSuspend();
    void CancelResumeRecovery();
    void StopProductionEcSampling();
    void StartProductionPresentMonSampling();
    void StopProductionPresentMonSampling();
    void HandlePresentMonHudUpdate(DWORD processId, std::optional<double> displayedFps);
    void StartGraphicsApiProbe(DWORD processId);
    void StopGraphicsApiProbe();
    bool RequestHudOnUiThread(bool visible, const HudVisibilityState* restore, DWORD timeoutMs);
    void DiscardPendingHudVisibilityRequests();
    void DiscardPendingPresentMonHudUpdates();

    HINSTANCE instance_{};
    HANDLE instanceMutex_{};
    TrayIcon tray_;
    std::unique_ptr<EcDiagnostic> ecDiagnostic_;
    std::unique_ptr<VrrDiagnostic> vrrDiagnostic_;
    std::unique_ptr<clawhud::HudPresentation> hudPresentation_;
    std::unique_ptr<EcHelperClient> ecHudClient_;
    clawhud::MsiEcHudTelemetry ecHudTelemetry_{};
    std::unique_ptr<clawhud::PresentMonHudTelemetry> presentMonHudTelemetry_;
    std::optional<double> latestPresentMonDisplayedFps_;
    DWORD presentMonProcessId_{};
    std::optional<clawhud::WindowsPowerTelemetry> latestPowerTelemetry_;
    clawhud::WindowsUsageSampler usageSampler_;
    std::optional<clawhud::WindowsUsageTelemetry> latestUsageTelemetry_;
    clawhud::IntelGraphicsApiProbe graphicsApiProbe_;
    std::optional<std::wstring> latestGraphicsApi_;
    DWORD graphicsApiProcessId_{};
    unsigned graphicsApiAttempts_{};
    ForegroundTracker foregroundTracker_;
    clawhud::HudLayoutOptions hudOptions_{};
    std::optional<bool> manualHudVisibilityOverride_;
    std::optional<DiagnosticHudMode> diagnosticHudMode_;
    bool mockHudEnabled_{};
    std::size_t mockFrameIndex_{};
    std::unique_ptr<SettingsWindow> settings_;
    bool exiting_{};
    std::wstring ecStatus_{ L"Idle" };
    std::wstring vrrStatus_{ L"Idle" };
    std::wstring executablePath_;
    int hudSizeOffset_{};
    bool hudHotkeyRegistered_{};
    bool ecHudSamplingActive_{};
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
    bool diagnosticsTabEnabled_{};
};
