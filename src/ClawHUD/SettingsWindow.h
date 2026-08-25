#pragma once

#include <windows.h>

class App;

class SettingsWindow
{
public:
    explicit SettingsWindow(App& app);
    ~SettingsWindow();

    bool Show(HINSTANCE instance);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void CreateTabs();

    App& app_;
    HINSTANCE instance_{};
    HWND window_{};
    HWND tabs_{};
};
