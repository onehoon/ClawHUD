#include "App.h"

#include "SettingsWindow.h"
#include "HudPresentation.h"
#include "Tweaks/IntelVrr/IntelVrrResultStore.h"
#include "SupportedHardware.h"
#include "HudSize.h"
#include "UninstallCleanup.h"
#include "RuntimeLogger.h"
#include "Version.h"
#include "ProductionTargetPolicy.h"

#include <Velopack.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <cwctype>
#include <sstream>
#include <string>

namespace
{
constexpr unsigned kIgclUnavailableFailureThreshold = 3;
constexpr UINT kSettingsDestroyed = WM_APP + 1;
constexpr UINT kForegroundChanged = WM_APP + 2;
constexpr UINT kHudVisibilityRequest = WM_APP + 3;
constexpr UINT kPresentMonHudUpdate = WM_APP + 4;
constexpr UINT kSteamRunningAppIdChanged = WM_APP + 5;
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
    bool steamResolution{};
    clawhud::PresentMonHudSample sample;
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
    clawhud::RuntimeLogger::SetDebugLogging(debugLoggingEnabled_);
    Log(L"ClawHUD started version=" CLAWHUD_VERSION L" pid=" +
        std::to_wstring(GetCurrentProcessId()));
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
        L"Runtime settings HUDEnabled=" + std::to_wstring(mockHudEnabled_ ? 1 : 0) +
        L" HUDSizeOffset=" + std::to_wstring(hudSizeOffset_) +
        L" StartWithWindows=" + std::to_wstring(startWithWindows_ ? 1 : 0));
}

App::~App()
{
    Log(L"ClawHUD exiting");
    KillTimer(tray_.Window(), kMockHudTimerId);
    CancelResumeRecovery();
    StopProductionEcSampling(false, L"app-shutdown");
    StopGraphicsApiProbe();
    foregroundTracker_.Stop();
    if (vrrDiagnostic_) vrrDiagnostic_->Stop();
    DiscardPendingHudVisibilityRequests();
    StopProductionPresentMonSampling(L"app-shutdown", true);
    steamRunningAppIdSource_.Stop();
    KillTimer(tray_.Window(), kSteamRendererResolveTimerId);
    steamGpuEngineSampler_.Reset();
    steamBaselineProcessIds_.clear();
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
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"RegisterHotKey(F8) failed; continuing without the global HUD toggle");
    ecDiagnostic_ = std::make_unique<EcDiagnostic>(tray_.Window());
    igclDiagnostic_ = std::make_unique<clawhud::IgclTelemetryDiagnostic>(tray_.Window());
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
        },
        [this](HWND window, DWORD processId)
        {
            if (mockHudEnabled_ && !DiagnosticRunning() &&
                steamRunningAppId_ == 0)
                AdoptForegroundProductionTarget(window, processId);
            else if (mockHudEnabled_ && !DiagnosticRunning() &&
                steamGameState_ == SteamGameState::Resolving)
                ResolveSteamRenderer();
        }))
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"Foreground tracker initialization failed");
        return 1;
    }
    if (!steamRunningAppIdSource_.Start(tray_.Window(), kSteamRunningAppIdChanged))
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"Steam RunningAppID watcher failed to start");
    else
    {
        const auto initialAppId = steamRunningAppIdSource_.ReadCurrentAppId();
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info,
            L"Steam RunningAppID watcher started appId=" +
            std::to_wstring(initialAppId));
        HandleSteamRunningAppId(initialAppId);
    }
    if (ShouldRestorePersistedHud(mockHudEnabled_))
    {
        if (!EnsureMockHud())
        {
            mockHudEnabled_ = false;
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"Persisted HUD enable restore failed during initialization");
        }
        else
        {
            ReconcileProductionTargetAuthority();
            ReconcileHudVisibility();
        }
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

void App::SetDebugLoggingEnabled(bool enabled)
{
    if (debugLoggingEnabled_ == enabled)
        return;
    debugLoggingEnabled_ = enabled;
    clawhud::RuntimeLogger::SetDebugLogging(enabled);
    SaveHudSettings();
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info,
        enabled ? L"Debug logging enabled" : L"Debug logging disabled");
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
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            enabled ? L"Startup registration failed" : L"Startup shortcut removal failed");
        return;
    }
    SaveHudSettings();
}

bool App::StartEcDiagnostic()
{
    if (!ecDiagnostic_ || DiagnosticRunning() || ecHudSamplingActive_)
        return false;

    igclStatus_ = L"Idle";
    pendingProductionTargetPid_ = 0;
    StopProductionPresentMonSampling(L"diagnostic-start", false);
    StopGraphicsApiProbe();
    if (!ecDiagnostic_->Start())
    {
        if (mockHudEnabled_ && !suspended_)
            AdoptForegroundProductionTarget();
        ReconcileHudVisibility();
        return false;
    }
    ecStatus_ = L"Running";
    return true;
}
bool App::StartIgclDiagnostic()
{
    if (!igclDiagnostic_ || DiagnosticRunning())
        return false;
    ecStatus_ = L"Idle";
    pendingProductionTargetPid_ = 0;
    StopProductionEcSampling(false, L"igcl-diagnostic-start");
    StopProductionPresentMonSampling(L"igcl-diagnostic-start", false);
    StopGraphicsApiProbe();
    if (!igclDiagnostic_->Start())
    {
        if (mockHudEnabled_ && !suspended_) AdoptForegroundProductionTarget();
        ReconcileHudVisibility();
        return false;
    }
    igclStatus_ = L"Waiting 5 seconds...";
    if (settings_) settings_->RequestClose();
    return true;
}
void App::StopIgclDiagnostic()
{
    const bool wasRunning = IgclDiagnosticRunning();
    if (igclDiagnostic_) igclDiagnostic_->Stop();
    if (wasRunning && clawhud::ShouldReevaluateForegroundAfterDiagnostic(
        mockHudEnabled_, DiagnosticRunning(), suspended_))
        AdoptForegroundProductionTarget();
    ReconcileHudVisibility();
}
bool App::IgclDiagnosticRunning() const { return igclDiagnostic_ && igclDiagnostic_->Running(); }
void App::FinishIgclDiagnostic(bool success)
{
    if (igclDiagnostic_) igclDiagnostic_->Stop();
    if (clawhud::ShouldReevaluateForegroundAfterDiagnostic(
        mockHudEnabled_, DiagnosticRunning(), suspended_))
        AdoptForegroundProductionTarget();
    ReconcileHudVisibility();
    if (!success)
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"IGCL diagnostic completed without success; production telemetry restored");
}
void App::StopEcDiagnostic()
{
    if (ecDiagnostic_)
        ecDiagnostic_->Stop();
    if (clawhud::ShouldReevaluateForegroundAfterDiagnostic(
        mockHudEnabled_, DiagnosticRunning(), suspended_))
        AdoptForegroundProductionTarget();
    ReconcileHudVisibility();
}
bool App::EcDiagnosticRunning() const { return ecDiagnostic_ && ecDiagnostic_->Running(); }
void App::OpenDiagnosticLogFolder()
{
    try
    {
        const auto path = clawhud::LogDirectory();
        if (!path.empty())
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    catch (...) {}
}
bool App::StartVrrDiagnostic()
{
    if (!vrrDiagnostic_ || DiagnosticRunning()) return false;
    if (!VrrDiagnosticCanWaitForF8(hudHotkeyRegistered_))
    {
        vrrStatus_ = L"F8 unavailable";
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"VRR diagnostic start failed: global F8 hotkey is not registered");
        return false;
    }
    StopProductionEcSampling(false, L"diagnostic-start");
    StopProductionPresentMonSampling(L"diagnostic-start", false);
    StopGraphicsApiProbe();
    pendingProductionTargetPid_ = 0;
    if (!vrrDiagnostic_->Start())
    {
        if (clawhud::ShouldReevaluateForegroundAfterDiagnostic(
            mockHudEnabled_, DiagnosticRunning(), suspended_))
            AdoptForegroundProductionTarget();
        ReconcileHudVisibility();
        if (const DWORD processId = foregroundTracker_.TrackedProcessId();
            processId && ProcessAlive(processId) && mockHudEnabled_)
            StartGraphicsApiProbe(processId);
        return false;
    }
    vrrStatus_ = L"Waiting for F8";
    if (settings_) settings_->RequestClose();
    return true;
}
void App::StopVrrDiagnostic()
{
    if (vrrDiagnostic_)
        vrrDiagnostic_->Stop();
    if (clawhud::ShouldReevaluateForegroundAfterDiagnostic(
        mockHudEnabled_, DiagnosticRunning(), suspended_))
        AdoptForegroundProductionTarget();
    ReconcileHudVisibility();
}
bool App::VrrDiagnosticRunning() const { return vrrDiagnostic_ && vrrDiagnostic_->Running(); }
bool App::DiagnosticRunning() const { return EcDiagnosticRunning() || VrrDiagnosticRunning() || IgclDiagnosticRunning(); }
void App::StopDiagnostic() { StopVrrDiagnostic(); StopEcDiagnostic(); StopIgclDiagnostic(); }

void App::HandleSystemSuspend()
{
    if (DiagnosticRunning() || suspended_)
        return;
    suspended_ = true;
    CancelResumeRecovery();
    KillTimer(tray_.Window(), kMockHudTimerId);
    if (hudPresentation_ && hudPresentation_->Visible())
    {
        if (SUCCEEDED(hudPresentation_->Hide()))
            Log(L"HUD suspended");
        else
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"HUD suspend hide failed");
    }
    PauseProductionSamplingForSuspend();
    DiscardPendingPresentMonHudUpdates();
    Log(L"System suspend detected");
}

void App::HandleSystemResume()
{
    if (!ResumeRecoveryShouldStart(resumeRecoveryActive_) || DiagnosticRunning())
        return;
    if (ResumeRecoveryNeedsSuspendFallback(suspended_))
    {
        KillTimer(tray_.Window(), kMockHudTimerId);
        if (hudPresentation_ && hudPresentation_->Visible())
            hudPresentation_->Hide();
        PauseProductionSamplingForSuspend();
        DiscardPendingPresentMonHudUpdates();
        Log(L"Suspend notification was missed; resume fallback prepared");
    }
    suspended_ = false;
    if (steamRunningAppIdSource_.Running())
        HandleSteamRunningAppId(steamRunningAppIdSource_.ReadCurrentAppId());
    resumeRecoveryActive_ = true;
    resumeRecoveryAttempts_ = 0;
    SetTimer(tray_.Window(), kResumeRecoveryTimerId,
        kResumeRecoveryIntervalMs, nullptr);
    Log(L"System resume detected");
    Log(L"HUD resume recovery started");
}

void App::TryResumeRecovery()
{
    if (!resumeRecoveryActive_)
        return;
    if (DiagnosticRunning())
    {
        CancelResumeRecovery();
        return;
    }

    ++resumeRecoveryAttempts_;
    foregroundTracker_.Reconcile();
    const DWORD processId = foregroundTracker_.TrackedProcessId();
    const bool processAlive = processId && ProcessAlive(processId);
    const bool retainPresentMon = ResumeRecoveryCanRetainPresentMon(
        processId, presentMonProcessId_,
        presentMonHudTelemetry_ && presentMonHudTelemetry_->Running());
    const bool expectedVisible = mockHudEnabled_ &&
        (manualHudVisibilityOverride_.has_value()
            ? *manualHudVisibilityOverride_
            : clawhud::ShouldShowHud(
                hudOptions_.visibilityMode,
                foregroundTracker_.ForegroundIsTrackedProcess()));
    const bool visibilityUsesForeground = !manualHudVisibilityOverride_.has_value() &&
        hudOptions_.visibilityMode == clawhud::HudVisibilityMode::InGameOnly;
    DiscardPendingPresentMonHudUpdates();
    if (ResumeRecoveryShouldWaitForForeground(
        mockHudEnabled_, visibilityUsesForeground, processAlive,
        foregroundTracker_.ForegroundIsTrackedProcess(), resumeRecoveryAttempts_))
    {
        SetTimer(tray_.Window(), kResumeRecoveryTimerId,
            kResumeRecoveryIntervalMs, nullptr);
        return;
    }

    if (processAlive)
        StartGraphicsApiProbe(processId);
    else
        StopGraphicsApiProbe();

    bool freshFrameReady = !expectedVisible || hudPresentation_ != nullptr;
    if (expectedVisible && hudPresentation_)
    {
        const HRESULT clearHr = hudPresentation_->Render(
            clawhud::HudTelemetrySnapshot{}, BuildHudRenderOptions());
        freshFrameReady = ResumeRecoveryFrameWasPresented(clearHr);
        if (!freshFrameReady && clearHr != S_FALSE && resumeRecoveryAttempts_ == 1)
            freshFrameReady = RecreateHudPresentation(false);
    }
    if (!ResumeRecoveryMayShowHud(expectedVisible, freshFrameReady))
    {
        resumeRecoveryActive_ = true;
        if (!ResumeRecoveryHasAttemptsRemaining(resumeRecoveryAttempts_))
        {
            CancelResumeRecovery();
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"HUD resume recovery exhausted");
            return;
        }
        SetTimer(tray_.Window(), kResumeRecoveryTimerId,
            kResumeRecoveryIntervalMs, nullptr);
        return;
    }

    resumeRecoveryActive_ = false;
    ReconcileHudVisibility();
    if (expectedVisible && !MockHudVisible() && resumeRecoveryAttempts_ == 1)
    {
        RecreateHudPresentation(true);
        ReconcileHudVisibility();
    }

    const bool recovered = !expectedVisible || MockHudVisible();
    if (recovered)
    {
        if (retainPresentMon && presentMonHudTelemetry_ &&
            presentMonProcessId_ == processId && presentMonHudTelemetry_->Running())
            Log(L"PresentMon retained after resume pid=" + std::to_wstring(processId));
        else if (processAlive && presentMonHudTelemetry_ &&
            presentMonProcessId_ == processId && presentMonHudTelemetry_->Running())
            Log(L"PresentMon restarted after resume pid=" + std::to_wstring(processId));
        const unsigned completedAttempt = resumeRecoveryAttempts_;
        CancelResumeRecovery();
        if (clawhud::ShouldReevaluateForegroundAfterResume(
            mockHudEnabled_, recovered))
            AdoptForegroundProductionTarget();
        Log(L"HUD resume recovery completed attempt=" +
            std::to_wstring(completedAttempt));
        return;
    }

    resumeRecoveryActive_ = true;
    if (!ResumeRecoveryHasAttemptsRemaining(resumeRecoveryAttempts_))
    {
        CancelResumeRecovery();
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"HUD resume recovery exhausted");
        return;
    }
    SetTimer(tray_.Window(), kResumeRecoveryTimerId,
        kResumeRecoveryIntervalMs, nullptr);
}

bool App::EnsureMockHud()
{
    if (!hudPresentation_)
        hudPresentation_ = std::make_unique<clawhud::HudPresentation>();
    const auto options = BuildHudRenderOptions();
    HRESULT hr = hudPresentation_->Initialize(instance_, options,
        hudOptions_.backgroundOpacity * 100.0f);
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
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"Hide Mock HUD ignored while VRR diagnostic is running");
        return;
    }
    if (mockHudEnabled_) Log(L"HUD disabled");
    mockHudEnabled_ = false;
    manualHudVisibilityOverride_.reset();
    KillTimer(tray_.Window(), kMockHudTimerId);
    StopProductionEcSampling(true, L"hud-disabled");
    StopGraphicsApiProbe();
    pendingProductionTargetPid_ = 0;
    ReconcileHudVisibility();
    if (hudPresentation_)
    {
        hudPresentation_->Shutdown();
        hudPresentation_.reset();
    }
}

bool App::SetHudEnabled(bool enabled)
{
    if (VrrDiagnosticRunning())
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"HUD enable change ignored while VRR diagnostic is running");
        return false;
    }
    if (!enabled)
    {
        StopMockHud();
        SaveHudEnabledSetting(false);
        return true;
    }
    if (!EnsureMockHud()) return false;
    if (!mockHudEnabled_) Log(L"HUD enabled");
    mockHudEnabled_ = true;
    mockFrameIndex_ = 0;
    AdoptForegroundProductionTarget();
    manualHudVisibilityOverride_.reset();
    ReconcileHudVisibility();
    SaveHudEnabledSetting(true);
    return true;
}

void App::SetHudAlignment(clawhud::HudAlignment alignment)
{
    if (VrrDiagnosticRunning())
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"HUD alignment change ignored while VRR diagnostic is running");
        return;
    }
    if (hudOptions_.alignment == alignment)
        return;
    const auto previousAlignment = hudOptions_.alignment;
    hudOptions_.alignment = alignment;
    if (hudOptions_.backgroundMode == clawhud::HudBackgroundMode::ContentWidth)
    {
        const bool restoreVisible = hudPresentation_ &&
            hudPresentation_->Initialized() && hudPresentation_->Visible();
        const bool recreated = RecreateHudPresentation(restoreVisible);
        hudOptions_.alignment = clawhud::CommitHudAlignmentAfterRecreation(
            previousAlignment, alignment, recreated);
        if (!recreated)
        {
            RecreateHudPresentation(restoreVisible);
            SaveHudSettings();
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"HUD alignment change rolled back after presentation recreation failure");
            return;
        }
    }
    else
    {
        RefreshMockHud();
    }
    SaveHudSettings();
}

void App::SetHudFont(clawhud::HudFont font)
{
    if (hudFont_ == font)
        return;
    const auto previousFont = hudFont_;
    const bool restoreVisible = hudPresentation_ &&
        hudPresentation_->Initialized() && hudPresentation_->Visible();
    hudFont_ = font;
    if (!RecreateHudPresentation(restoreVisible))
    {
        hudFont_ = previousFont;
        RecreateHudPresentation(restoreVisible);
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"HUD font change rolled back after presentation recreation failure");
        return;
    }
    SaveHudSettings();
    if (settings_)
        settings_->UpdateHudControls();
}

void App::SetHudBackgroundMode(clawhud::HudBackgroundMode mode)
{
    if (VrrDiagnosticRunning())
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"HUD background mode change ignored while VRR diagnostic is running");
        return;
    }
    if (hudOptions_.backgroundMode == mode)
        return;
    const auto previousMode = hudOptions_.backgroundMode;
    hudOptions_.backgroundMode = mode;
    const bool restoreVisible = hudPresentation_ &&
        hudPresentation_->Initialized() && hudPresentation_->Visible();
    const bool recreated = RecreateHudPresentation(restoreVisible);
    hudOptions_.backgroundMode = clawhud::CommitHudBackgroundModeAfterRecreation(
        previousMode, mode, recreated);
    if (!recreated)
    {
        RecreateHudPresentation(restoreVisible);
        SaveHudSettings();
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"HUD background mode change rolled back after presentation recreation failure");
        return;
    }
    SaveHudSettings();
}

bool App::SetHudOpacity(float opacity, bool persist)
{
    if (VrrDiagnosticRunning())
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"HUD opacity change ignored while VRR diagnostic is running");
        return false;
    }
    const long requestedPercent = static_cast<long>(std::lround(opacity * 100.0f));
    const long percent = clawhud::ClampHudOpacityPercent(requestedPercent);
    const float newOpacity = static_cast<float>(percent) / 100.0f;
    if (hudOptions_.backgroundOpacity == newOpacity)
    {
        if (persist) SaveHudSettings();
        return true;
    }
    if (hudPresentation_ && hudPresentation_->Initialized())
    {
        const HRESULT hr = hudPresentation_->SetHudOpacity(static_cast<float>(percent));
        if (FAILED(hr))
        {
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
                L"SetLayeredWindowAttributes for HUD opacity failed hr=" + HexHresult(hr));
            return false;
        }
    }
    hudOptions_.backgroundOpacity = newOpacity;
    if (persist) SaveHudSettings();
    return true;
}

void App::SetHudSizeOffset(int offset)
{
    if (VrrDiagnosticRunning())
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"HUD size change ignored while VRR diagnostic is running");
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
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"HUD size change rolled back after presentation recreation failure");
        return;
    }
    Log(L"HUD presentation recreated");
    SaveHudSettings();
}

clawhud::HudRenderOptions App::BuildHudRenderOptions() const
{
    auto options = clawhud::BuildHudRenderOptionsForSize(
        hudSizeOffset_, hudOptions_, hudFont_);
    // The renderer remains opaque; the existing layered HWND owns HUD opacity.
    options.layout.backgroundOpacity = 1.0f;
    return options;
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
    HRESULT hr = hudPresentation_->Initialize(instance_, options,
        hudOptions_.backgroundOpacity * 100.0f);
    if (FAILED(hr))
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"HUD presentation recreation failed hr=" + HexHresult(hr));
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
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"HUD visibility restore failed hr=" + HexHresult(hr));
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
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
                L"HUD render failed hr=" + HexHresult(hr));
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
        snapshot.systemMemoryUsedBytes = latestUsageTelemetry_->systemMemoryUsedBytes;
        snapshot.gpuMemoryUsedBytes = latestUsageTelemetry_->intelGpuMemoryUsedBytes;
    }
    if (latestIgclGpuTelemetry_)
    {
        snapshot.gpuUsagePercent = latestIgclGpuTelemetry_->gpuUsagePercent;
        snapshot.gpuClockMHz = latestIgclGpuTelemetry_->gpuClockMHz;
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
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
                L"HUD render failed hr=" + HexHresult(hr));
        hudRenderFailureLogged_ = true;
    }
    else
        hudRenderFailureLogged_ = false;
}

void App::SampleProductionTelemetry()
{
    if (suspended_ || !mockHudEnabled_ || !hudPresentation_ || !hudPresentation_->Visible() ||
        diagnosticHudMode_.has_value())
        return;
    if (graphicsApiProcessId_ && !ProcessAlive(graphicsApiProcessId_))
        StopGraphicsApiProbe();
    ecHudTelemetry_ = ReadHudEcTelemetry();
    if (!usageSampler_.Initialized())
    {
        if (!usageSampler_.Initialize())
            latestUsageTelemetry_.reset();
    }
    if (usageSampler_.Initialized())
        latestUsageTelemetry_ = usageSampler_.Sample();

    if (!igclGpuSampler_.Initialized() && !igclGpuSampler_.InitializationAttempted())
    {
        if (!igclGpuSampler_.Initialize())
            latestIgclGpuTelemetry_.reset();
        else
            igclTelemetryAvailable_ = true;
    }
    if (igclGpuSampler_.Initialized())
    {
        latestIgclGpuTelemetry_ = igclGpuSampler_.Sample();
        if (latestIgclGpuTelemetry_)
        {
            igclTelemetryFailureCount_ = 0;
        }
        else
        {
            ++igclTelemetryFailureCount_;
        }
        const auto transition = clawhud::ObserveIgclTelemetryTransition(
            igclTelemetryAvailable_, igclTelemetryFailureCount_,
            latestIgclGpuTelemetry_.has_value(), kIgclUnavailableFailureThreshold);
        if (transition == clawhud::IgclTelemetryTransition::Recovered)
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info,
                L"IGCL telemetry recovered");
        else if (transition == clawhud::IgclTelemetryTransition::Unavailable)
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"IGCL telemetry unavailable");
        if (latestIgclGpuTelemetry_)
            igclTelemetryAvailable_ = true;
        else if (igclTelemetryFailureCount_ >= kIgclUnavailableFailureThreshold)
            igclTelemetryAvailable_ = false;
    }
    RenderProductionHud();
}

void App::SampleProductionBatteryTelemetry()
{
    if (suspended_ || !mockHudEnabled_ || !hudPresentation_ || !hudPresentation_->Visible() ||
        diagnosticHudMode_.has_value())
        return;
    latestPowerTelemetry_ = clawhud::ReadWindowsPowerTelemetry();
    RenderProductionHud();
}

void App::StartProductionEcSampling()
{
    if (suspended_ || diagnosticHudMode_.has_value() || !MockHudVisible())
        return;
    if (steamRunningAppId_ && steamGameState_ == SteamGameState::Resolving)
        StartSteamRendererResolution();
    if (!ecHudSamplingActive_)
    {
        ecHudSamplingActive_ = true;
        Log(L"Production telemetry sampling started");
        SampleProductionTelemetry();
        SetTimer(tray_.Window(), kEcHudTimerId, kUsageSamplingIntervalMs, nullptr);
        SampleProductionBatteryTelemetry();
        SetTimer(tray_.Window(), kBatteryHudTimerId, kBatteryHudTimerIntervalMs, nullptr);
    }
    StartProductionPresentMonSampling();
}

void App::PauseProductionSamplingForSuspend()
{
    const bool wasActive = ecHudSamplingActive_;
    KillTimer(tray_.Window(), kEcHudTimerId);
    KillTimer(tray_.Window(), kBatteryHudTimerId);
    StopProductionPresentMonSampling(L"suspend", false);
    if (steamRunningAppId_)
    {
        StopSteamRendererResolution(true);
        steamRendererPid_ = 0;
        steamGameState_ = SteamGameState::Resolving;
    }
    StopGraphicsApiProbe();
    if (ecHudClient_)
    {
        ecHudClient_->Close();
        ecHudClient_.reset();
    }
    ecHudTelemetry_ = {};
    latestPowerTelemetry_.reset();
    latestUsageTelemetry_.reset();
    latestPresentMonDisplayedFps_.reset();
    usageSampler_.Reset();
    latestIgclGpuTelemetry_.reset();
    igclGpuSampler_.Reset();
    igclTelemetryAvailable_ = false;
    igclTelemetryFailureCount_ = 0;
    ecHudSamplingActive_ = false;
    if (wasActive)
        Log(L"Production telemetry sampling stopped reason=suspend");
}

void App::ReleaseCommittedProductionTarget(const wchar_t* reason)
{
    const DWORD processId = foregroundTracker_.TrackedProcessId();
    if (!processId)
        return;
    const auto release = clawhud::PlanCommittedTargetRelease();
    pendingProductionTargetPid_ = 0;
    presentMonRestartPid_ = 0;
    presentMonRestartAttempts_ = 0;
    clawhud::CommittedTargetReleaseOps ops;
    ops.stopPresentMon = [this, reason]
    {
        StopProductionPresentMonSampling(reason, true);
    };
    ops.stopGraphicsApiProbe = [this]
    {
        StopGraphicsApiProbe();
    };
    ops.clearTrackedProcess = [this]
    {
        foregroundTracker_.SetTrackedProcessId(0);
    };
    ops.startGlobalTelemetry = [this]
    {
        StartProductionEcSampling();
    };
    ops.stopGlobalTelemetry = [this, reason]
    {
        StopProductionEcSampling(false, reason);
    };
    ops.reconcileHudVisibility = [this]
    {
        ReconcileHudVisibility();
    };
    clawhud::ApplyCommittedTargetReleasePlan(release, ops);
    Log(L"Production target cleared pid=" + std::to_wstring(processId) +
        L" reason=" + reason);
}

void App::CancelResumeRecovery()
{
    KillTimer(tray_.Window(), kResumeRecoveryTimerId);
    resumeRecoveryActive_ = false;
    resumeRecoveryAttempts_ = 0;
}

void App::StopProductionEcSampling(bool stopPresentMon, const wchar_t* reason)
{
    const bool wasActive = ecHudSamplingActive_;
    KillTimer(tray_.Window(), kEcHudTimerId);
    KillTimer(tray_.Window(), kBatteryHudTimerId);
    if (stopPresentMon)
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
    latestIgclGpuTelemetry_.reset();
    igclGpuSampler_.Reset();
    igclTelemetryAvailable_ = false;
    igclTelemetryFailureCount_ = 0;
    ecHudSamplingActive_ = false;
    if (wasActive)
        Log(L"Production telemetry sampling stopped reason=" + std::wstring(reason));
}

void App::StartProductionPresentMonSampling(bool recoveryStart)
{
    if (suspended_ || DiagnosticRunning() || !mockHudEnabled_)
        return;
    if (steamRunningAppId_)
        return;
    const DWORD committedProcessId = foregroundTracker_.TrackedProcessId();
    const bool committedAlive = committedProcessId && ProcessAlive(committedProcessId);
    if (committedProcessId && !committedAlive)
    {
        ReleaseCommittedProductionTarget(L"game-exited");
        return;
    }
    if (!clawhud::ShouldSampleProductionPresentMon(
        committedProcessId, pendingProductionTargetPid_, committedAlive))
    {
        return;
    }
    const DWORD processId = clawhud::SelectProductionSamplingProcess(
        committedProcessId, pendingProductionTargetPid_);
    if (!processId || !ProcessAlive(processId))
    {
        if (pendingProductionTargetPid_ == processId)
            pendingProductionTargetPid_ = 0;
        if (!committedProcessId)
            StopProductionPresentMonSampling(L"explicit-reset", true);
        return;
    }
    if (presentMonHudTelemetry_ && presentMonProcessId_ == processId &&
        presentMonHudTelemetry_->Running())
        return;
    if (committedProcessId == processId && !presentMonHudTelemetry_ &&
        !clawhud::ShouldAllowProductionPresentMonStart(
            committedProcessId, processId, presentMonRestartPid_,
            presentMonRestartAttempts_, recoveryStart))
        return;

    StopProductionPresentMonSampling(L"target-handoff", false);
    const auto executable = std::filesystem::path(executablePath_).parent_path() /
        L"tools" / L"PresentMon.exe";
    presentMonHudTelemetry_ = std::make_unique<clawhud::PresentMonHudTelemetry>();
    presentMonProcessId_ = processId;
    Log(L"PresentMon start requested pid=" + std::to_wstring(processId));
    const bool started = presentMonHudTelemetry_->Start(executable.wstring(), processId,
        [this, processId](clawhud::PresentMonHudSample sample)
        {
            auto* update = new PresentMonHudUpdate{processId, false, sample};
            if (!PostMessageW(tray_.Window(), kPresentMonHudUpdate,
                reinterpret_cast<WPARAM>(update), 0))
                delete update;
        });
    if (started)
        Log(L"PresentMon started pid=" + std::to_wstring(processId) +
            L" session=" + presentMonHudTelemetry_->SessionName());
    else
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"PresentMon start failed pid=" + std::to_wstring(processId));
        presentMonHudTelemetry_.reset();
        presentMonProcessId_ = 0;
        if (!committedProcessId)
            latestPresentMonDisplayedFps_.reset();
        else
        {
            if (presentMonRestartPid_ != committedProcessId)
            {
                presentMonRestartPid_ = committedProcessId;
                presentMonRestartAttempts_ = 0;
            }
            else if (presentMonRestartAttempts_ < 1)
                ++presentMonRestartAttempts_;
            if (presentMonRestartAttempts_ >= 1)
                clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                    L"PresentMon recovery exhausted pid=" +
                    std::to_wstring(committedProcessId));
        }
    }
}

void App::StopProductionPresentMonSampling(const wchar_t* reason, bool clearLatestFps)
{
    if (presentMonHudTelemetry_)
    {
        const DWORD processId = presentMonProcessId_;
        Log(L"PresentMon stop requested pid=" +
            std::to_wstring(processId) + L" reason=" + reason);
        const DWORD exitCode = presentMonHudTelemetry_->Stop();
        Log(L"PresentMon stopped pid=" +
            std::to_wstring(processId) + L" exitCode=" +
            std::to_wstring(exitCode));
        presentMonHudTelemetry_.reset();
    }
    presentMonProcessId_ = 0;
    if (clearLatestFps)
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

void App::StopSteamRendererResolution(bool clearFps)
{
    KillTimer(tray_.Window(), kSteamRendererResolveTimerId);
    steamGpuEngineSampler_.Reset();
    steamBaselineProcessIds_.clear();
    steamRendererCandidatePid_ = 0;
    if (presentMonHudTelemetry_)
        StopProductionPresentMonSampling(L"steam-resolution-reset", clearFps);
    else if (clearFps)
        latestPresentMonDisplayedFps_.reset();
    StopGraphicsApiProbe();
}

void App::StartSteamRendererResolution()
{
    if (!steamRunningAppId_ || suspended_ || DiagnosticRunning() || !mockHudEnabled_)
        return;
    StopSteamRendererResolution(true);
    steamGameState_ = SteamGameState::Resolving;
    steamRendererPid_ = 0;
    foregroundTracker_.SetTrackedProcessId(0);
    if (!steamGpuEngineSampler_.Initialize())
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"Steam GPU Engine resolver failed to initialize");
    steamGpuEngineSampler_.Sample();
    for (const auto& activity : steamGpuEngineSampler_.Sample())
    {
        if (std::find(steamBaselineProcessIds_.begin(),
            steamBaselineProcessIds_.end(), activity.processId) ==
            steamBaselineProcessIds_.end())
            steamBaselineProcessIds_.push_back(activity.processId);
    }
    ResolveSteamRenderer();
    SetTimer(tray_.Window(), kSteamRendererResolveTimerId, 500, nullptr);
}

void App::ResolveSteamRenderer()
{
    if (steamGameState_ != SteamGameState::Resolving || !steamRunningAppId_ ||
        suspended_ || DiagnosticRunning())
        return;
    if (!steamGpuEngineSampler_.Initialized())
    {
        if (!steamGpuEngineSampler_.Initialize())
            return;
    }
    const auto activity = steamGpuEngineSampler_.Sample();
    HWND foreground = GetForegroundWindow();
    DWORD foregroundPid{};
    if (foreground)
        GetWindowThreadProcessId(foreground, &foregroundPid);
    const auto candidates = clawhud::SelectGpuActiveProcessIds(
        activity, foregroundPid, steamBaselineProcessIds_);
    for (const auto processId : candidates)
    {
        if (processId == GetCurrentProcessId() || !ProcessAlive(processId))
            continue;
        if (processId == steamRendererCandidatePid_ && presentMonHudTelemetry_ &&
            presentMonHudTelemetry_->Running())
            return;
        const bool candidateChanged = steamRendererCandidatePid_ != processId;
        steamRendererCandidatePid_ = processId;
        if (candidateChanged)
            StopProductionPresentMonSampling(L"steam-candidate-changed", true);
        StartGraphicsApiProbe(processId);
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info,
            L"Steam renderer candidate appId=" + std::to_wstring(steamRunningAppId_) +
            L" pid=" + std::to_wstring(processId) + L" source=gpu" +
            (processId == foregroundPid ? L"+foreground" : L"") +
            L" igcl=" + (latestGraphicsApi_.value_or(L"unresolved")));
        if (!StartSteamPresentMon(processId))
        {
            steamRendererCandidatePid_ = 0;
        }
        return;
    }
}

bool App::StartSteamPresentMon(DWORD processId)
{
    const auto executable = std::filesystem::path(executablePath_).parent_path() /
        L"tools" / L"PresentMon.exe";
    presentMonHudTelemetry_ = std::make_unique<clawhud::PresentMonHudTelemetry>();
    presentMonProcessId_ = processId;
    const bool started = presentMonHudTelemetry_->Start(executable.wstring(), processId,
        [this, processId](clawhud::PresentMonHudSample sample)
        {
            auto* update = new PresentMonHudUpdate{processId, true, sample};
            if (!PostMessageW(tray_.Window(), kPresentMonHudUpdate,
                reinterpret_cast<WPARAM>(update), 0))
                delete update;
        });
    if (started)
        return true;
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
        L"PresentMon start failed for Steam renderer candidate pid=" +
        std::to_wstring(processId));
    presentMonHudTelemetry_.reset();
    presentMonProcessId_ = 0;
    return false;
}

void App::HandleSteamRunningAppId(std::uint32_t appId)
{
    if (appId == steamRunningAppId_)
        return;
    const auto oldAppId = steamRunningAppId_;
    steamRunningAppId_ = appId;
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info,
        L"Steam session changed oldAppId=" + std::to_wstring(oldAppId) +
        L" newAppId=" + std::to_wstring(appId) + L" state=" +
        (appId ? L"Resolving" : L"None"));
    StopSteamRendererResolution(true);
    steamRendererPid_ = 0;
    steamGameState_ = appId ? SteamGameState::Resolving : SteamGameState::None;
    foregroundTracker_.SetTrackedProcessId(0);
    if (appId)
    {
        StartSteamRendererResolution();
        return;
    }
    ReconcileProductionTargetAuthority();
}

void App::ReconcileProductionTargetAuthority()
{
    if (steamRunningAppId_)
    {
        if (steamGameState_ == SteamGameState::Resolving)
            ResolveSteamRenderer();
        else if (steamGameState_ == SteamGameState::Active && steamRendererPid_ &&
            ProcessAlive(steamRendererPid_) && !presentMonHudTelemetry_)
        {
            steamRendererCandidatePid_ = steamRendererPid_;
            StartGraphicsApiProbe(steamRendererPid_);
            StartSteamPresentMon(steamRendererPid_);
        }
        return;
    }
    AdoptForegroundProductionTarget();
}

void App::HandlePresentMonHudUpdate(DWORD processId, bool steamResolution,
    clawhud::PresentMonHudSample sample)
{
    const auto displayedFps = sample.displayedFps;
    if (steamResolution)
    {
        if (suspended_ || resumeRecoveryActive_ || DiagnosticRunning() ||
            !presentMonHudTelemetry_ || presentMonProcessId_ != processId ||
            !steamRunningAppId_ ||
            (steamGameState_ == SteamGameState::Resolving &&
                steamRendererCandidatePid_ != processId) ||
            (steamGameState_ == SteamGameState::Active &&
                steamRendererPid_ != processId))
            return;
        if (steamGameState_ == SteamGameState::Resolving &&
            sample.hasDisplayedFrame && ProcessAlive(processId))
        {
            steamRendererPid_ = processId;
            steamRendererCandidatePid_ = 0;
            steamGameState_ = SteamGameState::Active;
            latestPresentMonDisplayedFps_ = displayedFps;
            steamGpuEngineSampler_.Reset();
            KillTimer(tray_.Window(), kSteamRendererResolveTimerId);
            foregroundTracker_.SetTrackedProcessId(processId);
            StartGraphicsApiProbe(processId);
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info,
                L"Steam renderer confirmed appId=" +
                std::to_wstring(steamRunningAppId_) + L" pid=" +
                std::to_wstring(processId) + L" firstPresent=1 igcl=" +
                latestGraphicsApi_.value_or(L"unresolved"));
            ReconcileHudVisibility();
            return;
        }
        if (sample.streamEnded || !ProcessAlive(processId))
        {
            if (steamGameState_ == SteamGameState::Active)
                clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info,
                    L"Steam renderer lost appId=" +
                    std::to_wstring(steamRunningAppId_) + L" pid=" +
                    std::to_wstring(processId) + L" state=Resolving");
            StopSteamRendererResolution(true);
            steamRendererPid_ = 0;
            steamGameState_ = SteamGameState::Resolving;
            foregroundTracker_.SetTrackedProcessId(0);
            StartSteamRendererResolution();
            ReconcileHudVisibility();
            return;
        }
        if (displayedFps)
        {
            latestPresentMonDisplayedFps_ = displayedFps;
            RenderProductionHud();
        }
        return;
    }
    if (suspended_ || resumeRecoveryActive_ || DiagnosticRunning() ||
        !presentMonHudTelemetry_ ||
        presentMonProcessId_ != processId ||
        (foregroundTracker_.TrackedProcessId() != processId &&
            pendingProductionTargetPid_ != processId))
        return;
    if (pendingProductionTargetPid_ == processId && displayedFps)
    {
        latestPresentMonDisplayedFps_ = displayedFps;
        ConfirmForegroundProductionTarget(processId);
        presentMonRestartPid_ = processId;
        presentMonRestartAttempts_ = 0;
        return;
    }
    if (!ProcessAlive(processId))
        StopGraphicsApiProbe();
    latestPresentMonDisplayedFps_ = displayedFps;
    RenderProductionHud();
    if (sample.streamEnded)
    {
        if (!ProcessAlive(processId))
        {
            Log(L"PresentMon target process exited pid=" + std::to_wstring(processId));
            if (foregroundTracker_.TrackedProcessId() == processId)
                ReleaseCommittedProductionTarget(L"game-exited");
            else
            {
                pendingProductionTargetPid_ = 0;
                StopProductionPresentMonSampling(L"game-exited", true);
                StopGraphicsApiProbe();
            }
            return;
        }
        else
        {
            const DWORD exitCode = presentMonHudTelemetry_->ExitCode();
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"PresentMon exited unexpectedly pid=" + std::to_wstring(processId) +
                L" exitCode=" + std::to_wstring(exitCode));
        }
        StopProductionPresentMonSampling(L"unexpected-exit", false);
        if (presentMonRestartPid_ != processId)
        {
            presentMonRestartPid_ = processId;
            presentMonRestartAttempts_ = 0;
        }
        if (clawhud::ShouldRetryProductionPresentMon(
            presentMonRestartPid_, presentMonRestartAttempts_, processId))
        {
            ++presentMonRestartAttempts_;
            StartProductionPresentMonSampling(true);
        }
        else
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"PresentMon recovery exhausted pid=" + std::to_wstring(processId));
    }
    else if (displayedFps)
    {
        presentMonRestartPid_ = processId;
        presentMonRestartAttempts_ = 0;
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
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"Track Mock Game ignored while VRR diagnostic is running");
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
    igclGpuSampler_.Reset();
    latestIgclGpuTelemetry_.reset();
    igclTelemetryAvailable_ = false;
    igclTelemetryFailureCount_ = 0;
    StartGraphicsApiProbe(processId);
    if (EnsureMockHud())
    {
        mockHudEnabled_ = true;
        ReconcileHudVisibility();
    }
}

bool App::AdoptForegroundProductionTarget()
{
    HWND foreground = GetForegroundWindow();
    DWORD processId{};
    if (foreground)
        GetWindowThreadProcessId(foreground, &processId);
    return AdoptForegroundProductionTarget(foreground, processId);
}

bool App::AdoptForegroundProductionTarget(HWND window, DWORD processId)
{
    if (steamRunningAppId_)
    {
        if (steamGameState_ == SteamGameState::Resolving)
            ResolveSteamRenderer();
        else
            ReconcileProductionTargetAuthority();
        return false;
    }
    if (!clawhud::ShouldConsiderForegroundProductionTarget(
        mockHudEnabled_, DiagnosticRunning(), suspended_))
        return false;
    const DWORD trackedProcessId = foregroundTracker_.TrackedProcessId();
    if (trackedProcessId &&
        clawhud::ShouldRetainCommittedProductionTarget(
            trackedProcessId, ProcessAlive(trackedProcessId)))
    {
        if (graphicsApiProcessId_ != trackedProcessId)
            StartGraphicsApiProbe(trackedProcessId);
        StartProductionPresentMonSampling();
        return true;
    }

    if (trackedProcessId)
        ReleaseCommittedProductionTarget(L"game-exited");

    const DWORD committedProcessId = foregroundTracker_.TrackedProcessId();

    std::wstring imageName;
    if (!IsUsableProductionTarget(window, processId, imageName))
    {
        if (pendingProductionTargetPid_ &&
            ProcessAlive(pendingProductionTargetPid_))
            return true;
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"Production target rejected pid=" + std::to_wstring(processId) +
            L" image=" + (imageName.empty() ? L"<unavailable>" : imageName));
        if (pendingProductionTargetPid_)
        {
            pendingProductionTargetPid_ = 0;
            StopProductionPresentMonSampling(L"candidate-replaced", true);
            StopGraphicsApiProbe();
        }
        return false;
    }

    const bool newCandidate =
        clawhud::ShouldEvaluateForegroundCandidate(committedProcessId, processId) &&
        clawhud::ShouldReplacePendingCandidate(pendingProductionTargetPid_, processId);
    if (newCandidate)
    {
        const DWORD oldProcessId = pendingProductionTargetPid_
            ? pendingProductionTargetPid_ : committedProcessId;
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"Production target foreground changed old=" +
            std::to_wstring(oldProcessId) + L" new=" +
            std::to_wstring(processId));
        pendingProductionTargetPid_ = processId;
        presentMonRestartPid_ = 0;
        presentMonRestartAttempts_ = 0;
        StopProductionPresentMonSampling(L"target-handoff", true);
        StopGraphicsApiProbe();
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"Production target candidate pid=" + std::to_wstring(processId) +
            L" image=" + imageName);
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"Production target validation started pid=" +
            std::to_wstring(processId));
        StartGraphicsApiProbe(processId);
    }
    else if (graphicsApiProcessId_ != processId)
        StartGraphicsApiProbe(processId);
    StartProductionPresentMonSampling();
    return true;
}

void App::ConfirmForegroundProductionTarget(DWORD processId)
{
    if (pendingProductionTargetPid_ != processId ||
        !clawhud::ShouldConsiderForegroundProductionTarget(
            mockHudEnabled_, DiagnosticRunning(), suspended_) ||
        !ProcessAlive(processId))
        return;
    HWND foreground = GetForegroundWindow();
    DWORD foregroundProcessId{};
    if (foreground)
        GetWindowThreadProcessId(foreground, &foregroundProcessId);
    if (!clawhud::ShouldConfirmProductionTarget(
        pendingProductionTargetPid_, processId, foregroundProcessId, true))
    {
        std::wstring foregroundImage;
        const bool foregroundUsable = foregroundProcessId != 0 &&
            IsUsableProductionTarget(
                foreground, foregroundProcessId, foregroundImage);
        if (clawhud::ShouldDeferPendingProductionValidation(
            pendingProductionTargetPid_, foregroundProcessId, foregroundUsable,
            ProcessAlive(processId)))
            return;
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"Production target validation discarded pid=" +
            std::to_wstring(processId) + L" reason=foreground-changed");
        pendingProductionTargetPid_ = 0;
        if (presentMonProcessId_ == processId)
            StopProductionPresentMonSampling(L"candidate-replaced", true);
        if (graphicsApiProcessId_ == processId)
            StopGraphicsApiProbe();
        return;
    }
    const DWORD previousProcessId = foregroundTracker_.TrackedProcessId();
    pendingProductionTargetPid_ = 0;
    foregroundTracker_.SetTrackedProcessId(processId);
    presentMonRestartPid_ = processId;
    presentMonRestartAttempts_ = 0;
    usageSampler_.Reset();
    latestUsageTelemetry_.reset();
    igclGpuSampler_.Reset();
    latestIgclGpuTelemetry_.reset();
    igclTelemetryAvailable_ = false;
    igclTelemetryFailureCount_ = 0;
    StartGraphicsApiProbe(processId);
    Log(L"Production target confirmed pid=" + std::to_wstring(processId));
    if (previousProcessId && previousProcessId != processId)
        Log(L"Production target handoff old=" + std::to_wstring(previousProcessId) +
            L" new=" + std::to_wstring(processId));
    ReconcileHudVisibility();
}

bool App::IsUsableProductionTarget(HWND window, DWORD processId,
    std::wstring& imageName) const
{
    imageName.clear();
    if (!window || !processId || processId == GetCurrentProcessId() ||
        !IsWindowVisible(window) || GetShellWindow() == window ||
        GetDesktopWindow() == window)
        return false;

    wchar_t imagePath[MAX_PATH]{};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return false;
    DWORD length = ARRAYSIZE(imagePath);
    const bool queried = QueryFullProcessImageNameW(process, 0, imagePath, &length) != FALSE;
    CloseHandle(process);
    if (!queried)
        return false;

    std::wstring image(imagePath, length);
    const auto separator = image.find_last_of(L"\\/");
    if (separator != std::wstring::npos)
        image.erase(0, separator + 1);
    std::transform(image.begin(), image.end(), image.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    imageName = image;
    return !clawhud::IsRejectedProductionTargetImage(image);
}

void App::SetHudVisibilityMode(clawhud::HudVisibilityMode mode)
{
    if (VrrDiagnosticRunning())
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"HUD visibility mode change ignored while VRR diagnostic is running");
        return;
    }
    hudOptions_.visibilityMode = mode;
    manualHudVisibilityOverride_.reset();
    SaveHudSettings();
    ReconcileHudVisibility();
}

void App::HandleHudToggleHotkey()
{
    if (vrrDiagnostic_ && vrrDiagnostic_->WaitingForTrigger())
    {
        if (vrrDiagnostic_->TriggerFromForeground())
            Log(L"VRR diagnostic triggered by F8");
        return;
    }
    if (VrrDiagnosticRunning())
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"F8 ignored while VRR diagnostic is running");
        return;
    }
    if (!mockHudEnabled_)
    {
        if (!EnsureMockHud())
        {
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
                L"F8 HUD ON initialization failed");
            return;
        }
        mockHudEnabled_ = true;
        mockFrameIndex_ = 0;
        AdoptForegroundProductionTarget();
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
    StopProductionEcSampling(true, L"diagnostic-start");
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
    StopProductionPresentMonSampling(L"diagnostic-start", false);
    StopProductionEcSampling(false, L"diagnostic-start");
    pendingProductionTargetPid_ = 0;
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
    if (suspended_ || resumeRecoveryActive_)
    {
        KillTimer(tray_.Window(), kMockHudTimerId);
        hudPresentation_->Hide();
        return;
    }
    if (mockHudEnabled_ && foregroundTracker_.TrackedProcessId() &&
        !ProcessAlive(foregroundTracker_.TrackedProcessId()))
        AdoptForegroundProductionTarget();
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
            if (clawhud::ShouldSampleProductionTelemetry(
                    resolvedShow, diagnosticHudMode_.has_value(), suspended_))
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
                    L"HUD show failed hr=" + HexHresult(hr));
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
                L"HUD hide failed hr=" + HexHresult(hr));
        if (FAILED(hr))
            hudHideFailureLogged_ = true;
        KillTimer(tray_.Window(), kMockHudTimerId);
        if (!diagnosticHudMode_.has_value())
            StopProductionEcSampling(false, L"hud-hidden");
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
    mockHudEnabled_ = true;
    const auto path = HudSettingsPath();
    if (path.empty()) return;
    mockHudEnabled_ = ReadBoolSetting(path, L"HUD", L"Enabled", true);
    diagnosticsTabEnabled_ = ReadBoolSetting(
        path, L"Developer", L"DiagnosticsTabEnabled", false);
    debugLoggingEnabled_ = ReadBoolSetting(
        path, L"Developer", L"DebugLoggingEnabled", false);
    wchar_t startup[8]{};
    GetPrivateProfileStringW(L"General", L"StartWithWindows", L"1", startup,
        ARRAYSIZE(startup), path.c_str());
    startWithWindows_ = std::wcstol(startup, nullptr, 10) != 0;
    const auto alignment = ReadHudSetting(path, L"Alignment", L"Center");
    if (alignment == L"Left") hudOptions_.alignment = clawhud::HudAlignment::Left;
    else if (alignment == L"Right") hudOptions_.alignment = clawhud::HudAlignment::Right;
    hudFont_ = clawhud::ParseHudFont(ReadHudSetting(path, L"Font", L"Unispace"));
    const auto background = ReadHudSetting(path, L"BackgroundWidth", L"FullWidth");
    if (background == L"ContentWidth") hudOptions_.backgroundMode = clawhud::HudBackgroundMode::ContentWidth;
    else if (background == L"FullWidth") hudOptions_.backgroundMode = clawhud::HudBackgroundMode::FullWidth;
    const auto visibility = ReadHudSetting(path, L"VisibilityMode", L"InGameOnly");
    if (visibility == L"Always") hudOptions_.visibilityMode = clawhud::HudVisibilityMode::Always;
    else if (visibility == L"InGameOnly") hudOptions_.visibilityMode = clawhud::HudVisibilityMode::InGameOnly;
    hudSizeOffset_ = clawhud::ParseHudSizeOffset(ReadHudSetting(path, L"Size", L"0"));
    const auto configuredOpacity = ReadHudSetting(path, L"HudOpacity", L"__missing__");
    const auto legacyOpacity = ReadHudSetting(path, L"BackgroundOpacity", L"");
    hudOptions_.backgroundOpacity = clawhud::HudOpacityFractionFromPercent(
        clawhud::HudOpacityPercentFromSettings(configuredOpacity,
            configuredOpacity != L"__missing__", legacyOpacity));
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
    const wchar_t* font = clawhud::HudFontIniToken(hudFont_);
    wchar_t opacity[8]{};
    swprintf_s(opacity, L"%ld", clawhud::HudOpacityPercentFromFraction(
        hudOptions_.backgroundOpacity));
    bool saved = WritePrivateProfileStringW(L"HUD", L"Alignment", alignment, path.c_str()) != FALSE;
    saved = WritePrivateProfileStringW(L"HUD", L"Font", font, path.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"HUD", L"BackgroundWidth", background, path.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"HUD", L"HudOpacity", opacity, path.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"HUD", L"VisibilityMode", visibility, path.c_str()) != FALSE && saved;
    wchar_t size[8]{};
    swprintf_s(size, L"%d", clawhud::ClampHudSizeOffset(hudSizeOffset_));
    saved = WritePrivateProfileStringW(L"HUD", L"Size", size, path.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"General", L"StartWithWindows",
        startWithWindows_ ? L"1" : L"0", path.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"Developer", L"DebugLoggingEnabled",
        debugLoggingEnabled_ ? L"1" : L"0", path.c_str()) != FALSE && saved;
    if (!saved)
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error, L"Settings save failed");
}

void App::SaveHudEnabledSetting(bool enabled) const
{
    const auto path = HudSettingsPath();
    if (path.empty()) return;
    const auto separator = path.find_last_of(L'\\');
    if (separator != std::wstring::npos)
        CreateDirectoryW(path.substr(0, separator).c_str(), nullptr);
    if (!WritePrivateProfileStringW(L"HUD", L"Enabled", enabled ? L"1" : L"0", path.c_str()))
    {
        clawhud::RuntimeLogger::Log(
            clawhud::RuntimeLogLevel::Error, L"Settings save failed key=Enabled");
    }
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
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"Open Settings ignored while VRR diagnostic is running");
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
    CancelResumeRecovery();
    KillTimer(tray_.Window(), kMockHudTimerId);
    StopProductionEcSampling(true, L"app-shutdown");
    steamRunningAppIdSource_.Stop();
    KillTimer(tray_.Window(), kSteamRendererResolveTimerId);
    steamGpuEngineSampler_.Reset();
    StopGraphicsApiProbe();
    foregroundTracker_.Stop();
    if (vrrDiagnostic_) vrrDiagnostic_->Stop();
    if (ecDiagnostic_) ecDiagnostic_->Stop();
    if (igclDiagnostic_) igclDiagnostic_->Stop();
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
        if (message.message == kSteamRunningAppIdChanged)
        {
            HandleSteamRunningAppId(static_cast<std::uint32_t>(message.wParam));
            continue;
        }
        if (message.message == kPresentMonHudUpdate)
        {
            auto* update = reinterpret_cast<PresentMonHudUpdate*>(message.wParam);
            if (update)
            {
                HandlePresentMonHudUpdate(update->processId, update->steamResolution,
                    update->sample);
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
        if (message.message == clawhud::kIgclDiagnosticStatus)
        {
            auto* status = reinterpret_cast<std::wstring*>(message.wParam);
            if (status) { igclStatus_ = *status; if (settings_) settings_->SetDiagnosticStatus(*status); }
            delete status;
            continue;
        }
        if (message.message == clawhud::kIgclDiagnosticCompleted)
        {
            FinishIgclDiagnostic(message.wParam != 0);
            continue;
        }
        if (message.message == kVrrDiagnosticStatus)
        {
            auto* status = reinterpret_cast<std::wstring*>(message.wParam);
            if (status)
            {
                vrrStatus_ = *status;
                if (settings_) settings_->SetVrrStatus(*status);
                if (VrrDiagnosticStatusRequiresForegroundReevaluation(*status) &&
                    clawhud::ShouldReevaluateForegroundAfterDiagnostic(
                        mockHudEnabled_, DiagnosticRunning(), suspended_))
                {
                    AdoptForegroundProductionTarget();
                    ReconcileHudVisibility();
                }
            }
            delete status;
            continue;
        }
        if (message.message == kVrrDiagnosticCompleted)
        {
            if (clawhud::ShouldReevaluateForegroundAfterDiagnostic(
                mockHudEnabled_, DiagnosticRunning(), suspended_))
            {
                AdoptForegroundProductionTarget();
                ReconcileHudVisibility();
            }
            continue;
        }
        if (settings_ && settings_->Window() &&
            IsWindowVisible(settings_->Window()) &&
            IsDialogMessageW(settings_->Window(), &message))
        {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
