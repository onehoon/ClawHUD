#pragma once

#include <windows.h>
#include <dbt.h>
#include <shellapi.h>

#include <functional>

// The two shell intents the Standalone tray emits. App supplies these at
// construction; TrayIcon does not know the application type.
struct TrayActions
{
    std::function<void()> openSettings;
    std::function<void()> exit;
};

class TrayIcon
{
public:
    explicit TrayIcon(TrayActions actions);
    ~TrayIcon();

    bool Create(HINSTANCE instance);
    void Destroy();
    HWND Window() const { return window_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    bool AddIcon();
    void ShowMenu();

    TrayActions actions_;
    HINSTANCE instance_{};
    HWND window_{};
    NOTIFYICONDATAW notifyIcon_{};
    UINT taskbarCreatedMessage_{};
    bool created_{};
};
