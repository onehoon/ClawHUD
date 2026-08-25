#pragma once

#include <windows.h>

#include <string>

class App;

class SettingsWindow
{
public:
    explicit SettingsWindow(App& app);
    ~SettingsWindow();

    bool Show(HINSTANCE instance);
    HWND Window() const { return window_; }
    void RequestClose() { if (window_) PostMessageW(window_, WM_CLOSE, 0, 0); }
    void SetDiagnosticStatus(const std::wstring& status);
    void SetVrrStatus(const std::wstring& status);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void CreateTabs();
    void ShowTab(int index);
    void UpdateHudControls();
    void UpdateDiagnosticButtons();

    App& app_;
    HINSTANCE instance_{};
    HWND window_{};
    HWND tabs_{};
    HWND generalPanel_{};
    HWND hudPanel_{};
    HWND diagnosticsPanel_{};
    HWND alignmentLeft_{};
    HWND alignmentCenter_{};
    HWND alignmentRight_{};
    HWND backgroundFull_{};
    HWND backgroundContent_{};
    HWND opacitySlider_{};
    HWND opacityLabel_{};
    HWND startEcButton_{};
    HWND openLogsButton_{};
    HWND diagnosticStatus_{};
    HWND startVrrButton_{};
    HWND stopVrrButton_{};
    HWND vrrStatus_{};
};
