#include "App.h"

#include "SettingsWindow.h"
#include "HudPresentation.h"
#include "Tweaks/IntelVrr/IntelVrrResultStore.h"
#include "SupportedHardware.h"
#include "HudSize.h"
#include "UninstallCleanup.h"
#include "RuntimeLogger.h"
#include "Version.h"

#include <Velopack.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <shobjidl.h>
#include <shlobj.h>
#include <string>

namespace
{
constexpr UINT kSettingsDestroyed = WM_APP + 1;
constexpr UINT kForegroundChanged = WM_APP + 2;
constexpr UINT kHudVisibilityRequest = WM_APP + 3;
constexpr UINT kPresentMonHudUpdate = WM_APP + 4;
constexpr UINT kMockHudTimerIntervalMs = 100;
constexpr UINT kUsageSamplingIntervalMs = 1000;
constexpr UINT kBatteryHudTimerIntervalMs = 5000;
constexpr UINT kGraphicsApiRetryIntervalMs = 500;
constexpr unsigned kGraphicsApiMaxAttempts = 5;
constexpr wchar_t kInstanceMutexName[] = L"Local\\ClawHUD.SingleInstance";
struct HudVisibilityRequest
{
    bool restore{};
    bool query{};
    bool modeRequest{};
    bool visible{};
    DiagnosticHudMode mode{ DiagnosticHudMode::Off };
    HudVisibilityState state{};
    HANDLE complete{};
    bool result{};
    std::atomic_bool cancelled{};
    ~HudVisibilityRequest() { if (complete) CloseHandle(complete); }
};

struct PresentMonHudUpdate
{
    DWORD processId{};
    std::optional<double> displayedFps;
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

bool ReadBoolSetting(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool fallback)
{
    wchar_t value[16]{};
    GetPrivateProfileStringW(section, key, fallback ? L"1" : L"0", value, ARRAYSIZE(value), path.c_str());
    return wcstol(value, nullptr, 10) != 0;
}

void Log(const std::wstring& message)
{
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, message);
}

std::wstring HexHresult(HRESULT hr)
{
    wchar_t buffer[11]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

bool ProcessAlive(DWORD processId)
{
    if (!processId)
        return false;
    HANDLE process = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return false;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

}

App::App(HINSTANCE instance) : instance_(instance), tray_(*this)
{
    clawhud::RuntimeLogger::Initialize();
    wchar_t path[MAX_PATH]{}; const DWORD length = GetModuleFileNameW(instance_, path, ARRAYSIZE(path));
    executablePath_.assign(path, length);
    LoadHudSettings();
    Log(L"ClawHUD started version=" CLAWHUD_VERSION L" pid=" +
        std::to_wstring(GetCurrentProcessId()));
    Log(L"Runtime settings HUDEnabled=" + std::to_wstring(mockHudEnabled_ ? 1 : 0) +
        L" HUDSizeOffset=" + std::to_wstring(hudSizeOffset_) +
        L" StartWithWindows=" + std::to_wstring(startWithWindows_ ? 1 : 0));
}

App::~App()
{
    Log(L"ClawHUD exiting");
    KillTimer(tray_.Window(), kMockHudTimerId);
    StopProductionEcSampling();
    StopGraphicsApiProbe();
    foregroundTracker_.Stop();
    if (vrrDiagnostic_) vrrDiagnostic_->Stop();
    DiscardPendingHudVisibilityRequests();
    StopProductionPresentMonSampling();
    DiscardPendingPresentMonHudUpdates();
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
    const auto hardware = CheckSupportedHardware();
    if (hardware == HardwareSupport::Unsupported)
    {
        MessageBoxW(nullptr, L"This device is not supported by ClawHUD.", L"ClawHUD",
            MB_OK | MB_ICONWARNING);
        return 0;
    }
    if (hardware == HardwareSupport::Indeterminate)
    {
        MessageBoxW(nullptr,
            L"This device could not be identified. ClawHUD will exit without making any changes.",
            L"ClawHUD", MB_OK | MB_ICONWARNING);
        return 0;
    }
    if (!ApplyStartupRegistration())
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"Startup registration failed");
    if (!tray_.Create(instance_))
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"Tray initialization failed");
        return 1;
    }
    hudHotkeyRegistered_ = RegisterHotKey(tray_.Window(), kHudToggleHotkeyId,
        MOD_NOREPEAT, VK_F8) != FALSE;
    if (!hudHotkeyRegistered_)
        Log(L"RegisterHotKey(F8) failed; continuing without the global HUD toggle");
    ecDiagnostic_ = std::make_unique<EcDiagnostic>(tray_.Window());
    vrrDiagnostic_ = std::make_unique<VrrDiagnostic>(*this, tray_.Window());
    if (!foregroundTracker_.Start(tray_.Window(), kForegroundChanged,
        [this](bool matches)
        {
            if (matches)
                Log(L"Foreground target pid=" +
                    std::to_wstring(foregroundTracker_.TrackedProcessId()));
            else
                Log(L"Foreground target cleared");
            ReconcileHudVisibility();
        }))
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"Foreground tracker initialization failed");
        return 1;
    }
    tweakStartupCoordinator_.Start(intelVrrRangeFixEnabled_);
    return ProcessMessages();
}

void App::SetIntelVrrRangeFixEnabled(bool enabled)
{
    intelVrrRangeFixEnabled_ = enabled;
    const auto path = HudSettingsPath();
    if (path.empty()) return;
    const auto separator = path.find_last_of(L'\\');
    if (separator != std::wstring::npos) CreateDirectoryW(path.substr(0, separator).c_str(), nullptr);
    if (!WritePrivateProfileStringW(L"Tweaks", L"IntelVrrRangeFixEnabled",
        enabled ? L"1" : L"0", path.c_str()))
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"Settings save failed key=IntelVrrRangeFixEnabled");
}

std::optional<clawhud::IntelVrrRunResult> App::IntelVrrLastResult() const
{
    return clawhud::IntelVrrResultStore::Load();
}

void App::SetStartWithWindows(bool enabled)
{
    if (startWithWindows_ == enabled)
        return;
    const bool previous = startWithWindows_;
    startWithWindows_ = enabled;
    if (!ApplyStartupRegistration())
    {
        startWithWindows_ = previous;
        Log(enabled ? L"Startup registration failed" : L"Startup shortcut removal failed");
        return;
    }
    SaveHudSettings();
}

bool App::StartEcDiagnostic()
{
    if (!ecDiagnostic_ || VrrDiagnosticRunning() || ecHudSamplingActive_ || !ecDiagnostic_->Start()) return false;
    ecStatus_ = L"Running";
    return true;
}
void App::StopEcDiagnostic() { if (ecDiagnostic_) ecDiagnostic_->Stop(); }
bool App::EcDiagnosticRunning() const { return ecDiagnostic_ && ecDiagnostic_->Running(); }
void App::OpenDiagnosticLogFolder() { if (ecDiagnostic_) ecDiagnostic_->OpenLogFolder(); }
bool App::StartVrrDiagnostic()
{
    if (!vrrDiagnostic_ || EcDiagnosticRunning()) return false;
    StopProductionEcSampling();
    StopGraphicsApiProbe();
    if (!vrrDiagnostic_->Start())
    {
        ReconcileHudVisibility();
        if (const DWORD processId = foregroundTracker_.TrackedProcessId();
            processId && ProcessAlive(processId) && mockHudEnabled_)
            StartGraphicsApiProbe(processId);
        return false;
    }
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
    if (VrrDiagnosticRunning())
    {
        Log(L"Show Mock HUD ignored while VRR diagnostic is running");
        return false;
    }
    if (!EnsureMockHud()) return false;
    if (!mockHudEnabled_) Log(L"HUD enabled");
    mockHudEnabled_ = true;
    mockFrameIndex_ = 0;
    if (const DWORD processId = foregroundTracker_.TrackedProcessId())
        StartGraphicsApiProbe(processId);
    ReconcileHudVisibility();
    return true;
}

bool App::EnsureMockHud()
{
    if (!hudPresentation_)
        hudPresentation_ = std::make_unique<clawhud::HudPresentation>();
    const auto options = BuildHudRenderOptions();
    HRESULT hr = hudPresentation_->Initialize(instance_, options);
    if (FAILED(hr))
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"HUD initialization failed hr=" + HexHresult(hr));
        return false;
    }
    if (!hudInitializedLogged_)
    {
        Log(L"HUD initialized");
        hudInitializedLogged_ = true;
    }
    if (diagnosticHudMode_.has_value())
    {
        hr = hudPresentation_->Render(clawhud::MakeGameDcSample(), options);
        if (FAILED(hr))
        {
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
                L"HUD render failed hr=" + HexHresult(hr));
            return false;
        }
    }
    return true;
}

void App::StopMockHud()
{
    if (VrrDiagnosticRunning())
    {
        Log(L"Hide Mock HUD ignored while VRR diagnostic is running");
        return;
    }
    if (mockHudEnabled_) Log(L"HUD disabled");
    mockHudEnabled_ = false;
    manualHudVisibilityOverride_.reset();
    KillTimer(tray_.Window(), kMockHudTimerId);
    StopProductionEcSampling();
    StopGraphicsApiProbe();
    ReconcileHudVisibility();
}

bool App::SetHudEnabled(bool enabled)
{
    if (VrrDiagnosticRunning())
    {
        Log(L"HUD enable change ignored while VRR diagnostic is running");
        return false;
    }
    if (!enabled)
    {
        StopMockHud();
        return true;
    }
    if (!EnsureMockHud()) return false;
    if (!mockHudEnabled_) Log(L"HUD enabled");
    mockHudEnabled_ = true;
    mockFrameIndex_ = 0;
    manualHudVisibilityOverride_.reset();
    ReconcileHudVisibility();
    return true;
}

void App::SetHudAlignment(clawhud::HudAlignment alignment)
{
    if (VrrDiagnosticRunning())
    {
        Log(L"HUD alignment change ignored while VRR diagnostic is running");
        return;
    }
    if (hudOptions_.alignment == alignment)
        return;
    hudOptions_.alignment = alignment;
    SaveHudSettings();
    if (hudOptions_.backgroundMode == clawhud::HudBackgroundMode::ContentWidth)
    {
        const bool restoreVisible = hudPresentation_ &&
            hudPresentation_->Initialized() && hudPresentation_->Visible();
        if (!RecreateHudPresentation(restoreVisible))
            Log(L"HUD alignment presentation recreation failed");
    }
    else
    {
        RefreshMockHud();
    }
}

void App::SetHudBackgroundMode(clawhud::HudBackgroundMode mode)
{
    if (VrrDiagnosticRunning())
    {
        Log(L"HUD background mode change ignored while VRR diagnostic is running");
        return;
    }
    if (hudOptions_.backgroundMode == mode)
        return;
    hudOptions_.backgroundMode = mode;
    SaveHudSettings();
    const bool restoreVisible = hudPresentation_ &&
        hudPresentation_->Initialized() && hudPresentation_->Visible();
    if (!RecreateHudPresentation(restoreVisible))
        Log(L"HUD background mode presentation recreation failed");
}

void App::SetHudBackgroundOpacity(float opacity, bool persist)
{
    if (VrrDiagnosticRunning())
    {
        Log(L"HUD background opacity change ignored while VRR diagnostic is running");
        return;
    }
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

void App::SetHudSizeOffset(int offset)
{
    if (VrrDiagnosticRunning())
    {
        Log(L"HUD size change ignored while VRR diagnostic is running");
        return;
    }
    offset = clawhud::ClampHudSizeOffset(offset);
    if (hudSizeOffset_ == offset)
        return;

    const int previousOffset = hudSizeOffset_;
    const bool restoreVisible = hudPresentation_ &&
        hudPresentation_->Initialized() && hudPresentation_->Visible();
    hudSizeOffset_ = offset;
    const bool recreated = RecreateHudPresentation(restoreVisible);
    hudSizeOffset_ = clawhud::CommitHudSizeOffsetAfterRecreation(
        previousOffset, offset, recreated);
    if (!recreated)
    {
        RecreateHudPresentation(restoreVisible);
        Log(L"HUD size change rolled back after presentation recreation failure");
        return;
    }
    Log(L"HUD presentation recreated");
    SaveHudSettings();
}

clawhud::HudRenderOptions App::BuildHudRenderOptions() const
{
    return clawhud::BuildHudRenderOptionsForSize(hudSizeOffset_, hudOptions_);
}

bool App::RecreateHudPresentation(bool restoreVisible)
{
    if (!hudPresentation_)
        return true;

    const bool wasInitialized = hudPresentation_->Initialized();
    if (!wasInitialized && !mockHudEnabled_)
        return true;

    const auto options = BuildHudRenderOptions();
    hudPresentation_->Shutdown();
    HRESULT hr = hudPresentation_->Initialize(instance_, options);
    if (FAILED(hr))
    {
        Log(L"HUD presentation recreation failed");
        return false;
    }
    if (mockHudEnabled_)
    {
        if (diagnosticHudMode_.has_value())
            RenderMockHud(true);
        else
            RenderProductionHud(true);
    }
    if (clawhud::ShouldRestoreHudVisibility(restoreVisible))
    {
        hr = hudPresentation_->Show();
        if (FAILED(hr))
        {
            Log(L"HUD size visibility restore failed");
            return false;
        }
    }
    return true;
}

void App::RefreshMockHud()
{
    if (mockHudEnabled_ && hudPresentation_ && hudPresentation_->Visible())
    {
        if (diagnosticHudMode_.has_value())
            RenderMockHud();
        else
            RenderProductionHud();
    }
}

void App::RenderMockHud(bool allowHidden)
{
    if (!mockHudEnabled_ || !hudPresentation_ ||
        (!allowHidden && !hudPresentation_->Visible()))
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
    const auto options = BuildHudRenderOptions();
    const HRESULT hr = hudPresentation_->Render(snapshot, options);
    if (FAILED(hr))
    {
        if (!hudRenderFailureLogged_)
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error, L"HUD render failed");
        hudRenderFailureLogged_ = true;
    }
    else
        hudRenderFailureLogged_ = false;
}

clawhud::MsiEcHudTelemetry App::ReadHudEcTelemetry()
{
    clawhud::MsiEcHudTelemetry result{};
    if (!ecHudClient_)
        ecHudClient_ = std::make_unique<EcHelperClient>();

    std::vector<std::uint8_t> payload;
    if (ecHudClient_->ReadTemperature(payload))
        result.cpuTempC = clawhud::DecodeCpuTempC(payload);

    payload.clear();
    if (ecHudClient_->ReadFan(payload))
    {
        if (const auto fans = clawhud::DecodeFanTelemetry(payload))
        {
            result.fan1Rpm = fans->fan1Rpm;
            result.fan2Rpm = fans->fan2Rpm;
            result.hudFanRpm = clawhud::SelectHudFanRpm(
                result.fan1Rpm, result.fan2Rpm);
        }
    }

    payload.clear();
    if (ecHudClient_->ReadData(221, payload))
        result.cpuPackagePowerW = clawhud::DecodeCpuPackagePowerW(payload);
    return result;
}

void App::RenderProductionHud(bool allowHidden)
{
    if (!mockHudEnabled_ || !hudPresentation_ ||
        (!allowHidden && !hudPresentation_->Visible()) ||
        diagnosticHudMode_.has_value())
        return;

    clawhud::HudTelemetrySnapshot snapshot{};
    snapshot.cpuTemperatureC = ecHudTelemetry_.cpuTempC;
    snapshot.cpuPackagePowerW = ecHudTelemetry_.cpuPackagePowerW
        ? std::optional<double>(*ecHudTelemetry_.cpuPackagePowerW) : std::nullopt;
    snapshot.fan1Rpm = ecHudTelemetry_.fan1Rpm;
    snapshot.fan2Rpm = ecHudTelemetry_.fan2Rpm;
    snapshot.graphicsApi = latestGraphicsApi_;
    snapshot.presentMonDisplayedFps = latestPresentMonDisplayedFps_;
    if (latestUsageTelemetry_)
    {
        snapshot.cpuUsagePercent = latestUsageTelemetry_->cpuUsagePercent;
        snapshot.gpuUsagePercent = latestUsageTelemetry_->gpuUsagePercent;
        snapshot.gpuMemoryUsedBytes = latestUsageTelemetry_->intelGpuMemoryUsedBytes;
    }
    if (latestPowerTelemetry_)
    {
        snapshot.batteryPercent = latestPowerTelemetry_->batteryPercent;
        snapshot.onBattery = latestPowerTelemetry_->onBattery.value_or(false);
        if (snapshot.onBattery)
            snapshot.remainingMinutes = latestPowerTelemetry_->remainingMinutes;
    }

    const auto options = BuildHudRenderOptions();
    const HRESULT hr = hudPresentation_->Render(snapshot, options);
    if (FAILED(hr))
    {
        if (!hudRenderFailureLogged_)
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error, L"HUD render failed");
        hudRenderFailureLogged_ = true;
    }
    else
        hudRenderFailureLogged_ = false;
}

void App::SampleProductionTelemetry()
{
    if (!mockHudEnabled_ || !hudPresentation_ || !hudPresentation_->Visible() ||
        diagnosticHudMode_.has_value())
        return;
    if (graphicsApiProcessId_ && !ProcessAlive(graphicsApiProcessId_))
        StopGraphicsApiProbe();
    ecHudTelemetry_ = ReadHudEcTelemetry();
    if (!usageSampler_.Initialized())
    {
        if (!usageSampler_.Initialize())
        {
            latestUsageTelemetry_.reset();
            RenderProductionHud();
            return;
        }
    }
    latestUsageTelemetry_ = usageSampler_.Sample();
    RenderProductionHud();
}

void App::SampleProductionBatteryTelemetry()
{
    if (!mockHudEnabled_ || !hudPresentation_ || !hudPresentation_->Visible() ||
        diagnosticHudMode_.has_value())
        return;
    latestPowerTelemetry_ = clawhud::ReadWindowsPowerTelemetry();
    RenderProductionHud();
}

void App::StartProductionEcSampling()
{
    if (diagnosticHudMode_.has_value() || !MockHudVisible())
        return;
    if (!ecHudSamplingActive_)
    {
        ecHudSamplingActive_ = true;
        SampleProductionTelemetry();
        SetTimer(tray_.Window(), kEcHudTimerId, kUsageSamplingIntervalMs, nullptr);
        SampleProductionBatteryTelemetry();
        SetTimer(tray_.Window(), kBatteryHudTimerId, kBatteryHudTimerIntervalMs, nullptr);
    }
    StartProductionPresentMonSampling();
}

void App::StopProductionEcSampling()
{
    KillTimer(tray_.Window(), kEcHudTimerId);
    KillTimer(tray_.Window(), kBatteryHudTimerId);
    StopProductionPresentMonSampling();
    if (ecHudClient_)
    {
        ecHudClient_->Close();
        ecHudClient_.reset();
    }
    ecHudTelemetry_ = {};
    latestPowerTelemetry_.reset();
    latestUsageTelemetry_.reset();
    usageSampler_.Reset();
    ecHudSamplingActive_ = false;
}

void App::StartProductionPresentMonSampling()
{
    if (diagnosticHudMode_.has_value() || !mockHudEnabled_ || !MockHudVisible())
        return;
    const DWORD processId = foregroundTracker_.TrackedProcessId();
    if (!processId || !ProcessAlive(processId))
    {
        StopProductionPresentMonSampling();
        StopGraphicsApiProbe();
        return;
    }
    if (presentMonHudTelemetry_ && presentMonProcessId_ == processId &&
        presentMonHudTelemetry_->Running())
        return;

    StopProductionPresentMonSampling();
    const auto executable = std::filesystem::path(executablePath_).parent_path() /
        L"tools" / L"PresentMon.exe";
    presentMonHudTelemetry_ = std::make_unique<clawhud::PresentMonHudTelemetry>();
    presentMonProcessId_ = processId;
    Log(L"PresentMon start requested pid=" + std::to_wstring(processId));
    const bool started = presentMonHudTelemetry_->Start(executable.wstring(), processId,
        [this, processId](std::optional<double> displayedFps)
        {
            auto* update = new PresentMonHudUpdate{processId, displayedFps};
            if (!PostMessageW(tray_.Window(), kPresentMonHudUpdate,
                reinterpret_cast<WPARAM>(update), 0))
                delete update;
        });
    if (started)
        Log(L"PresentMon started pid=" + std::to_wstring(processId));
    else
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"PresentMon start failed pid=" + std::to_wstring(processId));
        presentMonHudTelemetry_.reset();
        presentMonProcessId_ = 0;
        latestPresentMonDisplayedFps_.reset();
    }
}

void App::StopProductionPresentMonSampling()
{
    if (presentMonHudTelemetry_)
    {
        Log(L"PresentMon stopped");
        presentMonHudTelemetry_->Stop();
        presentMonHudTelemetry_.reset();
    }
    presentMonProcessId_ = 0;
    latestPresentMonDisplayedFps_.reset();
}

void App::StartGraphicsApiProbe(DWORD processId)
{
    StopGraphicsApiProbe();
    graphicsApiProcessId_ = processId;
    TryGraphicsApiProbe();
}

void App::StopGraphicsApiProbe()
{
    KillTimer(tray_.Window(), kGraphicsApiRetryTimerId);
    graphicsApiProbe_.Reset();
    graphicsApiProcessId_ = 0;
    graphicsApiAttempts_ = 0;
    latestGraphicsApi_.reset();
}

void App::TryGraphicsApiProbe()
{
    if (!graphicsApiProcessId_ || !ProcessAlive(graphicsApiProcessId_))
    {
        StopGraphicsApiProbe();
        return;
    }
    ++graphicsApiAttempts_;
    latestGraphicsApi_ = graphicsApiProbe_.Query(graphicsApiProcessId_);
    if (latestGraphicsApi_ || graphicsApiAttempts_ >= kGraphicsApiMaxAttempts)
    {
        KillTimer(tray_.Window(), kGraphicsApiRetryTimerId);
        graphicsApiProbe_.Reset();
        if (!latestGraphicsApi_)
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"IGCL Graphics API unresolved after bounded retries");
        else
            Log(L"Graphics API resolved api=" + *latestGraphicsApi_);
        RenderProductionHud();
        return;
    }
    SetTimer(tray_.Window(), kGraphicsApiRetryTimerId,
        kGraphicsApiRetryIntervalMs, nullptr);
}

void App::HandlePresentMonHudUpdate(DWORD processId,
    std::optional<double> displayedFps)
{
    if (diagnosticHudMode_.has_value() || !presentMonHudTelemetry_ ||
        presentMonProcessId_ != processId || !MockHudVisible())
        return;
    if (!ProcessAlive(processId))
        StopGraphicsApiProbe();
    latestPresentMonDisplayedFps_ = displayedFps;
    RenderProductionHud();
    if (!displayedFps)
    {
        if (!ProcessAlive(processId))
            Log(L"PresentMon target process exited");
        else
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"PresentMon exited unexpectedly");
        StopProductionPresentMonSampling();
    }
}

bool App::MockHudVisible() const noexcept
{
    return hudPresentation_ && hudPresentation_->Visible();
}

void App::TrackMockGameWindow(HWND window)
{
    if (VrrDiagnosticRunning())
    {
        Log(L"Track Mock Game ignored while VRR diagnostic is running");
        return;
    }
    DWORD processId{};
    if (window)
        GetWindowThreadProcessId(window, &processId);
    if (!processId)
        return;
    foregroundTracker_.SetTrackedProcessId(processId);
    usageSampler_.Reset();
    latestUsageTelemetry_.reset();
    StartGraphicsApiProbe(processId);
    if (EnsureMockHud())
    {
        mockHudEnabled_ = true;
        ReconcileHudVisibility();
    }
}

void App::SetHudVisibilityMode(clawhud::HudVisibilityMode mode)
{
    if (VrrDiagnosticRunning())
    {
        Log(L"HUD visibility mode change ignored while VRR diagnostic is running");
        return;
    }
    hudOptions_.visibilityMode = mode;
    manualHudVisibilityOverride_.reset();
    SaveHudSettings();
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
        if (const DWORD processId = foregroundTracker_.TrackedProcessId())
            StartGraphicsApiProbe(processId);
    }
    manualHudVisibilityOverride_ = !MockHudVisible();
    ReconcileHudVisibility();
    if (settings_) settings_->UpdateHudControls();
}

HudVisibilityState App::CaptureHudVisibilityState() const noexcept
{
    return { mockHudEnabled_, manualHudVisibilityOverride_, MockHudVisible() };
}

bool App::ApplyDiagnosticHudVisibility(bool visible)
{
    diagnosticHudMode_.reset();
    StopProductionEcSampling();
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

bool App::ApplyDiagnosticHudMode(DiagnosticHudMode mode)
{
    StopProductionPresentMonSampling();
    StopProductionEcSampling();
    diagnosticHudMode_ = mode;
    manualHudVisibilityOverride_ = mode == DiagnosticHudMode::Off
        ? std::optional<bool>(false)
        : std::optional<bool>(true);
    if (mode != DiagnosticHudMode::Off)
    {
        if (!EnsureMockHud()) return false;
        mockHudEnabled_ = true;
        mockFrameIndex_ = 0;
    }
    ReconcileHudVisibility();
    return MockHudVisible() == (mode != DiagnosticHudMode::Off);
}

bool App::RestoreHudVisibilityState(const HudVisibilityState& state)
{
    diagnosticHudMode_.reset();
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
    if (state.mockHudEnabled)
    {
        if (const DWORD processId = foregroundTracker_.TrackedProcessId();
            processId && ProcessAlive(processId))
            StartGraphicsApiProbe(processId);
    }
    const bool expectedVisible = state.mockHudEnabled &&
        (state.manualOverride.has_value()
            ? *state.manualOverride
            : clawhud::ShouldShowHud(
                hudOptions_.visibilityMode,
                foregroundTracker_.ForegroundIsTrackedProcess()));
    return MockHudVisible() == expectedVisible;
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

bool App::RequestDiagnosticHudVisibilityMatches(bool expected, DWORD timeoutMs)
{
    if (!tray_.Window()) return false;
    auto request = std::make_shared<HudVisibilityRequest>();
    request->complete = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!request->complete) return false;
    request->query = true;
    request->visible = expected;
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

bool App::RequestDiagnosticHudMode(DiagnosticHudMode mode, DWORD timeoutMs)
{
    if (!tray_.Window()) return false;
    auto request = std::make_shared<HudVisibilityRequest>();
    request->complete = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!request->complete) return false;
    request->modeRequest = true;
    request->mode = mode;
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

bool App::RequestDiagnosticHudState(const HudVisibilityState& state, DWORD timeoutMs)
{
    return RequestHudOnUiThread(false, &state, timeoutMs);
}

void App::CancelPendingHudVisibilityRequests()
{
    DiscardPendingHudVisibilityRequests();
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

void App::DiscardPendingPresentMonHudUpdates()
{
    MSG message{};
    while (PeekMessageW(&message, tray_.Window(), kPresentMonHudUpdate,
        kPresentMonHudUpdate, PM_REMOVE))
        delete reinterpret_cast<PresentMonHudUpdate*>(message.wParam);
}

void App::ReconcileHudVisibility()
{
    if (!hudPresentation_)
        return;
    if (graphicsApiProcessId_ && !ProcessAlive(graphicsApiProcessId_))
        StopGraphicsApiProbe();
    const bool resolvedShow = mockHudEnabled_ && (manualHudVisibilityOverride_.has_value()
        ? *manualHudVisibilityOverride_
        : clawhud::ShouldShowHud(hudOptions_.visibilityMode,
            foregroundTracker_.ForegroundIsTrackedProcess()));
    if (resolvedShow)
    {
        const bool wasVisible = hudPresentation_->Visible();
        const HRESULT hr = hudPresentation_->Show();
        if (SUCCEEDED(hr))
        {
            if (!wasVisible) Log(L"HUD shown");
            hudShowFailureLogged_ = false;
            if (!diagnosticHudMode_.has_value())
            {
                KillTimer(tray_.Window(), kMockHudTimerId);
                StartProductionEcSampling();
            }
            else if (DiagnosticHudModeUsesPeriodicUpdates(*diagnosticHudMode_))
                SetTimer(tray_.Window(), kMockHudTimerId, kMockHudTimerIntervalMs, nullptr);
            else
                KillTimer(tray_.Window(), kMockHudTimerId);
        }
        else
        {
            if (!hudShowFailureLogged_)
                clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
                    L"HUD show failed");
            hudShowFailureLogged_ = true;
        }
    }
    else
    {
        const bool wasVisible = hudPresentation_->Visible();
        const HRESULT hr = hudPresentation_->Hide();
        if (SUCCEEDED(hr))
        {
            if (wasVisible) Log(L"HUD hidden");
            hudHideFailureLogged_ = false;
        }
        else if (!hudHideFailureLogged_)
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
                L"HUD hide failed");
        if (FAILED(hr))
            hudHideFailureLogged_ = true;
        KillTimer(tray_.Window(), kMockHudTimerId);
        StopProductionEcSampling();
    }
}

bool App::AcquireSingleInstance()
{
    instanceMutex_ = CreateMutexW(nullptr, TRUE, kInstanceMutexName);
    if (!instanceMutex_)
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"CreateMutex failed; exiting");
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        Log(L"another ClawHUD instance already exists");
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
    wchar_t startup[8]{};
    GetPrivateProfileStringW(L"General", L"StartWithWindows", L"1", startup,
        ARRAYSIZE(startup), path.c_str());
    startWithWindows_ = std::wcstol(startup, nullptr, 10) != 0;
    const auto alignment = ReadHudSetting(path, L"Alignment", L"Center");
    if (alignment == L"Left") hudOptions_.alignment = clawhud::HudAlignment::Left;
    else if (alignment == L"Right") hudOptions_.alignment = clawhud::HudAlignment::Right;
    const auto background = ReadHudSetting(path, L"BackgroundWidth", L"FullWidth");
    if (background == L"ContentWidth") hudOptions_.backgroundMode = clawhud::HudBackgroundMode::ContentWidth;
    else if (background == L"FullWidth") hudOptions_.backgroundMode = clawhud::HudBackgroundMode::FullWidth;
    const auto visibility = ReadHudSetting(path, L"VisibilityMode", L"InGameOnly");
    if (visibility == L"Always") hudOptions_.visibilityMode = clawhud::HudVisibilityMode::Always;
    else if (visibility == L"InGameOnly") hudOptions_.visibilityMode = clawhud::HudVisibilityMode::InGameOnly;
    hudSizeOffset_ = clawhud::ParseHudSizeOffset(ReadHudSetting(path, L"Size", L"0"));
    const auto opacityText = ReadHudSetting(path, L"BackgroundOpacity", L"50");
    wchar_t* end{};
    const long parsed = std::wcstol(opacityText.c_str(), &end, 10);
    const bool valid = end != opacityText.c_str() && end && *end == L'\0';
    const long percent = std::clamp(valid ? parsed : 50L, 0L, 100L);
    hudOptions_.backgroundOpacity = static_cast<float>(percent) / 100.0f;
    intelVrrRangeFixEnabled_ = ReadBoolSetting(path, L"Tweaks", L"IntelVrrRangeFixEnabled", true);
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
    const wchar_t* visibility = hudOptions_.visibilityMode == clawhud::HudVisibilityMode::Always
        ? L"Always" : L"InGameOnly";
    wchar_t opacity[8]{};
    swprintf_s(opacity, L"%d", static_cast<int>(hudOptions_.backgroundOpacity * 100.0f + 0.5f));
    bool saved = WritePrivateProfileStringW(L"HUD", L"Alignment", alignment, path.c_str()) != FALSE;
    saved = WritePrivateProfileStringW(L"HUD", L"BackgroundWidth", background, path.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"HUD", L"BackgroundOpacity", opacity, path.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"HUD", L"VisibilityMode", visibility, path.c_str()) != FALSE && saved;
    wchar_t size[8]{};
    swprintf_s(size, L"%d", clawhud::ClampHudSizeOffset(hudSizeOffset_));
    saved = WritePrivateProfileStringW(L"HUD", L"Size", size, path.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"General", L"StartWithWindows",
        startWithWindows_ ? L"1" : L"0", path.c_str()) != FALSE && saved;
    if (!saved)
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error, L"Settings save failed");
}

bool App::ApplyStartupRegistration() const
{
    const auto shortcut = clawhud::StartupShortcutPath();
    if (shortcut.empty()) return false;

    if (!startWithWindows_)
        return DeleteFileW(shortcut.c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND;

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return false;

    IShellLinkW* shellLink{};
    IPersistFile* persistFile{};
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&shellLink));
    if (SUCCEEDED(result))
    {
        result = shellLink->SetPath(executablePath_.c_str());
        if (SUCCEEDED(result))
        {
            const auto workingDirectory = std::filesystem::path(executablePath_).parent_path();
            result = shellLink->SetWorkingDirectory(workingDirectory.c_str());
        }
        if (SUCCEEDED(result)) result = shellLink->QueryInterface(IID_PPV_ARGS(&persistFile));
        if (SUCCEEDED(result)) result = persistFile->Save(shortcut.c_str(), TRUE);
    }
    if (persistFile) persistFile->Release();
    if (shellLink) shellLink->Release();
    CoUninitialize();
    return SUCCEEDED(result);
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
    if (VrrDiagnosticRunning())
    {
        Log(L"Open Settings ignored while VRR diagnostic is running");
        return;
    }
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
    StopProductionEcSampling();
    StopGraphicsApiProbe();
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
                {
                    request->result = request->modeRequest
                        ? ApplyDiagnosticHudMode(request->mode)
                        : request->query
                            ? MockHudVisible() == request->visible
                            : request->restore
                                ? RestoreHudVisibilityState(request->state)
                                : ApplyDiagnosticHudVisibility(request->visible);
                }
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
        if (message.message == kPresentMonHudUpdate)
        {
            auto* update = reinterpret_cast<PresentMonHudUpdate*>(message.wParam);
            if (update)
            {
                HandlePresentMonHudUpdate(update->processId, update->displayedFps);
                delete update;
            }
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
