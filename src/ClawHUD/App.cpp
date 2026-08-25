#include "App.h"

#include "SettingsWindow.h"

#include <Velopack.hpp>

#include <cstdlib>
#include <memory>
#include <string>

namespace
{
constexpr UINT kSettingsDestroyed = WM_APP + 1;
constexpr wchar_t kInstanceMutexName[] = L"Local\\ClawHUD.SingleInstance";

void Log(const std::wstring& message)
{
    OutputDebugStringW((L"[ClawHUD] " + message + L"\n").c_str());
}

}

App::App(HINSTANCE instance) : instance_(instance), tray_(*this)
{
}

App::~App()
{
    settings_.reset();
    tray_.Destroy();
    if (instanceMutex_)
    {
        ReleaseMutex(instanceMutex_);
        CloseHandle(instanceMutex_);
    }
}

int App::Run()
{
    if (!AcquireSingleInstance()) return 0;
    CheckForUpdates();
    if (!tray_.Create(instance_)) return 1;
    return ProcessMessages();
}

bool App::AcquireSingleInstance()
{
    instanceMutex_ = CreateMutexW(nullptr, TRUE, kInstanceMutexName);
    if (!instanceMutex_)
    {
        Log(L"CreateMutex failed; exiting");
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(instanceMutex_);
        instanceMutex_ = nullptr;
        return false;
    }
    return true;
}

void App::CheckForUpdates()
{
    try
    {
        Velopack::UpdateManager manager(std::make_unique<Velopack::GithubSource>(CLAWHUD_UPDATE_REPOSITORY));
        const auto pending = manager.UpdatePendingRestart();
        if (pending.has_value())
        {
            Log(L"Velopack: applying pending update silently");
            manager.WaitExitThenApplyUpdates(*pending, true, true);
            std::exit(0);
        }
        const auto update = manager.CheckForUpdates();
        if (!update.has_value())
        {
            Log(L"Velopack: no update available");
            return;
        }
        Log(L"Velopack: update available; downloading silently");
        manager.DownloadUpdates(*update);
        manager.WaitExitThenApplyUpdates(*update, true, true);
        std::exit(0);
    }
    catch (const std::exception&)
    {
        Log(L"Velopack update unavailable; continuing with the installed version");
    }
}

void App::OpenSettings()
{
    if (settings_)
    {
        settings_->Show(instance_);
        return;
    }
    settings_ = std::make_unique<SettingsWindow>(*this);
    if (!settings_->Show(instance_)) settings_.reset();
}

void App::SettingsDestroyed()
{
    settings_.reset();
}

void App::Exit()
{
    if (exiting_) return;
    exiting_ = true;
    settings_.reset();
    tray_.Destroy();
    PostQuitMessage(0);
}

int App::ProcessMessages()
{
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.message == kSettingsDestroyed)
        {
            SettingsDestroyed();
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
