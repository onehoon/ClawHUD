#include "App.h"

#include "SettingsWindow.h"
#include "Tweaks/IntelVrr/IntelVrrResultStore.h"
#include "SupportedHardware.h"
#include "UninstallCleanup.h"
#include "RuntimeLogger.h"
#include "Version.h"
#include "ProductionTargetPolicy.h"
#include "SuspendResumePolicy.h"
#include "GameDetection/GameDetectionTrace.h"
#include "Win32Format.h"
#include "ProcessLiveness.h"
#include "HudSettingsStore.h"

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
constexpr UINT kSettingsDestroyed = WM_APP + 1;
constexpr UINT kForegroundChanged = WM_APP + 2;
constexpr UINT kSteamRunningAppIdChanged = WM_APP + 5;
constexpr UINT kMicrosoftGameEvidence = WM_APP + 6;
constexpr UINT kGameRenderVerifierUpdate = WM_APP + 7;
constexpr UINT kProductionWindowEvent = WM_APP + 8;
constexpr UINT kProductionProcessExit = WM_APP + 9;
constexpr wchar_t kInstanceMutexName[] = L"Local\\ClawHUD.SingleInstance";

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

void Log(const std::wstring& message)
{
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, message);
}

using clawhud::HexHresult;
using clawhud::HwndText;
using clawhud::ProcessAlive;

}

App::App(HINSTANCE instance) : instance_(instance), tray_(*this),
    steamRunningAppTrigger_(gameDetectionCoordinator_)
{
    clawhud::RuntimeLogger::Initialize();
    wchar_t path[MAX_PATH]{}; const DWORD length = GetModuleFileNameW(instance_, path, ARRAYSIZE(path));
    executablePath_.assign(path, length);
    LoadHudSettings();
    hudController_.SetRenderCallback(
        [this](bool allowHidden) { RenderProductionHud(allowHidden); });
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
    productionGameWindowSource_.Stop();
    productionProcessLifetimeWatcher_.Disarm();
    foregroundTracker_.Stop();
    windowLifecycleSource_.Stop();
    presentActivitySource_.Stop();
    processLifecycleSource_.Stop();
    StopGameRenderVerification(L"app-shutdown", true);
    steamRunningAppIdSource_.Stop();
    DiscardPendingGameRenderVerifierEvents();
    DiscardPendingMicrosoftGameEvidence();
    DiscardPendingProductionWindowEvents();
    DiscardPendingProductionProcessExitEvents();
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
        presentActivitySource_.Start(presentMonTelemetryProvider_);
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
            productionTelemetry_.OnForegroundProcessChanged(processId);
            ReconcileHudVisibility();
            if (debugLoggingEnabled_)
            {
                windowsGameIdentitySource_.QueueInspect(window, processId);
                presentActivitySource_.Watch(processId);
            }
            if (hudController_.Enabled())
                HandleProductionForegroundChanged(window, processId);
        }))
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
    DiscardPendingGameRenderVerifierEvents();
    DiscardPendingMicrosoftGameEvidence();
    DiscardPendingProductionWindowEvents();
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
        DiscardPendingGameRenderVerifierEvents();
        DiscardPendingProductionWindowEvents();
        DiscardPendingMicrosoftGameEvidence();
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
    foregroundTracker_.Reconcile();
    const DWORD processId = foregroundTracker_.TrackedProcessId();
    const bool processAlive = processId && ProcessAlive(processId);
    const bool retainVerifier = clawhud::ResumeRecoveryCanRetainVerifier(
        processId, gameRenderVerifier_.ProcessId(), gameRenderVerifier_.Running());
    const bool rendererForegroundActive = foregroundTracker_.ForegroundIsTrackedProcess();
    const bool hudEnabled = hudController_.Enabled();
    const auto manualOverride = hudController_.ManualOverride();
    const auto visibilityMode = hudController_.VisibilityMode();
    const bool expectedVisible = hudEnabled &&
        (manualOverride.has_value()
            ? *manualOverride
            : visibilityMode == clawhud::HudVisibilityMode::Always ||
                rendererForegroundActive ||
                foregroundTracker_.ForegroundIsTrackedProcess());
    const bool visibilityUsesForeground = !manualOverride.has_value() &&
        visibilityMode == clawhud::HudVisibilityMode::InGameOnly;
    DiscardPendingGameRenderVerifierEvents();
    if (clawhud::ResumeRecoveryShouldWaitForForeground(
        hudEnabled, visibilityUsesForeground, processAlive,
        foregroundTracker_.ForegroundIsTrackedProcess(), resumeRecoveryAttempts_))
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
        if (retainVerifier && gameRenderVerifier_.ProcessId() == processId &&
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
            hudEnabled, recovered))
            ReevaluateProductionGameDetection();
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
    if (gameDetectionCoordinator_.Context().state != clawhud::GameDetectionState::Committed)
        ClearProductionCandidate(L"hud-disabled");
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
    ReevaluateProductionGameDetection();
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
    StartGameRenderVerification();
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
    StopGameRenderVerification(L"suspend", false);
    productionTelemetry_.StopGraphicsApiProbe();
    productionTelemetry_.ResetSamplingState(L"suspend");
}

void App::ReleaseCommittedProductionTarget(const wchar_t* reason)
{
    const auto& context = gameDetectionCoordinator_.Context();
    const DWORD processId = context.candidateProcessId;
    const auto generation = context.generation;
    if (!processId)
        return;
    productionTelemetry_.ClearCommittedProcess();
    const auto release = clawhud::PlanCommittedTargetRelease();
    clawhud::CommittedTargetReleaseOps ops;
    ops.stopRenderVerification = [this, reason]
    {
        StopGameRenderVerification(reason, true);
    };
    ops.stopGraphicsApiProbe = [this]
    {
        productionTelemetry_.StopGraphicsApiProbe();
    };
    ops.clearTrackedProcess = [this]
    {
        foregroundTracker_.SetTrackedProcessId(0);
    };
    ops.startGlobalTelemetry = [this]
    {
        StartProductionSampling();
    };
    ops.stopGlobalTelemetry = [this, reason]
    {
        StopProductionSampling(false, reason);
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

void App::StopProductionSampling(bool stopRenderVerification, const wchar_t* reason)
{
    productionTelemetry_.StopSamplingTimersAndFps();
    if (stopRenderVerification)
        StopGameRenderVerification();
    productionTelemetry_.ResetSamplingState(reason);
}

void App::StartGameRenderVerification()
{
    if (suspended_ || !hudController_.Enabled())
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
    // The API2 verifier's job ends at the first displayed frame; once the
    // renderer is confirmed for the current target there is nothing to re-run.
    if (context.rendererObserved &&
        gameRenderVerifier_.ProcessId() == processId &&
        gameRenderVerifier_.Generation() == generation)
        return;

    StopGameRenderVerification(L"target-handoff", false);
    Log(L"[GameDetection] verifier.start pid=" + std::to_wstring(processId) +
        L" gen=" + std::to_wstring(generation));
    const bool started = gameRenderVerifier_.Start(processId, generation,
        [this](const clawhud::GameRenderVerifierEvent& event)
        {
            auto* update = new GameRenderVerifierUpdate{event};
            if (!PostMessageW(tray_.Window(), kGameRenderVerifierUpdate,
                reinterpret_cast<WPARAM>(update), 0))
                delete update;
        });
    if (started)
        Log(L"[GameDetection] verifier.api2-ready pid=" + std::to_wstring(processId) +
            L" gen=" + std::to_wstring(generation));
    else
    {
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error,
            L"[GameDetection] verifier.start-failed pid=" +
            std::to_wstring(processId) + L" gen=" + std::to_wstring(generation));
        if (!committed)
            ReleaseProductionGameCandidate(L"verifier-start-failed");
    }
}

void App::StopGameRenderVerification(const wchar_t* reason, bool clearLatestFps)
{
    if (gameRenderVerifier_.ProcessId())
    {
        const DWORD processId = gameRenderVerifier_.ProcessId();
        Log(L"[GameDetection] verifier.stop pid=" +
            std::to_wstring(processId) + L" reason=" + reason);
        gameRenderVerifier_.Stop();
    }
    if (clearLatestFps)
        productionTelemetry_.StopFpsSampling();
}

void App::TryGraphicsApiProbe()
{
    productionTelemetry_.TryGraphicsApiProbe();
}

void App::HandleGameRenderVerifierEvent(
    const clawhud::GameRenderVerifierEvent& event)
{
    const auto& context = gameDetectionCoordinator_.Context();
    if (suspended_ || resumeRecoveryActive_ ||
        event.processId != context.candidateProcessId ||
        event.generation != context.generation)
        return;
    if (event.type != clawhud::GameRenderVerifierEventType::FirstDisplayedFrame)
        return;
    if (!clawhud::GameRenderVerifier::ApplyRendererEvidence(
        gameDetectionCoordinator_, event))
        return;
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
        hudController_.Enabled(), suspended_))
        return;
    if (TryCommitReadyCandidateFromForeground(window, processId))
        return;
    const auto& context = gameDetectionCoordinator_.Context();
    if (context.state == clawhud::GameDetectionState::Committed)
    {
        if (ProcessAlive(context.candidateProcessId))
        {
            productionTelemetry_.EnsureGraphicsApiProbe(context.candidateProcessId);
            StartGameRenderVerification();
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
    if (!clawhud::ShouldConsiderForegroundProductionTarget(
        hudController_.Enabled(), suspended_))
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
        hudController_.Enabled(), suspended_))
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
        productionTelemetry_.StopFpsSampling();
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
        StopGameRenderVerification(L"candidate-replaced", true);
        productionTelemetry_.StopGraphicsApiProbe();
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
    StartGameRenderVerification();
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
    if (hudController_.Enabled() && !suspended_)
        ReevaluateProductionGameDetection();
}

bool App::TryCommitReadyCandidateFromForeground(HWND,
    DWORD foregroundProcessId)
{
    const auto& context = gameDetectionCoordinator_.Context();
    if (!clawhud::ShouldCommitReadyCandidate(
            context, foregroundProcessId, ProcessAlive(context.candidateProcessId)) ||
        !hudController_.Enabled() || suspended_)
        return false;
    const DWORD processId = context.candidateProcessId;
    const auto generation = context.generation;
    if (!gameDetectionCoordinator_.CommitCandidate(processId, generation))
        return false;
    foregroundTracker_.SetTrackedProcessId(processId);
    productionTelemetry_.StartGraphicsApiProbe(processId);
    productionTelemetry_.SetCommittedProcess(processId);
    StartProductionSampling();
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
    StopGameRenderVerification(reason, true);
    productionTelemetry_.StopGraphicsApiProbeIfTarget(context.candidateProcessId);
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
        ReevaluateProductionGameDetection();
        if (const DWORD processId = foregroundTracker_.TrackedProcessId())
            productionTelemetry_.StartGraphicsApiProbe(processId);
    }
    hudController_.SetManualOverride(!HudVisible());
    ReconcileHudVisibility();
    if (settings_) settings_->UpdateHudControls();
}

void App::DiscardPendingGameRenderVerifierEvents()
{
    MSG message{};
    while (PeekMessageW(&message, tray_.Window(), kGameRenderVerifierUpdate,
        kGameRenderVerifierUpdate, PM_REMOVE))
        delete reinterpret_cast<GameRenderVerifierUpdate*>(message.wParam);
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
    if (!hudController_.HasPresentation())
        return;
    if (suspended_ || resumeRecoveryActive_)
    {
        hudController_.HideForLifecycleGate();
        return;
    }
    if (gameDetectionCoordinator_.Context().state == clawhud::GameDetectionState::Committed &&
        gameDetectionCoordinator_.Context().candidateProcessId &&
        !ProcessAlive(gameDetectionCoordinator_.Context().candidateProcessId))
        ReleaseCommittedProductionTarget(L"game-exited");
    productionTelemetry_.ReconcileGraphicsApiTargetLiveness();
    const bool foregroundGameActive = foregroundTracker_.ForegroundIsTrackedProcess();
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
    productionGameWindowSource_.Stop();
    productionProcessLifetimeWatcher_.Disarm();
    foregroundTracker_.Stop();
    windowLifecycleSource_.Stop();
    presentActivitySource_.Stop();
    processLifecycleSource_.Stop();
    StopGameRenderVerification(L"app-shutdown", true);
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
