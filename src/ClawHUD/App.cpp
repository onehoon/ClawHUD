#include "App.h"

#include "SettingsWindow.h"
#include "Tweaks/IntelVrr/IntelVrrResultStore.h"
#include "SupportedHardware.h"
#include "UninstallCleanup.h"
#include "RuntimeLogger.h"
#include "Version.h"
#include "ProductionTargetPolicy.h"
#include "SuspendResumePolicy.h"
#include "ProcessLiveness.h"
#include "HudSettingsStore.h"
#include "GameDetection/DebugObservationController.h"

#include <Velopack.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <shobjidl.h>
#include <shlobj.h>
#include <cwctype>
#include <sstream>
#include <string>

namespace
{
// WM_APP + 1 is the application/UI message; the game-session ids (WM_APP + 2, 5,
// 6, 7, 8, 9) live in GameSessionController.cpp and the telemetry timer ids in
// ProductionTelemetryController.h.
constexpr UINT kSettingsDestroyed = WM_APP + 1;
constexpr wchar_t kInstanceMutexName[] = L"Local\\ClawHUD.SingleInstance";

void Log(const std::wstring& message)
{
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, message);
}

using clawhud::ProcessAlive;

}

App::App(HINSTANCE instance) : instance_(instance), tray_(*this)
{
    clawhud::RuntimeLogger::Initialize();
    wchar_t path[MAX_PATH]{}; const DWORD length = GetModuleFileNameW(instance_, path, ARRAYSIZE(path));
    executablePath_.assign(path, length);
    LoadHudSettings();
    hudController_.SetRenderCallback(
        [this](bool allowHidden) { RenderProductionHud(allowHidden); });
    gameSession_.SetHooks(MakeGameSessionHooks());
    productionTelemetry_.SyncVisibilityMode(hudController_.Options().visibilityMode);
    clawhud::RuntimeLogger::SetDebugLogging(debugLoggingEnabled_);
    Log(L"ClawHUD started version=" CLAWHUD_VERSION L" pid=" +
        std::to_wstring(GetCurrentProcessId()));
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
        L"Runtime settings HUDEnabled=" + std::to_wstring(hudController_.Enabled() ? 1 : 0) +
        L" HUDSizeOffset=" + std::to_wstring(hudController_.SizeOffset()) +
        L" StartWithWindows=" + std::to_wstring(startWithWindows_ ? 1 : 0));
}

App::~App()
{
    Log(L"ClawHUD exiting");
    CancelResumeRecovery();
    StopProductionSampling(false, L"app-shutdown");
    productionTelemetry_.StopGraphicsApiProbe();
    gameSession_.StopSources();
    if (debugObservation_)
        debugObservation_->Stop();
    if (hudHotkeyRegistered_ && tray_.Window())
        UnregisterHotKey(tray_.Window(), kHudToggleHotkeyId);
    hudHotkeyRegistered_ = false;
    hudController_.DestroyPresentation();
    settings_.reset();
    tray_.Destroy();
    if (instanceMutex_)
    {
        ReleaseMutex(instanceMutex_);
        CloseHandle(instanceMutex_);
    }
}

clawhud::GameSessionHooks App::MakeGameSessionHooks()
{
    clawhud::GameSessionHooks hooks;
    hooks.runtimeState = [this]
    {
        return clawhud::GameSessionRuntimeState{
            hudController_.Enabled(), suspended_, resumeRecoveryActive_};
    };
    hooks.onForegroundChanged = [this](HWND window, DWORD processId)
    {
        productionTelemetry_.OnForegroundProcessChanged(processId);
        ReconcileHudVisibility();
        if (debugObservation_)
            debugObservation_->OnForegroundChanged(window, processId);
    };
    hooks.reconcileHudVisibility = [this] { ReconcileHudVisibility(); };
    hooks.startGraphicsApiProbe = [this](DWORD pid)
        { productionTelemetry_.StartGraphicsApiProbe(pid); };
    hooks.ensureGraphicsApiProbe = [this](DWORD pid)
        { productionTelemetry_.EnsureGraphicsApiProbe(pid); };
    hooks.stopGraphicsApiProbe = [this]
        { productionTelemetry_.StopGraphicsApiProbe(); };
    hooks.stopGraphicsApiProbeIfTarget = [this](DWORD pid)
        { productionTelemetry_.StopGraphicsApiProbeIfTarget(pid); };
    hooks.setCommittedProcess = [this](DWORD pid)
        { productionTelemetry_.SetCommittedProcess(pid); };
    hooks.clearCommittedProcess = [this]
        { productionTelemetry_.ClearCommittedProcess(); };
    hooks.stopFpsSampling = [this]
        { productionTelemetry_.StopFpsSampling(); };
    hooks.startProductionSampling = [this] { StartProductionSampling(); };
    hooks.stopProductionSampling = [this](bool stopRenderVerification, const wchar_t* reason)
        { StopProductionSampling(stopRenderVerification, reason); };
    return hooks;
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
    productionTelemetry_.Bind(tray_.Window(), [this] { RenderProductionHud(); });
    gameSession_.BindMessageWindow(tray_.Window());
    if (!gameSession_.StartWindowSource())
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"Production game window source failed to start; continuing with generic/Steam detection");
    }
    const bool steamWatcherStarted = gameSession_.StartSteamWatcher();
    if (!steamWatcherStarted)
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"Steam RunningAppID watcher initialization failed");
    gameSession_.InitializeSteamSession(steamWatcherStarted);
    if (debugLoggingEnabled_)
    {
        debugObservation_ = std::make_unique<clawhud::DebugObservationController>(
            presentMonTelemetryProvider_);
        debugObservation_->Start();
    }
    hudHotkeyRegistered_ = RegisterHotKey(tray_.Window(), kHudToggleHotkeyId,
        MOD_NOREPEAT, VK_F8) != FALSE;
    if (!hudHotkeyRegistered_)
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"RegisterHotKey(F8) failed; continuing without the global HUD toggle");
    const bool providerReady = presentMonTelemetryProvider_.Initialize();
    Log(L"[PresentMon] providerReady=" + std::to_wstring(providerReady) +
        L" processReady=" + std::to_wstring(
            presentMonTelemetryProvider_.ProcessReady()) +
        L" systemReady=" + std::to_wstring(
            presentMonTelemetryProvider_.SystemReady()));
    if (!gameSession_.StartForegroundTracking())
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"Foreground tracker initialization failed");
        return 1;
    }
    if (hudController_.Enabled())
    {
        if (!hudController_.Ensure())
        {
            hudController_.AbandonEnable();
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"Persisted HUD enable restore failed during initialization");
        }
        else
        {
            gameSession_.ReevaluateForeground();
            ReconcileHudVisibility();
        }
    }
    tweakStartupCoordinator_.Start(intelVrrRangeFixEnabled_);
    return ProcessMessages();
}

void App::SetIntelVrrRangeFixEnabled(bool enabled)
{
    intelVrrRangeFixEnabled_ = enabled;
    hudSettingsStore_.SaveIntelVrrRangeFixEnabled(enabled);
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

void App::HandleSystemSuspend()
{
    if (suspended_)
        return;
    suspended_ = true;
    CancelResumeRecovery();
    hudController_.HideForSuspend();
    PauseProductionSamplingForSuspend();
    gameSession_.DiscardPendingSuspendEvents();
    Log(L"System suspend detected");
}

void App::HandleSystemResume()
{
    if (!clawhud::ResumeRecoveryShouldStart(resumeRecoveryActive_))
        return;
    if (clawhud::ResumeRecoveryNeedsSuspendFallback(suspended_))
    {
        hudController_.HideForResumeFallback();
        PauseProductionSamplingForSuspend();
        gameSession_.DiscardPendingSuspendEvents();
        Log(L"Suspend notification was missed; resume fallback prepared");
    }
    suspended_ = false;
    resumeRecoveryActive_ = true;
    resumeRecoveryAttempts_ = 0;
    SetTimer(tray_.Window(), kResumeRecoveryTimerId,
        clawhud::kResumeRecoveryIntervalMs, nullptr);
    Log(L"System resume detected");
    Log(L"HUD resume recovery started");
}

void App::TryResumeRecovery()
{
    if (!resumeRecoveryActive_)
        return;

    ++resumeRecoveryAttempts_;
    gameSession_.ReconcileForeground();
    const DWORD processId = gameSession_.TrackedProcessId();
    const bool processAlive = processId && ProcessAlive(processId);
    const bool retainVerifier = clawhud::ResumeRecoveryCanRetainVerifier(
        processId, gameSession_.VerifierProcessId(), gameSession_.VerifierRunning());
    const bool rendererForegroundActive = gameSession_.ForegroundIsTrackedProcess();
    const bool hudEnabled = hudController_.Enabled();
    const auto manualOverride = hudController_.ManualOverride();
    const auto visibilityMode = hudController_.VisibilityMode();
    const bool expectedVisible = hudEnabled &&
        (manualOverride.has_value()
            ? *manualOverride
            : visibilityMode == clawhud::HudVisibilityMode::Always ||
                rendererForegroundActive);
    const bool visibilityUsesForeground = !manualOverride.has_value() &&
        visibilityMode == clawhud::HudVisibilityMode::InGameOnly;
    gameSession_.DiscardPendingRenderVerifierEvents();
    if (clawhud::ResumeRecoveryShouldWaitForForeground(
        hudEnabled, visibilityUsesForeground, processAlive,
        rendererForegroundActive, resumeRecoveryAttempts_))
    {
        SetTimer(tray_.Window(), kResumeRecoveryTimerId,
            clawhud::kResumeRecoveryIntervalMs, nullptr);
        return;
    }

    if (processAlive)
        productionTelemetry_.StartGraphicsApiProbe(processId);
    else
        productionTelemetry_.StopGraphicsApiProbe();

    bool freshFrameReady = !expectedVisible || hudController_.HasPresentation();
    if (expectedVisible && hudController_.HasPresentation())
    {
        const HRESULT clearHr = hudController_.RenderRecoveryFrame();
        freshFrameReady = clawhud::ResumeRecoveryFrameWasPresented(clearHr);
        if (!freshFrameReady && clearHr != S_FALSE && resumeRecoveryAttempts_ == 1)
            freshFrameReady = hudController_.Recreate(false);
    }
    if (!clawhud::ResumeRecoveryMayShowHud(expectedVisible, freshFrameReady))
    {
        resumeRecoveryActive_ = true;
        if (!clawhud::ResumeRecoveryHasAttemptsRemaining(resumeRecoveryAttempts_))
        {
            CancelResumeRecovery();
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
                L"HUD resume recovery exhausted");
            return;
        }
        SetTimer(tray_.Window(), kResumeRecoveryTimerId,
            clawhud::kResumeRecoveryIntervalMs, nullptr);
        return;
    }

    resumeRecoveryActive_ = false;
    ReconcileHudVisibility();
    if (expectedVisible && !HudVisible() && resumeRecoveryAttempts_ == 1)
    {
        hudController_.Recreate(true);
        ReconcileHudVisibility();
    }

    const bool recovered = !expectedVisible || HudVisible();
    if (recovered)
    {
        if (retainVerifier && gameSession_.VerifierProcessId() == processId &&
            gameSession_.VerifierRunning())
            Log(L"[GameDetection] verifier.resume-retained pid=" +
                std::to_wstring(processId));
        else if (processAlive && gameSession_.VerifierProcessId() == processId &&
            gameSession_.VerifierRunning())
            Log(L"[GameDetection] verifier.resume-restarted pid=" +
                std::to_wstring(processId));
        const unsigned completedAttempt = resumeRecoveryAttempts_;
        CancelResumeRecovery();
        if (clawhud::ShouldReevaluateForegroundAfterResume(
            hudEnabled, recovered))
            gameSession_.ReevaluateForeground();
        Log(L"HUD resume recovery completed attempt=" +
            std::to_wstring(completedAttempt));
        return;
    }

    resumeRecoveryActive_ = true;
    if (!clawhud::ResumeRecoveryHasAttemptsRemaining(resumeRecoveryAttempts_))
    {
        CancelResumeRecovery();
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"HUD resume recovery exhausted");
        return;
    }
    SetTimer(tray_.Window(), kResumeRecoveryTimerId,
        clawhud::kResumeRecoveryIntervalMs, nullptr);
}

void App::StopHud()
{
    hudController_.MarkDisabled();
    StopProductionSampling(true, L"hud-disabled");
    productionTelemetry_.StopGraphicsApiProbe();
    gameSession_.ClearCandidateIfNotCommitted(L"hud-disabled");
    productionTelemetry_.StopFpsSampling();
    ReconcileHudVisibility();
    hudController_.ShutdownPresentation();
}

bool App::SetHudEnabled(bool enabled)
{
    if (!enabled)
    {
        StopHud();
        SaveHudEnabledSetting(false);
        return true;
    }
    if (!hudController_.Ensure()) return false;
    hudController_.MarkEnabled(true);
    gameSession_.ReevaluateForeground();
    hudController_.ResetManualOverride();
    ReconcileHudVisibility();
    SaveHudEnabledSetting(true);
    return true;
}

void App::SetHudAlignment(clawhud::HudAlignment alignment)
{
    if (hudController_.SetAlignment(alignment))
        SaveHudSettings();
}

void App::SetHudFont(clawhud::HudFont font)
{
    if (hudController_.SetFont(font))
    {
        SaveHudSettings();
        if (settings_)
            settings_->UpdateHudControls();
    }
}

void App::SetHudBackgroundMode(clawhud::HudBackgroundMode mode)
{
    if (hudController_.SetBackgroundMode(mode))
        SaveHudSettings();
}

bool App::SetHudOpacity(float opacity, bool persist)
{
    if (!hudController_.SetOpacity(opacity))
        return false;
    if (persist) SaveHudSettings();
    return true;
}

void App::SetHudSizeOffset(int offset)
{
    if (hudController_.SetSizeOffset(offset))
        SaveHudSettings();
}

void App::RenderProductionHud(bool allowHidden)
{
    clawhud::HudTelemetrySnapshot snapshot{};
    productionTelemetry_.FillSnapshot(snapshot);
    hudController_.Render(snapshot, allowHidden);
}

void App::SampleProductionTelemetry()
{
    if (suspended_ || !hudController_.Enabled() || !HudVisible())
        return;
    productionTelemetry_.SampleSystemEc();
}

void App::SampleProductionBatteryTelemetry()
{
    if (suspended_ || !hudController_.Enabled() || !HudVisible())
        return;
    productionTelemetry_.SampleBattery();
}

void App::StartProductionSampling()
{
    if (suspended_ || !HudVisible())
        return;
    productionTelemetry_.StartBaseSampling();
    gameSession_.EnsureRenderVerification();
    productionTelemetry_.StartFpsSampling();
}

void App::SampleProductionFpsTelemetry()
{
    if (suspended_ || !hudController_.Enabled() || !HudVisible())
        return;
    productionTelemetry_.SampleFps();
}

void App::PauseProductionSamplingForSuspend()
{
    productionTelemetry_.StopSamplingTimersAndFps();
    gameSession_.StopRenderVerification(L"suspend", false);
    productionTelemetry_.StopGraphicsApiProbe();
    productionTelemetry_.ResetSamplingState(L"suspend");
}

void App::CancelResumeRecovery()
{
    KillTimer(tray_.Window(), kResumeRecoveryTimerId);
    resumeRecoveryActive_ = false;
    resumeRecoveryAttempts_ = 0;
}

void App::StopProductionSampling(bool stopRenderVerification, const wchar_t* reason)
{
    productionTelemetry_.StopSamplingTimersAndFps();
    if (stopRenderVerification)
        gameSession_.StopRenderVerification();
    productionTelemetry_.ResetSamplingState(reason);
}

void App::TryGraphicsApiProbe()
{
    productionTelemetry_.TryGraphicsApiProbe();
}

void App::SetHudVisibilityMode(clawhud::HudVisibilityMode mode)
{
    const auto previousMode = hudController_.SetVisibilityMode(mode);
    if (mode != previousMode)
    {
        DWORD foregroundProcessId{};
        if (mode == clawhud::HudVisibilityMode::Always)
        {
            // Adopt the currently known foreground PID immediately instead of
            // waiting for the next foreground-change event.
            HWND foreground = GetForegroundWindow();
            if (foreground)
                GetWindowThreadProcessId(foreground, &foregroundProcessId);
        }
        productionTelemetry_.SetVisibilityMode(mode, foregroundProcessId);
        SampleProductionFpsTelemetry();
    }
    SaveHudSettings();
    ReconcileHudVisibility();
}

void App::HandleHudToggleHotkey()
{
    if (!hudController_.Enabled())
    {
        if (!hudController_.Ensure())
        {
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
                L"F8 HUD ON initialization failed");
            return;
        }
        hudController_.MarkEnabled(false);
        gameSession_.ReevaluateForeground();
        if (const DWORD processId = gameSession_.TrackedProcessId())
            productionTelemetry_.StartGraphicsApiProbe(processId);
    }
    hudController_.SetManualOverride(!HudVisible());
    ReconcileHudVisibility();
    if (settings_) settings_->UpdateHudControls();
}

void App::ReconcileHudVisibility()
{
    if (!hudController_.HasPresentation())
        return;
    if (suspended_ || resumeRecoveryActive_)
    {
        hudController_.HideForLifecycleGate();
        return;
    }
    gameSession_.ReleaseCommittedIfForegroundGone();
    productionTelemetry_.ReconcileGraphicsApiTargetLiveness();
    const bool foregroundGameActive = gameSession_.ForegroundIsTrackedProcess();
    const auto effects = hudController_.ReconcileVisibility(foregroundGameActive);
    if (effects.startProductionSampling)
        StartProductionSampling();
    if (effects.stopProductionSampling)
        StopProductionSampling(false, L"hud-hidden");
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
    const auto settings = hudSettingsStore_.Load();
    debugLoggingEnabled_ = settings.debugLoggingEnabled;
    startWithWindows_ = settings.startWithWindows;
    intelVrrRangeFixEnabled_ = settings.intelVrrRangeFixEnabled;
    clawhud::HudControllerState hudState;
    hudState.enabled = settings.hudEnabled;
    hudState.options.alignment = settings.alignment;
    hudState.font = settings.font;
    hudState.options.backgroundMode = settings.backgroundMode;
    hudState.options.visibilityMode = settings.visibilityMode;
    hudState.sizeOffset = settings.sizeOffset;
    hudState.options.backgroundOpacity = settings.backgroundOpacity;
    hudController_.RestoreState(hudState);
}

void App::SaveHudSettings() const
{
    const auto& options = hudController_.Options();
    clawhud::HudSettings settings;
    settings.alignment = options.alignment;
    settings.font = hudController_.Font();
    settings.backgroundMode = options.backgroundMode;
    settings.visibilityMode = options.visibilityMode;
    settings.backgroundOpacity = options.backgroundOpacity;
    settings.sizeOffset = hudController_.SizeOffset();
    settings.startWithWindows = startWithWindows_;
    hudSettingsStore_.Save(settings);
}

void App::SaveHudEnabledSetting(bool enabled) const
{
    hudSettingsStore_.SaveEnabled(enabled);
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
    StopProductionSampling(false, L"app-shutdown");
    productionTelemetry_.StopGraphicsApiProbe();
    gameSession_.StopSources();
    if (debugObservation_)
        debugObservation_->Stop();
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
        if (message.message == kSettingsDestroyed)
        {
            SettingsDestroyed();
            continue;
        }
        if (gameSession_.HandleMessage(message))
            continue;
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
