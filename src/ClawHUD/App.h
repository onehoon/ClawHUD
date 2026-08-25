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

class SettingsWindow;
namespace clawhud { class HudPresentation; }

constexpr int kHudToggleHotkeyId = 1;

class App
{
public:
    explicit App(HINSTANCE instance);
    ~App();

    int Run();
    void OpenSettings();
    void Exit();
    void SettingsDestroyed();
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
    const std::wstring& VrrStatus() const { return vrrStatus_; }
    bool StartMockHud();
    void StopMockHud();
    void RenderMockHud();
    bool MockHudVisible() const noexcept;
    bool MockHudEnabled() const noexcept { return mockHudEnabled_; }
    const clawhud::HudLayoutOptions& HudOptions() const noexcept { return hudOptions_; }
    void SetHudAlignment(clawhud::HudAlignment alignment);
    void SetHudBackgroundMode(clawhud::HudBackgroundMode mode);
    void SetHudBackgroundOpacity(float opacity, bool persist = true);
    void TrackMockGameWindow(HWND window);
    void SetHudVisibilityMode(clawhud::HudVisibilityMode mode);
    bool IsHudAlwaysVisible() const noexcept;
    void HandleHudToggleHotkey();
    HudVisibilityState CaptureHudVisibilityState() const noexcept;
    bool RestoreHudVisibilityState(const HudVisibilityState& state);
    bool RequestDiagnosticHudVisibility(bool visible, DWORD timeoutMs = 5000);
    bool RequestDiagnosticHudVisibilityMatches(bool expected, DWORD timeoutMs = 5000);
    bool RequestDiagnosticHudState(const HudVisibilityState& state, DWORD timeoutMs = 5000);
    void CancelPendingHudVisibilityRequests();

private:
    bool AcquireSingleInstance();
    void CheckForUpdates();
    int ProcessMessages();
    void LoadHudSettings();
    void SaveHudSettings() const;
    void RefreshMockHud();
    bool EnsureMockHud();
    void ReconcileHudVisibility();
    bool ApplyDiagnosticHudVisibility(bool visible);
    bool RequestHudOnUiThread(bool visible, const HudVisibilityState* restore, DWORD timeoutMs);
    void DiscardPendingHudVisibilityRequests();

    HINSTANCE instance_{};
    HANDLE instanceMutex_{};
    TrayIcon tray_;
    std::unique_ptr<EcDiagnostic> ecDiagnostic_;
    std::unique_ptr<VrrDiagnostic> vrrDiagnostic_;
    std::unique_ptr<clawhud::HudPresentation> hudPresentation_;
    ForegroundTracker foregroundTracker_;
    clawhud::HudLayoutOptions hudOptions_{};
    std::optional<bool> manualHudVisibilityOverride_;
    bool mockHudEnabled_{};
    std::size_t mockFrameIndex_{};
    std::unique_ptr<SettingsWindow> settings_;
    bool exiting_{};
    std::wstring ecStatus_{ L"Idle" };
    std::wstring vrrStatus_{ L"Idle" };
    std::wstring executablePath_;
    bool hudHotkeyRegistered_{};
};
