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
#include "GameDetection/GameDetectionTrace.h"
#include "TelemetryRetention.h"

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
constexpr unsigned kEcTelemetryMissingThreshold = 3;
constexpr unsigned kUsageUnavailableFailureThreshold = 3;
constexpr UINT kSettingsDestroyed = WM_APP + 1;
constexpr UINT kForegroundChanged = WM_APP + 2;
constexpr UINT kHudVisibilityRequest = WM_APP + 3;
constexpr UINT kSteamRunningAppIdChanged = WM_APP + 5;
constexpr UINT kMicrosoftGameEvidence = WM_APP + 6;
constexpr UINT kGameRenderVerifierUpdate = WM_APP + 7;
constexpr UINT kProductionWindowEvent = WM_APP + 8;
constexpr UINT kProductionProcessExit = WM_APP + 9;
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

struct MicrosoftGameEvidenceUpdate
{
    clawhud::MicrosoftGameTriggerEvidence evidence;
};

struct ProductionWindowEventUpdate
{
    clawhud::ProductionWindowEvent event;
};

struct ProductionProcessExitUpdate
{
    DWORD processId{};
    std::uint64_t generation{};
};

struct GameRenderVerifierUpdate
{
    clawhud::GameRenderVerifierEvent event;
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

std::wstring HwndText(HWND window)
{
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%p", window);
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

App::App(HINSTANCE instance) : instance_(instance), tray_(*this),
    steamRunningAppTrigger_(gameDetectionCoordinator_)
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
    productionGameWindowSource_.Stop();
    productionProcessLifetimeWatcher_.Disarm();
    foregroundTracker_.Stop();
    windowLifecycleSource_.Stop();
    presentActivitySource_.Stop();
    processLifecycleSource_.Stop();
    if (vrrDiagnostic_) vrrDiagnostic_->Stop();
    if (presentMonApi2Diagnostic_) presentMonApi2Diagnostic_->Stop();
    DiscardPendingHudVisibilityRequests();
    StopProductionPresentMonSampling(L"app-shutdown", true);
    StopGlobalRendererTelemetry();
    DiscardPendingGlobalRendererTelemetry();
    steamRunningAppIdSource_.Stop();
    DiscardPendingGameRenderVerifierEvents();
    DiscardPendingMicrosoftGameEvidence();
    DiscardPendingProductionWindowEvents();
    DiscardPendingProductionProcessExitEvents();
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
    if (!productionGameWindowSource_.Start(
        [this](const clawhud::ProductionWindowEvent& event)
        {
            auto* windowUpdate = new ProductionWindowEventUpdate{event};
            if (!PostMessageW(tray_.Window(), kProductionWindowEvent,
                reinterpret_cast<WPARAM>(windowUpdate), 0))
                delete windowUpdate;

            if (const auto evidence = microsoftGameTrigger_.InspectWindowEvent(event))
            {
                clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info,
                    L"[GameDetection] microsoft.evidence pid=" +
                    std::to_wstring(evidence->processId) + L" hwnd=" +
                    HwndText(evidence->window) + L" sequence=" +
                    std::to_wstring(evidence->sourceSequence));
                auto* update = new MicrosoftGameEvidenceUpdate{*evidence};
                if (!PostMessageW(tray_.Window(), kMicrosoftGameEvidence,
                    reinterpret_cast<WPARAM>(update), 0))
                    delete update;
            }
        }))
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"Production game window source failed to start; continuing with generic/Steam detection");
    }
    const bool steamWatcherStarted = steamRunningAppIdSource_.Start(
        tray_.Window(), kSteamRunningAppIdChanged);
    if (!steamWatcherStarted)
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"Steam RunningAppID watcher initialization failed");
    steamRunningAppId_ = steamRunningAppIdSource_.GetRunningAppId();
    if (steamRunningAppId_ != 0)
        Log(L"[GameDetection] steam.session oldAppId=0 newAppId=" +
            std::to_wstring(steamRunningAppId_));
    HandleGameDetectionTransition(
        steamRunningAppTrigger_.Initialize(steamRunningAppId_));
    if (steamWatcherStarted)
        Log(L"[GameDetection] steam.watcher-started appId=" +
            std::to_wstring(steamRunningAppId_));
    if (debugLoggingEnabled_ && !processLifecycleSource_.Start())
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"Process lifecycle diagnostic source failed to start; continuing");
    if (debugLoggingEnabled_)
    {
        if (!windowLifecycleSource_.Start())
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"Window lifecycle diagnostic source failed to start; continuing");
        const auto executable = std::filesystem::path(executablePath_).parent_path() /
            L"tools" / L"PresentMon.exe";
        if (!presentActivitySource_.Start(executable.wstring()))
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"Present activity diagnostic source failed to start; continuing");
    }
    hudHotkeyRegistered_ = RegisterHotKey(tray_.Window(), kHudToggleHotkeyId,
        MOD_NOREPEAT, VK_F8) != FALSE;
    if (!hudHotkeyRegistered_)
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"RegisterHotKey(F8) failed; continuing without the global HUD toggle");
    ecDiagnostic_ = std::make_unique<EcDiagnostic>(tray_.Window());
    igclDiagnostic_ = std::make_unique<clawhud::IgclTelemetryDiagnostic>(tray_.Window());
    vrrDiagnostic_ = std::make_unique<VrrDiagnostic>(*this, tray_.Window());
    presentMonApi2Diagnostic_ = std::make_unique<clawhud::PresentMonApi2Diagnostic>(tray_.Window());
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
            {
                std::scoped_lock lock(rendererTargetMutex_);
                rendererTargetSelector_.SetForegroundProcess(processId);
                selectedRendererFps_ = rendererTargetSelector_.Selection()
                    ? rendererTargetSelector_.Selection()->fps : std::nullopt;
            }
            ReconcileHudVisibility();
            if (debugLoggingEnabled_)
                windowsGameIdentitySource_.QueueInspect(window, processId);
            if (mockHudEnabled_ && !DiagnosticRunning())
                HandleProductionForegroundChanged(window, processId);
        }))
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"Foreground tracker initialization failed");
        return 1;
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
            StartGlobalRendererTelemetry();
            ReevaluateProductionGameDetection();
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
    if (enabled)
    {
        if (!processLifecycleSource_.Start())
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"Process lifecycle diagnostic source failed to start; continuing");
        if (!windowLifecycleSource_.Start())
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"Window lifecycle diagnostic source failed to start; continuing");
        const auto executable = std::filesystem::path(executablePath_).parent_path() /
            L"tools" / L"PresentMon.exe";
        if (!presentActivitySource_.Start(executable.wstring()))
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"Present activity diagnostic source failed to start; continuing");
    }
    else
    {
        windowLifecycleSource_.Stop();
        presentActivitySource_.Stop();
        processLifecycleSource_.Stop();
    }
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
    StopProductionPresentMonSampling(L"diagnostic-start", false);
    StopGlobalRendererTelemetry();
    DiscardPendingGlobalRendererTelemetry();
    StopGraphicsApiProbe();
    if (gameDetectionCoordinator_.Context().state != clawhud::GameDetectionState::Committed)
        ClearProductionCandidate(L"diagnostic-start");
    if (!ecDiagnostic_->Start())
    {
        if (mockHudEnabled_ && !suspended_)
            ReevaluateProductionGameDetection();
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
    StopProductionEcSampling(false, L"igcl-diagnostic-start");
    StopProductionPresentMonSampling(L"igcl-diagnostic-start", false);
    StopGlobalRendererTelemetry();
    StopGraphicsApiProbe();
    if (gameDetectionCoordinator_.Context().state != clawhud::GameDetectionState::Committed)
        ClearProductionCandidate(L"diagnostic-start");
    if (!igclDiagnostic_->Start())
    {
        if (mockHudEnabled_ && !suspended_) ReevaluateProductionGameDetection();
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
        ReevaluateProductionGameDetection();
    ReconcileHudVisibility();
}
bool App::IgclDiagnosticRunning() const { return igclDiagnostic_ && igclDiagnostic_->Running(); }
void App::FinishIgclDiagnostic(bool success)
{
    if (igclDiagnostic_) igclDiagnostic_->Stop();
    if (clawhud::ShouldReevaluateForegroundAfterDiagnostic(
        mockHudEnabled_, DiagnosticRunning(), suspended_))
        ReevaluateProductionGameDetection();
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
        ReevaluateProductionGameDetection();
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
    StopGlobalRendererTelemetry();
    StopGraphicsApiProbe();
    if (gameDetectionCoordinator_.Context().state != clawhud::GameDetectionState::Committed)
        ClearProductionCandidate(L"diagnostic-start");
    if (!vrrDiagnostic_->Start())
    {
        if (clawhud::ShouldReevaluateForegroundAfterDiagnostic(
            mockHudEnabled_, DiagnosticRunning(), suspended_))
            ReevaluateProductionGameDetection();
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
        ReevaluateProductionGameDetection();
    ReconcileHudVisibility();
}
bool App::VrrDiagnosticRunning() const { return vrrDiagnostic_ && vrrDiagnostic_->Running(); }
bool App::StartPresentMonApi2Diagnostic()
{
    if (!presentMonApi2Diagnostic_ || DiagnosticRunning()) return false;
    ecStatus_ = L"Idle";
    igclStatus_ = L"Idle";
    StopProductionEcSampling(false, L"api2-diagnostic-start");
    StopProductionPresentMonSampling(L"api2-diagnostic-start", false);
    StopGlobalRendererTelemetry();
    StopGraphicsApiProbe();
    if (!presentMonApi2Diagnostic_->Start())
    {
        presentMonApi2Status_ = L"Start failed";
        return false;
    }
    presentMonApi2Status_ = L"Waiting 5 seconds...";
    if (settings_) settings_->RequestClose();
    return true;
}
void App::StopPresentMonApi2Diagnostic()
{
    if (presentMonApi2Diagnostic_) presentMonApi2Diagnostic_->Stop();
    if (clawhud::ShouldReevaluateForegroundAfterDiagnostic(
        mockHudEnabled_, DiagnosticRunning(), suspended_))
        ReevaluateProductionGameDetection();
    ReconcileHudVisibility();
}
bool App::PresentMonApi2DiagnosticRunning() const
{
    return presentMonApi2Diagnostic_ && presentMonApi2Diagnostic_->Running();
}
bool App::DiagnosticRunning() const
{
    return EcDiagnosticRunning() || VrrDiagnosticRunning() ||
        IgclDiagnosticRunning() || PresentMonApi2DiagnosticRunning();
}
void App::StopDiagnostic()
{
    StopVrrDiagnostic(); StopEcDiagnostic(); StopIgclDiagnostic();
    StopPresentMonApi2Diagnostic();
}

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
    DiscardPendingGameRenderVerifierEvents();
    DiscardPendingMicrosoftGameEvidence();
    DiscardPendingProductionWindowEvents();
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
        DiscardPendingGameRenderVerifierEvents();
        DiscardPendingProductionWindowEvents();
        DiscardPendingMicrosoftGameEvidence();
        Log(L"Suspend notification was missed; resume fallback prepared");
    }
    suspended_ = false;
    globalRendererTelemetryUnavailable_ = false;
    if (mockHudEnabled_)
        StartGlobalRendererTelemetry();
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
        processId, gameRenderVerifier_.ProcessId(), gameRenderVerifier_.Running());
    bool rendererForegroundActive = false;
    {
        std::scoped_lock lock(rendererTargetMutex_);
        rendererForegroundActive = rendererTargetSelector_.
            ForegroundHasActiveRenderer(GetTickCount64());
    }
    const bool expectedVisible = mockHudEnabled_ &&
        (manualHudVisibilityOverride_.has_value()
            ? *manualHudVisibilityOverride_
            : hudOptions_.visibilityMode == clawhud::HudVisibilityMode::Always ||
                rendererForegroundActive ||
                foregroundTracker_.ForegroundIsTrackedProcess());
    const bool visibilityUsesForeground = !manualHudVisibilityOverride_.has_value() &&
        hudOptions_.visibilityMode == clawhud::HudVisibilityMode::InGameOnly;
    DiscardPendingGameRenderVerifierEvents();
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
        if (retainPresentMon && gameRenderVerifier_.ProcessId() == processId &&
            gameRenderVerifier_.Running())
            Log(L"[GameDetection] verifier.resume-retained pid=" +
                std::to_wstring(processId));
        else if (processAlive && gameRenderVerifier_.ProcessId() == processId &&
            gameRenderVerifier_.Running())
            Log(L"[GameDetection] verifier.resume-restarted pid=" +
                std::to_wstring(processId));
        const unsigned completedAttempt = resumeRecoveryAttempts_;
        CancelResumeRecovery();
        if (clawhud::ShouldReevaluateForegroundAfterResume(
            mockHudEnabled_, recovered))
            ReevaluateProductionGameDetection();
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
    StopGlobalRendererTelemetry();
    StopGraphicsApiProbe();
    if (gameDetectionCoordinator_.Context().state != clawhud::GameDetectionState::Committed)
        ClearProductionCandidate(L"hud-disabled");
    latestPresentMonDisplayedFps_.reset();
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
    globalRendererTelemetryUnavailable_ = false;
    mockFrameIndex_ = 0;
    ReevaluateProductionGameDetection();
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

    const auto abortAfterFailure = [this]()
    {
        return clawhud::ShouldAbortEcTelemetrySample(
            ecHudClient_->LastStage());
    };

    std::vector<std::uint8_t> payload;
    if (ecHudClient_->ReadTemperature(payload))
        result.cpuTempC = clawhud::DecodeCpuTempC(payload);
    else if (abortAfterFailure())
    {
        ecHudClient_->Close();
        return result;
    }

    payload.clear();
    if (ecHudClient_->ReadFan(payload))
    {
        if (const auto fans = clawhud::DecodeFanTelemetry(payload))
        {
            result.fan1Rpm = fans->fan1Rpm;
            result.fan2Rpm = fans->fan2Rpm;
        }
    }
    else if (abortAfterFailure())
    {
        ecHudClient_->Close();
        return result;
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
    snapshot.presentMonDisplayedFps = selectedRendererFps_
        ? selectedRendererFps_ : latestPresentMonDisplayedFps_;
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
    const auto freshEcTelemetry = ReadHudEcTelemetry();
    clawhud::UpdateRetainedTelemetryField(
        ecHudTelemetry_.cpuTempC, freshEcTelemetry.cpuTempC,
        ecCpuTempMissingCount_, kEcTelemetryMissingThreshold);
    clawhud::UpdateRetainedTelemetryField(
        ecHudTelemetry_.fan1Rpm, freshEcTelemetry.fan1Rpm,
        ecFan1MissingCount_, kEcTelemetryMissingThreshold);
    clawhud::UpdateRetainedTelemetryField(
        ecHudTelemetry_.fan2Rpm, freshEcTelemetry.fan2Rpm,
        ecFan2MissingCount_, kEcTelemetryMissingThreshold);
    clawhud::UpdateRetainedTelemetryField(
        ecHudTelemetry_.cpuPackagePowerW, freshEcTelemetry.cpuPackagePowerW,
        ecTdpMissingCount_, kEcTelemetryMissingThreshold);
    if (!usageSampler_.Initialized())
    {
        if (!usageSampler_.Initialize())
        {
            latestUsageTelemetry_.reset();
            usageTelemetryFailureCount_ = 0;
            usageCpuMissingCount_ = 0;
            usageMemoryMissingCount_ = 0;
            usageGpuMemoryMissingCount_ = 0;
        }
    }
    if (usageSampler_.Initialized())
    {
        const auto sample = usageSampler_.Sample();
        if (sample)
        {
            latestUsageTelemetry_ = clawhud::MergeWindowsUsageTelemetry(
                latestUsageTelemetry_, sample);
            clawhud::UpdateRetainedTelemetryField(
                latestUsageTelemetry_->cpuUsagePercent,
                sample->cpuUsagePercent, usageCpuMissingCount_,
                kUsageUnavailableFailureThreshold);
            clawhud::UpdateRetainedTelemetryField(
                latestUsageTelemetry_->systemMemoryUsedBytes,
                sample->systemMemoryUsedBytes, usageMemoryMissingCount_,
                kUsageUnavailableFailureThreshold);
            clawhud::UpdateRetainedTelemetryField(
                latestUsageTelemetry_->intelGpuMemoryUsedBytes,
                sample->intelGpuMemoryUsedBytes, usageGpuMemoryMissingCount_,
                kUsageUnavailableFailureThreshold);
            usageTelemetryFailureCount_ = 0;
        }
        else if (clawhud::ShouldInvalidateWindowsUsageTelemetry(
            ++usageTelemetryFailureCount_, kUsageUnavailableFailureThreshold))
        {
            latestUsageTelemetry_.reset();
            usageSampler_.Reset();
            usageTelemetryFailureCount_ = 0;
            usageCpuMissingCount_ = 0;
            usageMemoryMissingCount_ = 0;
            usageGpuMemoryMissingCount_ = 0;
        }
    }

    if (!igclGpuSampler_.Initialized() &&
        !igclGpuSampler_.InitializationAttempted())
    {
        if (!igclGpuSampler_.Initialize())
        {
            latestIgclGpuTelemetry_.reset();
            igclGpuUsageMissingCount_ = 0;
            igclGpuClockMissingCount_ = 0;
            igclInitializationFailureCount_ = 0;
        }
        else
        {
            igclInitializationFailureCount_ = 0;
            igclTelemetryAvailable_ = true;
        }
    }
    else if (!igclGpuSampler_.Initialized() &&
        igclGpuSampler_.InitializationAttempted() &&
        ++igclInitializationFailureCount_ >= kIgclUnavailableFailureThreshold)
    {
        igclGpuSampler_.Reset();
        igclInitializationFailureCount_ = 0;
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info,
            L"IGCL initialization retry re-armed");
    }
    if (igclGpuSampler_.Initialized())
    {
        const auto sample = igclGpuSampler_.Sample();
        if (sample)
        {
            latestIgclGpuTelemetry_ = clawhud::MergeIgclGpuTelemetry(
                latestIgclGpuTelemetry_, sample);
            clawhud::UpdateRetainedTelemetryField(
                latestIgclGpuTelemetry_->gpuUsagePercent,
                sample->gpuUsagePercent, igclGpuUsageMissingCount_,
                kIgclUnavailableFailureThreshold);
            clawhud::UpdateRetainedTelemetryField(
                latestIgclGpuTelemetry_->gpuClockMHz,
                sample->gpuClockMHz, igclGpuClockMissingCount_,
                kIgclUnavailableFailureThreshold);
            igclTelemetryFailureCount_ = 0;
        }
        else
        {
            if (latestIgclGpuTelemetry_)
            {
                const std::optional<double> missingIgclField;
                clawhud::UpdateRetainedTelemetryField(
                    latestIgclGpuTelemetry_->gpuUsagePercent, missingIgclField,
                    igclGpuUsageMissingCount_, kIgclUnavailableFailureThreshold);
                clawhud::UpdateRetainedTelemetryField(
                    latestIgclGpuTelemetry_->gpuClockMHz, missingIgclField,
                    igclGpuClockMissingCount_, kIgclUnavailableFailureThreshold);
            }
            ++igclTelemetryFailureCount_;
        }
        const auto transition = clawhud::ObserveIgclTelemetryTransition(
            igclTelemetryAvailable_, igclTelemetryFailureCount_,
            sample.has_value(), kIgclUnavailableFailureThreshold);
        if (transition == clawhud::IgclTelemetryTransition::Recovered)
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info,
                L"IGCL telemetry recovered");
        else if (transition == clawhud::IgclTelemetryTransition::Unavailable)
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"IGCL telemetry unavailable");
        if (sample)
            igclTelemetryAvailable_ = true;
        else if (clawhud::ShouldResetIgclProvider(
            igclTelemetryFailureCount_, kIgclUnavailableFailureThreshold))
        {
            igclTelemetryAvailable_ = false;
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"IGCL telemetry provider failed repeatedly; resetting provider");
            igclGpuSampler_.Reset();
            igclTelemetryFailureCount_ = 0;
        }
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
    StopGlobalRendererTelemetry();
    StopGraphicsApiProbe();
    if (ecHudClient_)
    {
        ecHudClient_->Close();
        ecHudClient_.reset();
    }
    ecHudTelemetry_ = {};
    ecCpuTempMissingCount_ = 0;
    ecFan1MissingCount_ = 0;
    ecFan2MissingCount_ = 0;
    ecTdpMissingCount_ = 0;
    latestPowerTelemetry_.reset();
    latestUsageTelemetry_.reset();
    latestPresentMonDisplayedFps_.reset();
    usageSampler_.Reset();
    usageTelemetryFailureCount_ = 0;
    usageCpuMissingCount_ = 0;
    usageMemoryMissingCount_ = 0;
    usageGpuMemoryMissingCount_ = 0;
    latestIgclGpuTelemetry_.reset();
    igclGpuSampler_.Reset();
    igclGpuUsageMissingCount_ = 0;
    igclGpuClockMissingCount_ = 0;
    igclTelemetryAvailable_ = false;
    igclTelemetryFailureCount_ = 0;
    igclInitializationFailureCount_ = 0;
    ecHudSamplingActive_ = false;
    if (wasActive)
        Log(L"Production telemetry sampling stopped reason=suspend");
}

void App::ReleaseCommittedProductionTarget(const wchar_t* reason)
{
    const auto& context = gameDetectionCoordinator_.Context();
    const DWORD processId = context.candidateProcessId;
    const auto generation = context.generation;
    if (!processId)
        return;
    const auto release = clawhud::PlanCommittedTargetRelease();
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
    ClearProductionCandidate(L"game-exited");
    clawhud::ApplyCommittedTargetReleasePlan(release, ops);
    Log(L"[GameDetection] released pid=" + std::to_wstring(processId) +
        L" gen=" + std::to_wstring(generation) + L" reason=" + reason);
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
    ecCpuTempMissingCount_ = 0;
    ecFan1MissingCount_ = 0;
    ecFan2MissingCount_ = 0;
    ecTdpMissingCount_ = 0;
    latestPowerTelemetry_.reset();
    latestUsageTelemetry_.reset();
    usageSampler_.Reset();
    usageTelemetryFailureCount_ = 0;
    usageCpuMissingCount_ = 0;
    usageMemoryMissingCount_ = 0;
    usageGpuMemoryMissingCount_ = 0;
    latestIgclGpuTelemetry_.reset();
    igclGpuSampler_.Reset();
    igclGpuUsageMissingCount_ = 0;
    igclGpuClockMissingCount_ = 0;
    igclTelemetryAvailable_ = false;
    igclTelemetryFailureCount_ = 0;
    igclInitializationFailureCount_ = 0;
    ecHudSamplingActive_ = false;
    if (wasActive)
        Log(L"Production telemetry sampling stopped reason=" + std::wstring(reason));
}

void App::StartProductionPresentMonSampling(bool recoveryStart)
{
    if (suspended_ || DiagnosticRunning() || !mockHudEnabled_)
        return;
    const auto& context = gameDetectionCoordinator_.Context();
    const DWORD processId = context.candidateProcessId;
    const auto generation = context.generation;
    const bool committed = context.state == clawhud::GameDetectionState::Committed;
    if (!processId)
        return;
    if (!ProcessAlive(processId))
    {
        if (committed)
            ReleaseCommittedProductionTarget(L"game-exited");
        else
            ReleaseProductionGameCandidate(L"game-exited");
        return;
    }
    if (gameRenderVerifier_.Running() &&
        gameRenderVerifier_.ProcessId() == processId &&
        gameRenderVerifier_.Generation() == generation)
        return;
    if (committed && !gameRenderVerifier_.Running() &&
        !clawhud::ShouldAllowProductionPresentMonStart(
            processId, processId, presentMonRestartPid_,
            presentMonRestartAttempts_, recoveryStart))
        return;

    StopProductionPresentMonSampling(L"target-handoff", false);
    const auto executable = std::filesystem::path(executablePath_).parent_path() /
        L"tools" / L"PresentMon.exe";
    Log(L"[GameDetection] verifier.start pid=" + std::to_wstring(processId) +
        L" gen=" + std::to_wstring(generation));
    const bool started = gameRenderVerifier_.Start(executable.wstring(), processId,
        generation, [this](const clawhud::GameRenderVerifierEvent& event)
        {
            auto* update = new GameRenderVerifierUpdate{event};
            if (!PostMessageW(tray_.Window(), kGameRenderVerifierUpdate,
                reinterpret_cast<WPARAM>(update), 0))
                delete update;
        });
    if (started)
        Log(L"[GameDetection] verifier.started pid=" + std::to_wstring(processId) +
            L" gen=" + std::to_wstring(generation));
    else
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"[GameDetection] verifier.start-failed pid=" +
            std::to_wstring(processId) + L" gen=" + std::to_wstring(generation));
        if (!committed)
            ReleaseProductionGameCandidate(L"verifier-start-failed");
        else
        {
            if (presentMonRestartPid_ != processId)
            {
                presentMonRestartPid_ = processId;
                presentMonRestartAttempts_ = 0;
            }
            else if (presentMonRestartAttempts_ < 1)
                ++presentMonRestartAttempts_;
            if (presentMonRestartAttempts_ >= 1)
                clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                    L"[GameDetection] verifier.recovery-exhausted pid=" +
                    std::to_wstring(processId) + L" gen=" +
                    std::to_wstring(generation));
        }
    }
}

void App::StopProductionPresentMonSampling(const wchar_t* reason, bool clearLatestFps)
{
    if (gameRenderVerifier_.ProcessId())
    {
        const DWORD processId = gameRenderVerifier_.ProcessId();
        Log(L"[GameDetection] verifier.stop pid=" +
            std::to_wstring(processId) + L" reason=" + reason);
        const DWORD exitCode = gameRenderVerifier_.Stop();
        Log(L"[GameDetection] verifier.stopped pid=" +
            std::to_wstring(processId) + L" exitCode=" + std::to_wstring(exitCode));
    }
    if (clearLatestFps)
        latestPresentMonDisplayedFps_.reset();
}

void App::StartGlobalRendererTelemetry()
{
    if (!clawhud::GlobalRendererTelemetryStartAllowed(
        globalRendererTelemetryUnavailable_, suspended_, DiagnosticRunning(),
        mockHudEnabled_, globalPresentMonTelemetry_.Running()))
        return;
    const auto executable = std::filesystem::path(executablePath_).parent_path() /
        L"tools" / L"PresentMon.exe";
    if (!globalPresentMonTelemetry_.Start(executable.wstring(),
        [this](const clawhud::GlobalPresentMonEvent& event)
        {
            if (event.type == clawhud::GlobalPresentMonEvent::Type::StreamEnded)
            {
                globalRendererStreamEnded_ = true;
                QueueGlobalRendererTelemetryUpdate(true);
                return;
            }
            {
                std::scoped_lock lock(rendererTargetMutex_);
                rendererTargetSelector_.ObserveFrame(event.frame);
            }
            QueueGlobalRendererTelemetryUpdate(false);
        }))
    {
        globalRendererTelemetryUnavailable_ = true;
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"[RendererTelemetry] global.start-failed; legacy detection remains active");
        return;
    }
    Log(L"[RendererTelemetry] global.started session=" +
        globalPresentMonTelemetry_.SessionName());
}

void App::StopGlobalRendererTelemetry()
{
    if (!globalPresentMonTelemetry_.Running() &&
        globalPresentMonTelemetry_.SessionName().empty())
        return;
    const DWORD exitCode = globalPresentMonTelemetry_.Stop();
    Log(L"[RendererTelemetry] global.stopped exitCode=" +
        std::to_wstring(exitCode));
    {
        std::scoped_lock lock(rendererTargetMutex_);
        rendererTargetSelector_.Clear();
    }
    selectedRendererFps_.reset();
}

void App::QueueGlobalRendererTelemetryUpdate(bool force)
{
    constexpr std::uint64_t kMinimumNotificationIntervalMs = 100;
    const auto now = GetTickCount64();
    const auto last = lastGlobalRendererUiUpdateTick_.load();
    if (!force && now >= last && now - last < kMinimumNotificationIntervalMs)
        return;
    if (globalRendererUiUpdatePending_.exchange(true))
        return;
    lastGlobalRendererUiUpdateTick_ = now;
    if (!PostMessageW(tray_.Window(), kGlobalRendererTelemetryUpdate, 0, 0))
        globalRendererUiUpdatePending_ = false;
}

void App::RefreshRendererHints()
{
    const auto& context = gameDetectionCoordinator_.Context();
    const DWORD candidate = context.candidateProcessId;
    std::scoped_lock lock(rendererTargetMutex_);
    rendererTargetSelector_.SetMicrosoftHint(
        context.evidence.microsoftGameIdentity && candidate
            ? std::optional<DWORD>(candidate) : std::nullopt);
    rendererTargetSelector_.SetSteamHint(
        context.evidence.steamSession && candidate
            ? std::optional<DWORD>(candidate) : std::nullopt);
    selectedRendererFps_ = rendererTargetSelector_.Selection()
        ? rendererTargetSelector_.Selection()->fps : std::nullopt;
}

void App::HandleGlobalRendererTelemetry()
{
    globalRendererUiUpdatePending_ = false;
    if (suspended_ || DiagnosticRunning() || !mockHudEnabled_)
        return;
    std::optional<clawhud::RendererTargetSelection> current;
    const bool streamEnded = globalRendererStreamEnded_.exchange(false);
    {
        std::scoped_lock lock(rendererTargetMutex_);
        if (streamEnded)
            rendererTargetSelector_.Clear();
        else
            rendererTargetSelector_.Reevaluate(GetTickCount64());
        current = rendererTargetSelector_.Selection();
    }
    if (!current && !lastReportedRendererSelection_ &&
        globalPresentMonTelemetry_.SessionName().empty())
        return;
    if (streamEnded)
    {
        globalRendererTelemetryUnavailable_ = true;
        StopGlobalRendererTelemetry();
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"[RendererTelemetry] global.stream-ended; legacy detection remains active");
    }
    selectedRendererFps_ = current ? current->fps : std::nullopt;
    const auto previous = lastReportedRendererSelection_;
    if (clawhud::RendererTargetSelectionIdentityChanged(previous, current))
    {
        if (current)
            Log(L"[RendererSelector] selected pid=" +
                std::to_wstring(current->processId) + L" app=" +
                current->application + L" fps=" +
                (current->fps ? std::to_wstring(*current->fps) : L"pending") +
                L" reason=" + clawhud::RendererSelectionReasonName(current->reason));
        else
            Log(L"[RendererSelector] cleared");
    }
    lastReportedRendererSelection_ = current;
    ReconcileHudVisibility();
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

void App::HandleGameRenderVerifierEvent(
    const clawhud::GameRenderVerifierEvent& event)
{
    const auto& context = gameDetectionCoordinator_.Context();
    if (suspended_ || resumeRecoveryActive_ || DiagnosticRunning() ||
        event.processId != context.candidateProcessId ||
        event.generation != context.generation)
        return;
    if (event.event.type == clawhud::PresentMonHudEventType::FirstDisplayedFrame)
    {
        if (clawhud::GameRenderVerifier::ApplyRendererEvidence(
            gameDetectionCoordinator_, event))
        {
            Log(L"[GameDetection] renderer.first-frame pid=" +
                std::to_wstring(event.processId) + L" gen=" +
                std::to_wstring(event.generation));
            HandleGameDetectionTransition({
                clawhud::GameDetectionTransition::RendererReady,
                event.generation, event.processId});
            HWND foreground = GetForegroundWindow();
            DWORD foregroundProcessId{};
            if (foreground)
                GetWindowThreadProcessId(foreground, &foregroundProcessId);
            if (!TryCommitReadyCandidateFromForeground(foreground, foregroundProcessId))
                Log(L"[GameDetection] ready.waiting-foreground pid=" +
                    std::to_wstring(event.processId) + L" gen=" +
                    std::to_wstring(event.generation) + L" foregroundPid=" +
                    std::to_wstring(foregroundProcessId));
        }
        return;
    }
    if (event.event.type == clawhud::PresentMonHudEventType::FpsUpdate)
    {
        latestPresentMonDisplayedFps_ = event.event.displayedFps;
        if (event.event.displayedFps)
        {
            presentMonRestartPid_ = event.processId;
            presentMonRestartAttempts_ = 0;
        }
        RenderProductionHud();
        return;
    }

    if (event.event.type != clawhud::PresentMonHudEventType::StreamEnded)
        return;
    const bool alive = ProcessAlive(event.processId);
    Log(L"[GameDetection] verifier.ended pid=" + std::to_wstring(event.processId) +
        L" gen=" + std::to_wstring(event.generation) +
        L" alive=" + std::to_wstring(alive ? 1 : 0));
    if (!alive)
    {
        if (context.state == clawhud::GameDetectionState::Committed)
            ReleaseCommittedProductionTarget(L"game-exited");
        else
            ReleaseProductionGameCandidate(L"game-exited");
        if (mockHudEnabled_ && !DiagnosticRunning() && !suspended_)
            ReevaluateProductionGameDetection();
        return;
    }

    StopProductionPresentMonSampling(L"unexpected-exit", false);
    if (presentMonRestartPid_ != event.processId)
    {
        presentMonRestartPid_ = event.processId;
        presentMonRestartAttempts_ = 0;
    }
    if (clawhud::ShouldRetryProductionPresentMon(
        presentMonRestartPid_, presentMonRestartAttempts_, event.processId))
    {
        ++presentMonRestartAttempts_;
        Log(L"[GameDetection] verifier.retry pid=" +
            std::to_wstring(event.processId) + L" gen=" +
            std::to_wstring(event.generation) + L" attempt=" +
            std::to_wstring(presentMonRestartAttempts_));
        StartProductionPresentMonSampling(true);
    }
    else
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"[GameDetection] verifier.recovery-exhausted pid=" +
            std::to_wstring(event.processId) + L" gen=" +
            std::to_wstring(event.generation));
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
    igclInitializationFailureCount_ = 0;
    StartGraphicsApiProbe(processId);
    if (EnsureMockHud())
    {
        mockHudEnabled_ = true;
        ReconcileHudVisibility();
    }
}

void App::ReevaluateProductionGameDetection()
{
    HWND foreground = GetForegroundWindow();
    DWORD processId{};
    if (foreground)
        GetWindowThreadProcessId(foreground, &processId);
    HandleProductionForegroundChanged(foreground, processId);
}

void App::HandleProductionForegroundChanged(HWND window, DWORD processId)
{
    if (!clawhud::ShouldConsiderForegroundProductionTarget(
        mockHudEnabled_, DiagnosticRunning(), suspended_))
        return;
    if (TryCommitReadyCandidateFromForeground(window, processId))
        return;
    const auto& context = gameDetectionCoordinator_.Context();
    if (context.state == clawhud::GameDetectionState::Committed)
    {
        if (ProcessAlive(context.candidateProcessId))
        {
            if (graphicsApiProcessId_ != context.candidateProcessId)
                StartGraphicsApiProbe(context.candidateProcessId);
            StartProductionPresentMonSampling();
            return;
        }
        ReleaseCommittedProductionTarget(L"game-exited");
    }
    if (context.state == clawhud::GameDetectionState::Ready)
        return;
    const auto evidence = genericForegroundTrigger_.Inspect(window, processId);
    if (evidence)
        ApplyProductionEvidence(clawhud::GameDetectionTrigger::GenericForeground,
            evidence->window, evidence->processId);
}

void App::HandleMicrosoftGameEvidence(
    const clawhud::MicrosoftGameTriggerEvidence& evidence)
{
    {
        std::scoped_lock lock(rendererTargetMutex_);
        rendererTargetSelector_.SetMicrosoftHint(evidence.processId);
        selectedRendererFps_ = rendererTargetSelector_.Selection()
            ? rendererTargetSelector_.Selection()->fps : std::nullopt;
    }
    if (!clawhud::ShouldConsiderForegroundProductionTarget(
        mockHudEnabled_, DiagnosticRunning(), suspended_))
        return;
    if (!ProcessAlive(evidence.processId))
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"[GameDetection] microsoft.stale-evidence pid=" +
            std::to_wstring(evidence.processId));
        return;
    }
    ApplyProductionEvidence(clawhud::GameDetectionTrigger::MicrosoftGameIdentity,
        evidence.window, evidence.processId);
}

void App::HandleProductionWindowEvent(
    const clawhud::ProductionWindowEvent& event)
{
    if (event.type != clawhud::ProductionWindowEventType::Create &&
        event.type != clawhud::ProductionWindowEventType::Show)
        return;
    if (!clawhud::ShouldConsiderForegroundProductionTarget(
        mockHudEnabled_, DiagnosticRunning(), suspended_))
        return;

    const auto& context = gameDetectionCoordinator_.Context();
    if (context.state != clawhud::GameDetectionState::Armed ||
        context.steamAppId == 0 || !event.immediateTopLevel ||
        event.processId == 0 || event.processId == GetCurrentProcessId())
        return;
    if (!clawhud::InspectProductionTargetProcess(event.processId) ||
        !ProcessAlive(event.processId))
        return;

    const DWORD previousProcessId = context.candidateProcessId;
    const auto previousGeneration = context.generation;
    const auto transition = gameDetectionCoordinator_.ObserveWake({
        clawhud::GameDetectionTrigger::SteamRunningAppId,
        event.processId, event.window, context.steamAppId, false});
    if (transition.transition == clawhud::GameDetectionTransition::None)
        return;

    const wchar_t* eventName = event.type ==
        clawhud::ProductionWindowEventType::Create ? L"Create" : L"Show";
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
        L"[GameDetection] steam.window-candidate pid=" +
        std::to_wstring(event.processId) + L" hwnd=" + HwndText(event.window) +
        L" appId=" + std::to_wstring(context.steamAppId) +
        L" event=" + eventName);
    HandleGameDetectionTransition(transition,
        clawhud::GameDetectionTrigger::SteamRunningAppId,
        previousProcessId, previousGeneration);
}

void App::ApplyProductionEvidence(clawhud::GameDetectionTrigger trigger,
    HWND window, DWORD processId)
{
    const DWORD previousProcessId =
        gameDetectionCoordinator_.Context().candidateProcessId;
    const auto previousGeneration = gameDetectionCoordinator_.Context().generation;
    const auto disposition = clawhud::DecideCandidateDisposition(
        gameDetectionCoordinator_.Context(), trigger, processId);
    if (disposition == clawhud::CandidateDisposition::Ignore)
        return;
    clawhud::GameDetectionTransitionResult transition;
    if (disposition == clawhud::CandidateDisposition::Replace)
        transition = gameDetectionCoordinator_.ReplaceCandidate(processId, window, trigger);
    else if (trigger == clawhud::GameDetectionTrigger::MicrosoftGameIdentity)
        transition = microsoftGameTrigger_.ApplyEvidence(
            gameDetectionCoordinator_, {0, window, processId});
    else
        transition = genericForegroundTrigger_.ApplyEvidence(
            gameDetectionCoordinator_, {window, processId});
    if (trigger == clawhud::GameDetectionTrigger::GenericForeground)
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"[GameDetection] generic.candidate pid=" +
            std::to_wstring(processId) + L" hwnd=" + HwndText(window));
    HandleGameDetectionTransition(
        transition, trigger, previousProcessId, previousGeneration);
}

void App::HandleGameDetectionTransition(
    const clawhud::GameDetectionTransitionResult& transition,
    clawhud::GameDetectionTrigger trigger,
    DWORD previousProcessId, std::uint64_t previousGeneration)
{
    RefreshRendererHints();
    switch (transition.transition)
    {
    case clawhud::GameDetectionTransition::Armed:
        Log(L"[GameDetection] transition old=Idle new=Armed "
            L"transition=Armed trigger=Steam appId=" +
            std::to_wstring(gameDetectionCoordinator_.Context().steamAppId));
        Log(L"[GameDetection] steam.armed appId=" +
            std::to_wstring(gameDetectionCoordinator_.Context().steamAppId));
        break;
    case clawhud::GameDetectionTransition::CandidateStarted:
        ArmProductionProcessLifetime(transition.processId,
            transition.generation);
        latestPresentMonDisplayedFps_.reset();
        presentMonRestartPid_ = 0;
        presentMonRestartAttempts_ = 0;
        Log(L"[GameDetection] candidate.start trigger=" +
            std::wstring(clawhud::GameDetectionTriggerName(trigger)) +
            L" pid=" + std::to_wstring(transition.processId) + L" gen=" +
            std::to_wstring(transition.generation) + L" steamAppId=" +
            std::to_wstring(gameDetectionCoordinator_.Context().steamAppId) +
            L" microsoft=" + std::to_wstring(
                gameDetectionCoordinator_.Context().evidence.microsoftGameIdentity ? 1 : 0) +
            L" generic=" + std::to_wstring(
                gameDetectionCoordinator_.Context().evidence.genericForeground ? 1 : 0) +
            L" hwnd=" + HwndText(
                gameDetectionCoordinator_.Context().candidateWindow));
        Log(L"[GameDetection] transition old=" +
            std::wstring(gameDetectionCoordinator_.Context().steamAppId != 0
                ? L"Armed" : L"Idle") + L" new=Verifying "
            L"transition=CandidateStarted pid=" +
            std::to_wstring(transition.processId) + L" gen=" +
            std::to_wstring(transition.generation));
        StartCandidateRenderVerification();
        break;
    case clawhud::GameDetectionTransition::CandidateUpdated:
    {
        const auto& context = gameDetectionCoordinator_.Context();
        const std::wstring state(clawhud::GameDetectionStateName(context.state));
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"[GameDetection] candidate.merge trigger=" +
            std::wstring(clawhud::GameDetectionTriggerName(trigger)) +
            L" pid=" + std::to_wstring(transition.processId) + L" gen=" +
            std::to_wstring(transition.generation) + L" generic=" +
            std::to_wstring(context.evidence.genericForeground ? 1 : 0) +
            L" microsoft=" + std::to_wstring(
                context.evidence.microsoftGameIdentity ? 1 : 0));
        Log(L"[GameDetection] transition old=" + state + L" new=" + state +
            L" "
            L"transition=CandidateUpdated pid=" +
            std::to_wstring(transition.processId) + L" gen=" +
            std::to_wstring(transition.generation));
        break;
    }
    case clawhud::GameDetectionTransition::CandidateReplaced:
        ArmProductionProcessLifetime(transition.processId,
            transition.generation);
        StopProductionPresentMonSampling(L"candidate-replaced", true);
        StopGraphicsApiProbe();
        presentMonRestartPid_ = 0;
        presentMonRestartAttempts_ = 0;
        Log(L"[GameDetection] candidate.replace oldPid=" +
            std::to_wstring(previousProcessId) + L" newPid=" +
            std::to_wstring(transition.processId) + L" trigger=" +
            std::wstring(clawhud::GameDetectionTriggerName(trigger)) +
            L" oldGen=" + std::to_wstring(previousGeneration) +
            L" newGen=" + std::to_wstring(transition.generation));
        Log(L"[GameDetection] transition old=Verifying new=Verifying "
            L"transition=CandidateReplaced pid=" +
            std::to_wstring(transition.processId) + L" gen=" +
            std::to_wstring(transition.generation));
        StartCandidateRenderVerification();
        break;
    case clawhud::GameDetectionTransition::RendererReady:
        Log(L"[GameDetection] transition old=Verifying new=Ready "
            L"transition=RendererReady pid=" +
            std::to_wstring(transition.processId) + L" gen=" +
            std::to_wstring(transition.generation));
        break;
    case clawhud::GameDetectionTransition::Committed:
        Log(L"[GameDetection] transition old=Ready new=Committed "
            L"transition=Committed pid=" +
            std::to_wstring(transition.processId) + L" gen=" +
            std::to_wstring(transition.generation));
        Log(L"[GameDetection] committed pid=" +
            std::to_wstring(transition.processId) + L" gen=" +
            std::to_wstring(transition.generation) + L" foregroundPid=" +
            std::to_wstring(transition.processId) + L" steam=" +
            std::to_wstring(gameDetectionCoordinator_.Context().evidence.steamSession ? 1 : 0) +
            L" generic=" + std::to_wstring(
                gameDetectionCoordinator_.Context().evidence.genericForeground ? 1 : 0) +
            L" microsoft=" + std::to_wstring(
                gameDetectionCoordinator_.Context().evidence.microsoftGameIdentity ? 1 : 0) +
            L" renderer=" + std::to_wstring(
                gameDetectionCoordinator_.Context().rendererObserved ? 1 : 0));
        break;
    case clawhud::GameDetectionTransition::CandidateCleared:
        productionProcessLifetimeWatcher_.Disarm();
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"[GameDetection] transition=CandidateCleared");
        break;
    case clawhud::GameDetectionTransition::Reset:
        productionProcessLifetimeWatcher_.Disarm();
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"[GameDetection] reset");
        break;
    case clawhud::GameDetectionTransition::None:
        break;
    }
}

void App::StartCandidateRenderVerification()
{
    StartProductionPresentMonSampling();
}

void App::ArmProductionProcessLifetime(DWORD processId,
    std::uint64_t generation)
{
    const HWND messageWindow = tray_.Window();
    if (!productionProcessLifetimeWatcher_.Arm(processId, generation,
        [messageWindow](DWORD exitedProcessId, std::uint64_t exitedGeneration)
        {
            auto* update = new ProductionProcessExitUpdate{
                exitedProcessId, exitedGeneration};
            if (!PostMessageW(messageWindow, kProductionProcessExit,
                reinterpret_cast<WPARAM>(update), 0))
                delete update;
        }))
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"[GameDetection] process-watch.arm-failed pid=" +
            std::to_wstring(processId) + L" gen=" +
            std::to_wstring(generation));
        return;
    }
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
        L"[GameDetection] process-watch.arm pid=" +
        std::to_wstring(processId) + L" gen=" +
        std::to_wstring(generation));
}

void App::HandleProductionProcessExit(DWORD processId,
    std::uint64_t generation)
{
    const auto& context = gameDetectionCoordinator_.Context();
    if (context.candidateProcessId != processId ||
        context.generation != generation)
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
            L"[GameDetection] process.exit-stale pid=" +
            std::to_wstring(processId) + L" gen=" +
            std::to_wstring(generation));
        return;
    }

    const auto action = clawhud::DecideProductionProcessExit(
        context, processId, generation);
    if (action == clawhud::ProductionProcessExitAction::Ignore)
        return;
    const auto state = std::wstring(
        clawhud::GameDetectionStateName(context.state));
    Log(L"[GameDetection] process.exit pid=" + std::to_wstring(processId) +
        L" gen=" + std::to_wstring(generation) + L" state=" + state);
    if (action == clawhud::ProductionProcessExitAction::ReleaseCommitted)
        ReleaseCommittedProductionTarget(L"game-exited");
    else
        ReleaseProductionGameCandidate(L"process-exited");
    if (mockHudEnabled_ && !DiagnosticRunning() && !suspended_)
        ReevaluateProductionGameDetection();
}

bool App::TryCommitReadyCandidateFromForeground(HWND,
    DWORD foregroundProcessId)
{
    const auto& context = gameDetectionCoordinator_.Context();
    if (!clawhud::ShouldCommitReadyCandidate(
            context, foregroundProcessId, ProcessAlive(context.candidateProcessId)) ||
        !mockHudEnabled_ || DiagnosticRunning() || suspended_)
        return false;
    const DWORD processId = context.candidateProcessId;
    const auto generation = context.generation;
    if (!gameDetectionCoordinator_.CommitCandidate(processId, generation))
        return false;
    foregroundTracker_.SetTrackedProcessId(processId);
    presentMonRestartPid_ = processId;
    presentMonRestartAttempts_ = 0;
    StartGraphicsApiProbe(processId);
    StartProductionEcSampling();
    HandleGameDetectionTransition({
        clawhud::GameDetectionTransition::Committed, generation, processId});
    ReconcileHudVisibility();
    return true;
}

void App::ReleaseProductionGameCandidate(const wchar_t* reason)
{
    const auto& context = gameDetectionCoordinator_.Context();
    if (!context.candidateProcessId)
        return;
    StopProductionPresentMonSampling(reason, true);
    if (graphicsApiProcessId_ == context.candidateProcessId)
        StopGraphicsApiProbe();
    presentMonRestartPid_ = 0;
    presentMonRestartAttempts_ = 0;
    ClearProductionCandidate(reason);
}

void App::ClearProductionCandidate(const wchar_t* reason)
{
    const auto& context = gameDetectionCoordinator_.Context();
    const DWORD processId = context.candidateProcessId;
    const auto generation = context.generation;
    const auto transition = gameDetectionCoordinator_.ClearCandidatePreservingSession();
    HandleGameDetectionTransition(transition);
    if (processId)
        Log(L"[GameDetection] candidate.clear pid=" + std::to_wstring(processId) +
            L" gen=" + std::to_wstring(generation) + L" reason=" + reason);
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
        globalRendererTelemetryUnavailable_ = false;
        mockFrameIndex_ = 0;
        StartGlobalRendererTelemetry();
        ReevaluateProductionGameDetection();
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
    StopGlobalRendererTelemetry();
    if (gameDetectionCoordinator_.Context().state != clawhud::GameDetectionState::Committed)
        ClearProductionCandidate(L"diagnostic-start");
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
    StopGlobalRendererTelemetry();
    if (gameDetectionCoordinator_.Context().state != clawhud::GameDetectionState::Committed)
        ClearProductionCandidate(L"diagnostic-start");
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
        globalRendererTelemetryUnavailable_ = false;
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

void App::DiscardPendingGameRenderVerifierEvents()
{
    MSG message{};
    while (PeekMessageW(&message, tray_.Window(), kGameRenderVerifierUpdate,
        kGameRenderVerifierUpdate, PM_REMOVE))
        delete reinterpret_cast<GameRenderVerifierUpdate*>(message.wParam);
}

void App::DiscardPendingGlobalRendererTelemetry()
{
    MSG message{};
    while (PeekMessageW(&message, tray_.Window(),
        kGlobalRendererTelemetryUpdate, kGlobalRendererTelemetryUpdate, PM_REMOVE))
        (void)message;
    globalRendererUiUpdatePending_ = false;
    globalRendererStreamEnded_ = false;
}

void App::DiscardPendingMicrosoftGameEvidence()
{
    MSG message{};
    while (PeekMessageW(&message, tray_.Window(), kMicrosoftGameEvidence,
        kMicrosoftGameEvidence, PM_REMOVE))
        delete reinterpret_cast<MicrosoftGameEvidenceUpdate*>(message.wParam);
}

void App::DiscardPendingProductionWindowEvents()
{
    MSG message{};
    while (PeekMessageW(&message, tray_.Window(), kProductionWindowEvent,
        kProductionWindowEvent, PM_REMOVE))
        delete reinterpret_cast<ProductionWindowEventUpdate*>(message.wParam);
}

void App::DiscardPendingProductionProcessExitEvents()
{
    MSG message{};
    while (PeekMessageW(&message, tray_.Window(), kProductionProcessExit,
        kProductionProcessExit, PM_REMOVE))
        delete reinterpret_cast<ProductionProcessExitUpdate*>(message.wParam);
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
    if (gameDetectionCoordinator_.Context().state == clawhud::GameDetectionState::Committed &&
        gameDetectionCoordinator_.Context().candidateProcessId &&
        !ProcessAlive(gameDetectionCoordinator_.Context().candidateProcessId))
        ReleaseCommittedProductionTarget(L"game-exited");
    if (graphicsApiProcessId_ && !ProcessAlive(graphicsApiProcessId_))
        StopGraphicsApiProbe();
    if (mockHudEnabled_ && !DiagnosticRunning())
        StartGlobalRendererTelemetry();
    bool rendererForegroundActive = false;
    {
        std::scoped_lock lock(rendererTargetMutex_);
        rendererTargetSelector_.Reevaluate(GetTickCount64());
        selectedRendererFps_ = rendererTargetSelector_.Selection()
            ? rendererTargetSelector_.Selection()->fps : std::nullopt;
        rendererForegroundActive = rendererTargetSelector_.
            ForegroundHasActiveRenderer(GetTickCount64());
    }
    const bool legacyForegroundActive = foregroundTracker_.ForegroundIsTrackedProcess();
    const bool resolvedShow = mockHudEnabled_ && (manualHudVisibilityOverride_.has_value()
        ? *manualHudVisibilityOverride_
        : hudOptions_.visibilityMode == clawhud::HudVisibilityMode::Always ||
            rendererForegroundActive || legacyForegroundActive);
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
                SetTimer(tray_.Window(), kMockHudTimerId,
                    kDiagnosticMockHudTimerIntervalMs, nullptr);
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
    StopProductionEcSampling(false, L"app-shutdown");
    StopGraphicsApiProbe();
    productionGameWindowSource_.Stop();
    productionProcessLifetimeWatcher_.Disarm();
    foregroundTracker_.Stop();
    windowLifecycleSource_.Stop();
    presentActivitySource_.Stop();
    processLifecycleSource_.Stop();
    if (vrrDiagnostic_) vrrDiagnostic_->Stop();
    if (ecDiagnostic_) ecDiagnostic_->Stop();
    if (igclDiagnostic_) igclDiagnostic_->Stop();
    StopProductionPresentMonSampling(L"app-shutdown", true);
    StopGlobalRendererTelemetry();
    DiscardPendingGlobalRendererTelemetry();
    DiscardPendingHudVisibilityRequests();
    DiscardPendingProductionWindowEvents();
    DiscardPendingProductionProcessExitEvents();
    DiscardPendingMicrosoftGameEvidence();
    DiscardPendingGameRenderVerifierEvents();
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
        if (message.message == kGameRenderVerifierUpdate)
        {
            auto* update = reinterpret_cast<GameRenderVerifierUpdate*>(message.wParam);
            if (update)
            {
                HandleGameRenderVerifierEvent(update->event);
                delete update;
            }
            continue;
        }
        if (message.message == kGlobalRendererTelemetryUpdate)
        {
            HandleGlobalRendererTelemetry();
            continue;
        }
        if (message.message == kProductionWindowEvent)
        {
            auto* update = reinterpret_cast<ProductionWindowEventUpdate*>(message.wParam);
            if (update)
            {
                HandleProductionWindowEvent(update->event);
                delete update;
            }
            continue;
        }
        if (message.message == kProductionProcessExit)
        {
            auto* update = reinterpret_cast<ProductionProcessExitUpdate*>(message.wParam);
            if (update)
            {
                HandleProductionProcessExit(update->processId,
                    update->generation);
                delete update;
            }
            continue;
        }
        if (message.message == kMicrosoftGameEvidence)
        {
            auto* update = reinterpret_cast<MicrosoftGameEvidenceUpdate*>(message.wParam);
            if (update)
            {
                HandleMicrosoftGameEvidence(update->evidence);
                delete update;
            }
            continue;
        }
        if (message.message == kSteamRunningAppIdChanged)
        {
            const auto current = steamRunningAppIdSource_.GetRunningAppId();
            if (RunningAppIdChanged(steamRunningAppId_, current))
            {
                const auto previous = steamRunningAppId_;
                steamRunningAppId_ = current;
                Log(L"[GameDetection] steam.session oldAppId=" +
                    std::to_wstring(previous) + L" newAppId=" +
                    std::to_wstring(current));
                HandleGameDetectionTransition(
                    steamRunningAppTrigger_.ObserveChange(previous, current));
                if (current == 0)
                {
                    const auto& context = gameDetectionCoordinator_.Context();
                    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
                        L"[GameDetection] steam.session-cleared appId=" +
                        std::to_wstring(previous) + L" candidatePid=" +
                        std::to_wstring(context.candidateProcessId) + L" state=" +
                        std::wstring(clawhud::GameDetectionStateName(context.state)));
                }
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
        if (message.message == clawhud::kPresentMonApi2DiagnosticStatus)
        {
            auto* status = reinterpret_cast<std::wstring*>(message.wParam);
            if (status)
            {
                presentMonApi2Status_ = *status;
                if (settings_) settings_->SetDiagnosticStatus(*status);
            }
            delete status;
            continue;
        }
        if (message.message == clawhud::kPresentMonApi2DiagnosticCompleted)
        {
            StopPresentMonApi2Diagnostic();
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
                    ReevaluateProductionGameDetection();
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
                ReevaluateProductionGameDetection();
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
