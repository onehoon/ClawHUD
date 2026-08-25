#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "TrayIcon.h"
#include "EcDiagnostic.h"
#include "VrrDiagnostic.h"

class SettingsWindow;
namespace clawhud { class HudPresentation; }

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
    bool MockHudVisible() const noexcept;

private:
    bool AcquireSingleInstance();
    void CheckForUpdates();
    int ProcessMessages();

    HINSTANCE instance_{};
    HANDLE instanceMutex_{};
    TrayIcon tray_;
    std::unique_ptr<EcDiagnostic> ecDiagnostic_;
    std::unique_ptr<VrrDiagnostic> vrrDiagnostic_;
    std::unique_ptr<clawhud::HudPresentation> hudPresentation_;
    std::unique_ptr<SettingsWindow> settings_;
    bool exiting_{};
    std::wstring ecStatus_{ L"Idle" };
    std::wstring vrrStatus_{ L"Idle" };
    std::wstring executablePath_;
};
