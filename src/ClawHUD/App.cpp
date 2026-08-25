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
    wchar_t path[MAX_PATH]{}; const DWORD length = GetModuleFileNameW(instance_, path, ARRAYSIZE(path));
    executablePath_.assign(path, length);
}

App::~App()
{
    vrrDiagnostic_.reset();
    ecDiagnostic_.reset();
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
    ecDiagnostic_ = std::make_unique<EcDiagnostic>(tray_.Window());
    vrrDiagnostic_ = std::make_unique<VrrDiagnostic>(*this, tray_.Window());
    return ProcessMessages();
}

bool App::StartEcDiagnostic()
{
    if (!ecDiagnostic_ || VrrDiagnosticRunning() || !ecDiagnostic_->Start()) return false;
    ecStatus_ = L"Running";
    return true;
}
void App::StopEcDiagnostic() { if (ecDiagnostic_) ecDiagnostic_->Stop(); }
bool App::EcDiagnosticRunning() const { return ecDiagnostic_ && ecDiagnostic_->Running(); }
void App::OpenDiagnosticLogFolder() { if (ecDiagnostic_) ecDiagnostic_->OpenLogFolder(); }
bool App::StartVrrDiagnostic()
{
    if (!vrrDiagnostic_ || EcDiagnosticRunning() || !vrrDiagnostic_->Start()) return false;
    vrrStatus_ = L"Waiting for game";
    if (settings_) settings_->RequestClose();
    return true;
}
void App::StopVrrDiagnostic() { if (vrrDiagnostic_) vrrDiagnostic_->Stop(); }
bool App::VrrDiagnosticRunning() const { return vrrDiagnostic_ && vrrDiagnostic_->Running(); }
bool App::DiagnosticRunning() const { return EcDiagnosticRunning() || VrrDiagnosticRunning(); }
void App::StopDiagnostic() { StopVrrDiagnostic(); StopEcDiagnostic(); }

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
    if (vrrDiagnostic_) vrrDiagnostic_->Stop();
    if (ecDiagnostic_) ecDiagnostic_->Stop();
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
        if (message.message == kEcDiagnosticStatus)
        {
            auto* status = reinterpret_cast<std::wstring*>(message.wParam);
            if (status) { ecStatus_ = *status; if (settings_) settings_->SetDiagnosticStatus(*status); }
            delete status;
            continue;
        }
        if (message.message == kVrrDiagnosticStatus)
        {
            auto* status = reinterpret_cast<std::wstring*>(message.wParam);
            if (status) { vrrStatus_ = *status; if (settings_) settings_->SetVrrStatus(*status); }
            delete status;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
