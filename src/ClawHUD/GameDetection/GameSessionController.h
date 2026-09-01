#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>

#include "ForegroundTracker.h"
#include "GameDetectionCoordinator.h"
#include "GameRenderVerifier.h"
#include "GenericForegroundTrigger.h"
#include "KnownGameProcessCache.h"
#include "MicrosoftGameTrigger.h"
#include "ProductionGameWindowSource.h"
#include "ProductionProcessLifetime.h"
#include "SteamRunningAppTrigger.h"
#include "SteamRunningAppIdSource.h"
#include "PresentMonTelemetryProvider.h"

namespace clawhud
{
// The App runtime gate the production detector reads. App owns these flags
// (HUD enable in HudController, suspend/resume in App); the controller never
// holds an App&.
struct GameSessionRuntimeState
{
    bool hudEnabled{};
    bool suspended{};
    bool resumeRecoveryActive{};
};

// Narrow, domain-specific cross-controller effects. Every one maps 1:1 to an
// App call that used to live inline in the game-detection code. No generic
// event bus.
struct GameSessionHooks
{
    std::function<GameSessionRuntimeState()> runtimeState;

    // Foreground event tail: App runs ProductionTelemetryController
    // OnForegroundProcessChanged + ReconcileHudVisibility + debug observation.
    // Runs before the controller's own production game-detection handling.
    std::function<void(HWND window, DWORD processId)> onForegroundChanged;
    // Foreground tracked/match state changed: App reconciles HUD visibility.
    std::function<void()> reconcileHudVisibility;

    // ProductionTelemetryController graphics-API probe.
    std::function<void(DWORD)> startGraphicsApiProbe;
    std::function<void(DWORD)> ensureGraphicsApiProbe;
    std::function<void()> stopGraphicsApiProbe;
    std::function<void(DWORD)> stopGraphicsApiProbeIfTarget;

    // ProductionTelemetryController committed FPS target + FPS reset.
    std::function<void(DWORD)> setCommittedProcess;
    std::function<void()> clearCommittedProcess;
    std::function<void()> stopFpsSampling;

    // App production-sampling lifecycle.
    std::function<void()> startProductionSampling;
    std::function<void(bool stopRenderVerification, const wchar_t* reason)> stopProductionSampling;
};

// Owns all production game-session detection: the three triggers, the
// GameDetectionCoordinator state machine, the event sources
// (ProductionGameWindowSource, ProductionProcessLifetimeWatcher,
// ForegroundTracker, SteamRunningAppIdSource), GameRenderVerifier, the Steam
// RunningAppID session context, and every production game-session WM_APP
// payload / message. It holds a non-owning reference to the one shared
// PresentMonTelemetryProvider (for GameRenderVerifier only). It never touches
// HudPresentation, EC, or Settings UI: cross-domain effects go through hooks.
class GameSessionController
{
public:
    explicit GameSessionController(PresentMonTelemetryProvider& provider);
    ~GameSessionController();

    GameSessionController(const GameSessionController&) = delete;
    GameSessionController& operator=(const GameSessionController&) = delete;

    // SetHooks is called from the App constructor (the cross-domain effects
    // capture App members that already exist); BindMessageWindow is called from
    // App::Run once the tray/message window exists and before any source starts.
    void SetHooks(GameSessionHooks hooks);
    void BindMessageWindow(HWND messageWindow);

    // --- staged startup (App preserves the exact ordering) ---------------
    bool StartWindowSource();
    bool StartSteamWatcher();
    void InitializeSteamSession(bool steamWatcherStarted);
    bool StartForegroundTracking();

    // --- App message-loop delegation ------------------------------------
    // Returns true when `message` is a game-session message and was consumed
    // (payload deleted).
    bool HandleMessage(const MSG& message);
    // Verifier queue only (resume-recovery attempt).
    void DiscardPendingRenderVerifierEvents();
    // Verifier + Microsoft + window queues (suspend / missed-suspend fallback).
    void DiscardPendingSuspendEvents();
    // All four game-session queues (shutdown).
    void DiscardPendingEvents();

    // --- App orchestration entry points --------------------------------
    void ReevaluateForeground();
    void EnsureRenderVerification();
    void StopRenderVerification(const wchar_t* reason = L"explicit-reset",
        bool clearLatestFps = false);
    void ReleaseCommittedIfForegroundGone();
    void ClearCandidateIfNotCommitted(const wchar_t* reason);

    // --- narrow queries (resume recovery / F8 / HUD reconcile) --------
    void ReconcileForeground();
    DWORD TrackedProcessId() const noexcept;
    bool ForegroundIsTrackedProcess() const noexcept;
    DWORD VerifierProcessId() const noexcept;
    std::uint64_t VerifierGeneration() const noexcept;
    bool VerifierRunning() const noexcept;
    bool CommittedProcessAliveOrNone() const;

    // --- shutdown ----------------------------------------------------
    void StopSources();

private:
    void HandleProductionForegroundChanged(HWND window, DWORD processId);
    void HandleProductionWindowEvent(const ProductionWindowEvent& event);
    void HandleMicrosoftGameEvidence(const MicrosoftGameTriggerEvidence& evidence);
    void HandleProductionProcessExit(DWORD processId, std::uint64_t generation);
    void HandleGameRenderVerifierEvent(const GameRenderVerifierEvent& event);
    void HandleSteamRunningAppIdChanged();
    void ApplyProductionEvidence(GameDetectionTrigger trigger, HWND window, DWORD processId);
    void HandleGameDetectionTransition(
        const GameDetectionTransitionResult& transition,
        GameDetectionTrigger trigger = GameDetectionTrigger::GenericForeground,
        DWORD previousProcessId = 0, std::uint64_t previousGeneration = 0);
    void StartCandidateRenderVerification();
    void ArmProductionProcessLifetime(DWORD processId, std::uint64_t generation);
    bool TryCommitReadyCandidateFromForeground(HWND foreground, DWORD foregroundProcessId);
    void ReleaseProductionGameCandidate(const wchar_t* reason);
    void ClearProductionCandidate(const wchar_t* reason);
    void ReleaseCommittedProductionTarget(const wchar_t* reason);

    GameSessionRuntimeState Runtime() const;

    PresentMonTelemetryProvider& provider_;
    HWND messageWindow_{};
    GameSessionHooks hooks_;

    ForegroundTracker foregroundTracker_;
    GameDetectionCoordinator gameDetectionCoordinator_;
    SteamRunningAppTrigger steamRunningAppTrigger_{gameDetectionCoordinator_};
    GenericForegroundTrigger genericForegroundTrigger_;
    KnownGameProcessCache knownGameProcesses_;
    MicrosoftGameTrigger microsoftGameTrigger_{knownGameProcesses_};
    ProductionGameWindowSource productionGameWindowSource_;
    ProductionProcessLifetimeWatcher productionProcessLifetimeWatcher_;
    GameRenderVerifier gameRenderVerifier_{provider_};
    SteamRunningAppIdSource steamRunningAppIdSource_;
    std::uint32_t steamRunningAppId_{};
};
}
