#include "App.h"

#include "SettingsWindow.h"
#include "HudPresentation.h"

#include <Velopack.hpp>

#include <cstdlib>
#include <memory>
#include <string>

namespace
{
constexpr UINT kSettingsDestroyed = WM_APP + 1;
constexpr UINT kForegroundChanged = WM_APP + 2;
constexpr UINT_PTR kMockHudTimerId = 1;
constexpr UINT kMockHudTimerIntervalMs = 100;
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
    KillTimer(tray_.Window(), kMockHudTimerId);
    foregroundTracker_.Stop();
    hudPresentation_.reset();
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
    if (!foregroundTracker_.Start(tray_.Window(), kForegroundChanged,
        [this](bool) { ReconcileHudVisibility(); }))
        return 1;
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

bool App::StartMockHud()
{
    if (!EnsureMockHud()) return false;
    mockHudEnabled_ = true;
    mockFrameIndex_ = 0;
    ReconcileHudVisibility();
    return true;
}

bool App::EnsureMockHud()
{
    if (!hudPresentation_)
        hudPresentation_ = std::make_unique<clawhud::HudPresentation>();
    clawhud::HudRenderOptions options{};
    options.layout = hudOptions_;
    HRESULT hr = hudPresentation_->Initialize(instance_, options);
    if (FAILED(hr)) return false;
    hr = hudPresentation_->Render(clawhud::MakeGameDcSample(), options);
    if (FAILED(hr)) return false;
    return true;
}

void App::StopMockHud()
{
    mockHudEnabled_ = false;
    KillTimer(tray_.Window(), kMockHudTimerId);
    ReconcileHudVisibility();
}

void App::RenderMockHud()
{
    if (!mockHudEnabled_ || !hudPresentation_ || !hudPresentation_->Visible())
        return;
    auto snapshot = clawhud::MakeGameDcSample();
    const auto frame = mockFrameIndex_++;
    snapshot.renderFps = 58.0 + static_cast<double>(frame % 5);
    snapshot.cpuUsagePercent = 30.0 + static_cast<double>(frame % 10);
    snapshot.gpuUsagePercent = 90.0 + static_cast<double>(frame % 9);
    clawhud::HudRenderOptions options{};
    options.layout = hudOptions_;
    const HRESULT hr = hudPresentation_->Render(snapshot, options);
    if (FAILED(hr)) Log(L"Mock HUD redraw failed");
}

bool App::MockHudVisible() const noexcept
{
    return hudPresentation_ && hudPresentation_->Visible();
}

void App::TrackMockGameWindow(HWND window)
{
    DWORD processId{};
    if (window)
        GetWindowThreadProcessId(window, &processId);
    if (!processId)
        return;
    foregroundTracker_.SetTrackedProcessId(processId);
    if (EnsureMockHud())
    {
        mockHudEnabled_ = true;
        ReconcileHudVisibility();
    }
}

void App::SetHudVisibilityMode(clawhud::HudVisibilityMode mode)
{
    hudOptions_.visibilityMode = mode;
    ReconcileHudVisibility();
}

bool App::IsHudAlwaysVisible() const noexcept
{
    return hudOptions_.visibilityMode == clawhud::HudVisibilityMode::Always;
}

void App::ReconcileHudVisibility()
{
    if (!hudPresentation_)
        return;
    const bool show = mockHudEnabled_ && clawhud::ShouldShowHud(
        hudOptions_.visibilityMode, foregroundTracker_.ForegroundIsTrackedProcess());
    if (show)
    {
        const HRESULT hr = hudPresentation_->Show();
        if (SUCCEEDED(hr))
            SetTimer(tray_.Window(), kMockHudTimerId, kMockHudTimerIntervalMs, nullptr);
        else
            Log(L"Mock HUD show failed");
    }
    else
    {
        hudPresentation_->Hide();
        KillTimer(tray_.Window(), kMockHudTimerId);
    }
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
    KillTimer(tray_.Window(), kMockHudTimerId);
    foregroundTracker_.Stop();
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
        if (message.message == kForegroundChanged)
        {
            foregroundTracker_.Reconcile();
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
