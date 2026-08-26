#pragma once

#include <windows.h>
#include <dbt.h>
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
    bool AddIcon();
    void ShowMenu();

    App& app_;
    HINSTANCE instance_{};
    HWND window_{};
    NOTIFYICONDATAW notifyIcon_{};
    HPOWERNOTIFY suspendResumeNotification_{};
    UINT taskbarCreatedMessage_{};
    bool created_{};
};
