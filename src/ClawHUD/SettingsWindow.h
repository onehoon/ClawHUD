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
    void SetDiagnosticStatus(const std::wstring& status);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void CreateTabs();
    void ShowTab(int index);

    App& app_;
    HINSTANCE instance_{};
    HWND window_{};
    HWND tabs_{};
    HWND diagnosticsPanel_{};
    HWND startEcButton_{};
    HWND openLogsButton_{};
    HWND diagnosticStatus_{};
};
