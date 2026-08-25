#include "App.h"

#include "SettingsWindow.h"
#include "HudPresentation.h"

#include <Velopack.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <shlobj.h>
#include <string>

namespace
{
constexpr UINT kSettingsDestroyed = WM_APP + 1;
constexpr UINT kForegroundChanged = WM_APP + 2;
constexpr UINT kHudVisibilityRequest = WM_APP + 3;
constexpr UINT_PTR kMockHudTimerId = 1;
constexpr UINT kMockHudTimerIntervalMs = 100;
constexpr wchar_t kInstanceMutexName[] = L"Local\\ClawHUD.SingleInstance";

struct HudVisibilityRequest
{
    bool restore{};
    bool visible{};
    HudVisibilityState state{};
    HANDLE complete{};
    bool result{};
    std::atomic_bool cancelled{};
    ~HudVisibilityRequest() { if (complete) CloseHandle(complete); }
};

std::wstring HudSettingsPath()
{
    PWSTR localAppData{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData)))
        return {};
    std::wstring path(localAppData);
    CoTaskMemFree(localAppData);
    return path + L"\\ClawHUD\\settings.ini";
}

std::wstring ReadHudSetting(const std::wstring& path, const wchar_t* key,
    const wchar_t* fallback)
{
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"HUD", key, fallback, value, ARRAYSIZE(value), path.c_str());
    return value;
}

void Log(const std::wstring& message)
{
    OutputDebugStringW((L"[ClawHUD] " + message + L"\n").c_str());
}

}

App::App(HINSTANCE instance) : instance_(instance), tray_(*this)
{
    wchar_t path[MAX_PATH]{}; const DWORD length = GetModuleFileNameW(instance_, path, ARRAYSIZE(path));
    executablePath_.assign(path, length);
    LoadHudSettings();
}

App::~App()
{
    KillTimer(tray_.Window(), kMockHudTimerId);
    foregroundTracker_.Stop();
    if (vrrDiagnostic_) vrrDiagnostic_->Stop();
    DiscardPendingHudVisibilityRequests();
    vrrDiagnostic_.reset();
    if (hudHotkeyRegistered_ && tray_.Window())
        UnregisterHotKey(tray_.Window(), kHudToggleHotkeyId);
    hudHotkeyRegistered_ = false;
    hudPresentation_.reset();
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
    hudHotkeyRegistered_ = RegisterHotKey(tray_.Window(), kHudToggleHotkeyId,
        MOD_NOREPEAT, VK_F8) != FALSE;
    if (!hudHotkeyRegistered_)
        Log(L"RegisterHotKey(F8) failed; continuing without the global HUD toggle");
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
    manualHudVisibilityOverride_.reset();
    KillTimer(tray_.Window(), kMockHudTimerId);
    ReconcileHudVisibility();
}

void App::SetHudAlignment(clawhud::HudAlignment alignment)
{
    if (hudOptions_.alignment == alignment)
        return;
    hudOptions_.alignment = alignment;
    SaveHudSettings();
    RefreshMockHud();
}

void App::SetHudBackgroundMode(clawhud::HudBackgroundMode mode)
{
    if (hudOptions_.backgroundMode == mode)
        return;
    hudOptions_.backgroundMode = mode;
    SaveHudSettings();
    RefreshMockHud();
}

void App::SetHudBackgroundOpacity(float opacity, bool persist)
{
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (hudOptions_.backgroundOpacity == opacity)
    {
        if (persist) SaveHudSettings();
        return;
    }
    hudOptions_.backgroundOpacity = opacity;
    if (persist) SaveHudSettings();
    RefreshMockHud();
}

void App::RefreshMockHud()
{
    if (mockHudEnabled_ && hudPresentation_ && hudPresentation_->Visible())
        RenderMockHud();
}

void App::RenderMockHud()
{
    if (!mockHudEnabled_ || !hudPresentation_ || !hudPresentation_->Visible())
        return;
    auto snapshot = clawhud::MakeGameDcSample();
    const auto frame = mockFrameIndex_++;
    const auto phase = frame % 3;
    snapshot.renderFps = phase == 1 ? 100.0 : 99.0;
    snapshot.cpuUsagePercent = phase == 0 ? 9.0 : phase == 1 ? 10.0 : 100.0;
    snapshot.gpuUsagePercent = phase == 1 ? 100.0 : 99.0;
    snapshot.cpuPackagePowerW = phase == 1 ? 10.1 : 9.8;
    snapshot.fan1Rpm = phase == 1 ? 1000 : 999;
    snapshot.fan2Rpm = phase == 1 ? 1000 : 999;
    snapshot.batteryPercent = phase == 0 ? 9 : phase == 1 ? 10 : 100;
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

void App::HandleHudToggleHotkey()
{
    if (VrrDiagnosticRunning())
    {
        Log(L"F8 ignored while VRR diagnostic is running");
        return;
    }
    if (!mockHudEnabled_)
    {
        if (!EnsureMockHud())
        {
            Log(L"F8 HUD ON initialization failed");
            return;
        }
        mockHudEnabled_ = true;
        mockFrameIndex_ = 0;
    }
    manualHudVisibilityOverride_ = !MockHudVisible();
    ReconcileHudVisibility();
}

HudVisibilityState App::CaptureHudVisibilityState() const noexcept
{
    return { mockHudEnabled_, manualHudVisibilityOverride_, MockHudVisible() };
}

bool App::ApplyDiagnosticHudVisibility(bool visible)
{
    manualHudVisibilityOverride_ = visible;
    if (visible && !mockHudEnabled_)
    {
        if (!EnsureMockHud()) return false;
        mockHudEnabled_ = true;
        mockFrameIndex_ = 0;
    }
    ReconcileHudVisibility();
    return MockHudVisible() == visible;
}

bool App::RestoreHudVisibilityState(const HudVisibilityState& state)
{
    manualHudVisibilityOverride_ = state.manualOverride;
    if (state.mockHudEnabled)
    {
        if (!EnsureMockHud()) return false;
        mockHudEnabled_ = true;
    }
    else
    {
        mockHudEnabled_ = false;
    }
    ReconcileHudVisibility();
    return MockHudVisible() == state.visible;
}

bool App::RequestHudOnUiThread(bool visible, const HudVisibilityState* restore, DWORD timeoutMs)
{
    if (!tray_.Window()) return false;
    auto request = std::make_shared<HudVisibilityRequest>();
    request->complete = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!request->complete) return false;
    request->restore = restore != nullptr;
    request->visible = visible;
    if (restore) request->state = *restore;
    auto* payload = new std::shared_ptr<HudVisibilityRequest>(request);
    if (!PostMessageW(tray_.Window(), kHudVisibilityRequest,
        reinterpret_cast<WPARAM>(payload), 0))
    {
        delete payload;
        return false;
    }
    const bool completed = WaitForSingleObject(request->complete, timeoutMs) == WAIT_OBJECT_0;
    if (!completed) request->cancelled = true;
    return completed && request->result;
}

bool App::RequestDiagnosticHudVisibility(bool visible, DWORD timeoutMs)
{
    return RequestHudOnUiThread(visible, nullptr, timeoutMs);
}

bool App::RequestDiagnosticHudState(const HudVisibilityState& state, DWORD timeoutMs)
{
    return RequestHudOnUiThread(false, &state, timeoutMs);
}

void App::DiscardPendingHudVisibilityRequests()
{
    MSG message{};
    while (PeekMessageW(&message, tray_.Window(), kHudVisibilityRequest,
        kHudVisibilityRequest, PM_REMOVE))
    {
        auto* payload = reinterpret_cast<std::shared_ptr<HudVisibilityRequest>*>(message.wParam);
        if (payload)
        {
            (*payload)->cancelled = true;
            SetEvent((*payload)->complete);
            delete payload;
        }
    }
}

void App::ReconcileHudVisibility()
{
    if (!hudPresentation_)
        return;
    const bool resolvedShow = mockHudEnabled_ && (manualHudVisibilityOverride_.has_value()
        ? *manualHudVisibilityOverride_
        : clawhud::ShouldShowHud(hudOptions_.visibilityMode,
            foregroundTracker_.ForegroundIsTrackedProcess()));
    if (resolvedShow)
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

void App::LoadHudSettings()
{
    const auto path = HudSettingsPath();
    if (path.empty()) return;
    const auto alignment = ReadHudSetting(path, L"Alignment", L"Center");
    if (alignment == L"Left") hudOptions_.alignment = clawhud::HudAlignment::Left;
    else if (alignment == L"Right") hudOptions_.alignment = clawhud::HudAlignment::Right;
    const auto background = ReadHudSetting(path, L"BackgroundWidth", L"FullWidth");
    if (background == L"ContentWidth") hudOptions_.backgroundMode = clawhud::HudBackgroundMode::ContentWidth;
    else if (background == L"FullWidth") hudOptions_.backgroundMode = clawhud::HudBackgroundMode::FullWidth;
    const auto opacityText = ReadHudSetting(path, L"BackgroundOpacity", L"50");
    wchar_t* end{};
    const long parsed = std::wcstol(opacityText.c_str(), &end, 10);
    const bool valid = end != opacityText.c_str() && end && *end == L'\0';
    const long percent = std::clamp(valid ? parsed : 50L, 0L, 100L);
    hudOptions_.backgroundOpacity = static_cast<float>(percent) / 100.0f;
}

void App::SaveHudSettings() const
{
    const auto path = HudSettingsPath();
    if (path.empty()) return;
    const auto separator = path.find_last_of(L'\\');
    if (separator != std::wstring::npos)
        CreateDirectoryW(path.substr(0, separator).c_str(), nullptr);
    const wchar_t* alignment = hudOptions_.alignment == clawhud::HudAlignment::Left ? L"Left" :
        hudOptions_.alignment == clawhud::HudAlignment::Right ? L"Right" : L"Center";
    const wchar_t* background = hudOptions_.backgroundMode == clawhud::HudBackgroundMode::ContentWidth
        ? L"ContentWidth" : L"FullWidth";
    wchar_t opacity[8]{};
    swprintf_s(opacity, L"%d", static_cast<int>(hudOptions_.backgroundOpacity * 100.0f + 0.5f));
    WritePrivateProfileStringW(L"HUD", L"Alignment", alignment, path.c_str());
    WritePrivateProfileStringW(L"HUD", L"BackgroundWidth", background, path.c_str());
    WritePrivateProfileStringW(L"HUD", L"BackgroundOpacity", opacity, path.c_str());
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
    DiscardPendingHudVisibilityRequests();
    if (hudHotkeyRegistered_ && tray_.Window())
    {
        UnregisterHotKey(tray_.Window(), kHudToggleHotkeyId);
        hudHotkeyRegistered_ = false;
    }
    settings_.reset();
    tray_.Destroy();
    PostQuitMessage(0);
}

int App::ProcessMessages()
{
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.message == kHudVisibilityRequest)
        {
            auto* payload = reinterpret_cast<std::shared_ptr<HudVisibilityRequest>*>(message.wParam);
            if (payload)
            {
                auto request = *payload;
                delete payload;
                if (!request->cancelled.exchange(true))
                    request->result = request->restore
                        ? RestoreHudVisibilityState(request->state)
                        : ApplyDiagnosticHudVisibility(request->visible);
                SetEvent(request->complete);
            }
            continue;
        }
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
