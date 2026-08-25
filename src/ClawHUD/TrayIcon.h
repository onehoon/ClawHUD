#pragma once

#include <windows.h>
#include <shellapi.h>

class App;

class TrayIcon
{
public:
    explicit TrayIcon(App& app);
    ~TrayIcon();

    bool Create(HINSTANCE instance);
    void Destroy();
    HWND Window() const { return window_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void ShowMenu();

    App& app_;
    HINSTANCE instance_{};
    HWND window_{};
    NOTIFYICONDATAW notifyIcon_{};
    bool created_{};
};
