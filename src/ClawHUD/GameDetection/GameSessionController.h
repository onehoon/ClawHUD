#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>

#include "ForegroundTracker.h"
#include "ForegroundGameDetector.h"
#include "GameSessionCutoverPolicy.h"
#include "GameRenderVerifier.h"
#include "KnownGameProcessCache.h"
#include "MicrosoftGameTrigger.h"
#include "ProductionGameWindowSource.h"
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
    // Current foreground-game target changed: App reconciles HUD visibility.
    std::function<void()> reconcileHudVisibility;

    // ProductionTelemetryController In-Game Only FPS target. set(pid) means this
    // PID is the current eligible foreground game target now; clear() means there
    // is no current eligible foreground game. Not process-lifetime ownership.
    std::function<void(DWORD)> setInGameForegroundProcess;
    std::function<void()> clearInGameForegroundProcess;
    std::function<void()> stopFpsSampling;

    // App production-sampling lifecycle.
    std::function<void()> startProductionSampling;
};

// Owns all production game-session detection: the foreground-first
// ForegroundGameDetector, the known-game cache, the Microsoft identity trigger,
// the event sources (ProductionGameWindowSource, ForegroundTracker,
// SteamRunningAppIdSource), GameRenderVerifier, the Steam RunningAppID session
// context, and every production game-session WM_APP payload / message. It holds
// a non-owning reference to the one shared PresentMonTelemetryProvider (for
// GameRenderVerifier only). It never touches HudPresentation, EC, or Settings
// UI: cross-domain effects go through hooks.
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
    // Liveness safety net: if the current eligible foreground game's exact
    // process generation no longer exists, clear the target and re-evaluate.
    void RevalidateCurrentForegroundGame();
    // HUD-disable / reset: stop verification, clear the current foreground-game
    // target and its downstream effects; preserve the known-game cache.
    void ResetForegroundGameSession(const wchar_t* reason);

    // --- narrow queries (resume recovery / F8 / HUD reconcile) --------
    void ReconcileForeground();
    // The current eligible foreground game: true only when the latest
    // foreground-first evaluation is Eligible for a still-matching exact
    // process generation. Never derived from HUD/FPS/telemetry state.
    bool CurrentForegroundGameActive() const noexcept;
    DWORD CurrentForegroundGameProcessId() const noexcept;
    DWORD VerifierProcessId() const noexcept;
    bool VerifierRunning() const noexcept;

    // --- shutdown ----------------------------------------------------
    void StopSources();

private:
    void HandleProductionForegroundChanged(HWND window, DWORD processId);
    void HandleProductionWindowEvent(const ProductionWindowEvent& event);
    void HandleMicrosoftGameEvidence(const MicrosoftGameTriggerEvidence& evidence);
    void HandleGameRenderVerifierUpdate(const RendererVerificationRequest& request,
        GameRenderVerifierEventType type);
    void HandleSteamRunningAppIdChanged();
    void EvaluateCurrentForeground(const wchar_t* reason);
    void ApplyForegroundEvaluation(const ForegroundGameEvaluation& evaluation,
        const wchar_t* reason);
    bool WindowEventAffectsCurrentForeground(
        const ProductionWindowEvent& event) const;

    GameSessionRuntimeState Runtime() const;

    PresentMonTelemetryProvider& provider_;
    HWND messageWindow_{};
    GameSessionHooks hooks_;

    ForegroundTracker foregroundTracker_;
    KnownGameProcessCache knownGameProcesses_;
    ForegroundGameDetector foregroundGameDetector_{knownGameProcesses_};
    MicrosoftGameTrigger microsoftGameTrigger_{knownGameProcesses_};
    ProductionGameWindowSource productionGameWindowSource_;
    GameRenderVerifier gameRenderVerifier_{provider_};
    SteamRunningAppIdSource steamRunningAppIdSource_;
    std::uint32_t steamRunningAppId_{};
    std::optional<RendererVerificationRequest> activeRendererRequest_;
    // The current eligible foreground game as an exact process generation
    // (PID + creation time), or none. Kept as a full GameProcessInstance so
    // PID reuse cannot preserve an old generation's target authority.
    std::optional<GameProcessInstance> currentForegroundGameProcess_;
};
}
