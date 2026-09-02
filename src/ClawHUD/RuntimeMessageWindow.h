#pragma once

#include <windows.h>

class App;

// Owns the runtime-only hidden Win32 message window: the HWND that hosts the F8
// hotkey, WM_POWERBROADCAST suspend/resume notifications and the production
// WM_TIMER stream. It is deliberately independent of TrayIcon so a future
// no-tray launch mode can keep all runtime message infrastructure alive.
//
// This window is not the HUD presentation window and must never touch any HUD
// presentation styles or presentation code.
class RuntimeMessageWindow
{
public:
    explicit RuntimeMessageWindow(App& app);
    ~RuntimeMessageWindow();

    bool Create(HINSTANCE instance);
    void Destroy();
    HWND Window() const noexcept { return window_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    App& app_;
    HINSTANCE instance_{};
    HWND window_{};
    HPOWERNOTIFY suspendResumeNotification_{};
};
