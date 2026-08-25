#pragma once

#include <windows.h>

#include <memory>

#include "TrayIcon.h"

class SettingsWindow;

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

private:
    bool AcquireSingleInstance();
    void CheckForUpdates();
    int ProcessMessages();

    HINSTANCE instance_{};
    HANDLE instanceMutex_{};
    TrayIcon tray_;
    std::unique_ptr<SettingsWindow> settings_;
    bool exiting_{};
};
