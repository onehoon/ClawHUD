#include "GameSessionController.h"

#include "GameDetectionTrace.h"
#include "../ProductionTargetPolicy.h"
#include "../ProcessLiveness.h"
#include "../RuntimeLogger.h"
#include "../Win32Format.h"

#include <string>

namespace clawhud
{
namespace
{
// Production game-session WM_APP ids. Numeric values are shared with the
// application message window and stay distinct from App's kSettingsDestroyed
// (WM_APP + 1) and the telemetry timer ids.
constexpr UINT kForegroundChanged = WM_APP + 2;
constexpr UINT kSteamRunningAppIdChanged = WM_APP + 5;
constexpr UINT kMicrosoftGameEvidence = WM_APP + 6;
constexpr UINT kGameRenderVerifierUpdate = WM_APP + 7;
constexpr UINT kProductionWindowEvent = WM_APP + 8;
constexpr UINT kProductionProcessExit = WM_APP + 9;

struct MicrosoftGameEvidenceUpdate
{
    MicrosoftGameTriggerEvidence evidence;
};

struct ProductionWindowEventUpdate
{
    ProductionWindowEvent event;
};

struct ProductionProcessExitUpdate
{
    DWORD processId{};
    std::uint64_t generation{};
};

struct GameRenderVerifierUpdate
{
    GameRenderVerifierEvent event;
};

void Log(const std::wstring& message)
{
    RuntimeLogger::Log(RuntimeLogLevel::Info, message);
}

void DrainQueue(HWND window, UINT id,
    void (*deleter)(WPARAM))
{
    MSG message{};
    while (PeekMessageW(&message, window, id, id, PM_REMOVE))
        deleter(message.wParam);
}
}

GameSessionController::GameSessionController(PresentMonTelemetryProvider& provider)
    : provider_(provider)
{
}

GameSessionController::~GameSessionController() = default;

void GameSessionController::SetHooks(GameSessionHooks hooks)
{
    hooks_ = std::move(hooks);
}

void GameSessionController::BindMessageWindow(HWND messageWindow)
{
    messageWindow_ = messageWindow;
}

GameSessionRuntimeState GameSessionController::Runtime() const
{
    return hooks_.runtimeState();
}

// --- staged startup --------------------------------------------------------

bool GameSessionController::StartWindowSource()
{
    return productionGameWindowSource_.Start(
        [this](const ProductionWindowEvent& event)
        {
            auto* windowUpdate = new ProductionWindowEventUpdate{event};
            if (!PostMessageW(messageWindow_, kProductionWindowEvent,
                reinterpret_cast<WPARAM>(windowUpdate), 0))
                delete windowUpdate;
        });
}

bool GameSessionController::StartSteamWatcher()
{
    return steamRunningAppIdSource_.Start(messageWindow_, kSteamRunningAppIdChanged);
}

void GameSessionController::InitializeSteamSession(bool steamWatcherStarted)
{
    steamRunningAppId_ = steamRunningAppIdSource_.GetRunningAppId();
    if (steamRunningAppId_ != 0)
        Log(L"[GameDetection] steam.session oldAppId=0 newAppId=" +
            std::to_wstring(steamRunningAppId_));
    HandleGameDetectionTransition(
        steamRunningAppTrigger_.Initialize(steamRunningAppId_));
    if (steamWatcherStarted)
        Log(L"[GameDetection] steam.watcher-started appId=" +
            std::to_wstring(steamRunningAppId_));
}

bool GameSessionController::StartForegroundTracking()
{
    return foregroundTracker_.Start(messageWindow_, kForegroundChanged,
        [this](bool matches)
        {
            if (matches)
                Log(L"Foreground target pid=" +
                    std::to_wstring(foregroundTracker_.TrackedProcessId()));
            else
                Log(L"Foreground target cleared");
            hooks_.reconcileHudVisibility();
        },
        [this](HWND window, DWORD processId)
        {
            hooks_.onForegroundChanged(window, processId);
            if (Runtime().hudEnabled)
                HandleProductionForegroundChanged(window, processId);
        });
}

// --- App message-loop delegation -----------------------------------------

bool GameSessionController::HandleMessage(const MSG& message)
{
    if (message.message == kForegroundChanged)
    {
        foregroundTracker_.Reconcile();
        return true;
    }
    if (message.message == kGameRenderVerifierUpdate)
    {
        auto* update = reinterpret_cast<GameRenderVerifierUpdate*>(message.wParam);
        if (update)
        {
            HandleGameRenderVerifierEvent(update->event);
            delete update;
        }
        return true;
    }
    if (message.message == kProductionWindowEvent)
    {
        auto* update = reinterpret_cast<ProductionWindowEventUpdate*>(message.wParam);
        if (update)
        {
            HandleProductionWindowEvent(update->event);
            delete update;
        }
        return true;
    }
    if (message.message == kProductionProcessExit)
    {
        auto* update = reinterpret_cast<ProductionProcessExitUpdate*>(message.wParam);
        if (update)
        {
            HandleProductionProcessExit(update->processId, update->generation);
            delete update;
        }
        return true;
    }
    if (message.message == kMicrosoftGameEvidence)
    {
        auto* update = reinterpret_cast<MicrosoftGameEvidenceUpdate*>(message.wParam);
        if (update)
        {
            HandleMicrosoftGameEvidence(update->evidence);
            delete update;
        }
        return true;
    }
    if (message.message == kSteamRunningAppIdChanged)
    {
        HandleSteamRunningAppIdChanged();
        return true;
    }
    return false;
}

void GameSessionController::HandleSteamRunningAppIdChanged()
{
    const auto current = steamRunningAppIdSource_.GetRunningAppId();
    if (!RunningAppIdChanged(steamRunningAppId_, current))
        return;
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
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[GameDetection] steam.session-cleared appId=" +
            std::to_wstring(previous) + L" candidatePid=" +
            std::to_wstring(context.candidateProcessId) + L" state=" +
            std::wstring(GameDetectionStateName(context.state)));
    }
}

void GameSessionController::DiscardPendingRenderVerifierEvents()
{
    DrainQueue(messageWindow_, kGameRenderVerifierUpdate, [](WPARAM w)
        { delete reinterpret_cast<GameRenderVerifierUpdate*>(w); });
}

void GameSessionController::DiscardPendingSuspendEvents()
{
    DiscardPendingRenderVerifierEvents();
    DrainQueue(messageWindow_, kMicrosoftGameEvidence, [](WPARAM w)
        { delete reinterpret_cast<MicrosoftGameEvidenceUpdate*>(w); });
    DrainQueue(messageWindow_, kProductionWindowEvent, [](WPARAM w)
        { delete reinterpret_cast<ProductionWindowEventUpdate*>(w); });
}

void GameSessionController::DiscardPendingEvents()
{
    DiscardPendingSuspendEvents();
    DrainQueue(messageWindow_, kProductionProcessExit, [](WPARAM w)
        { delete reinterpret_cast<ProductionProcessExitUpdate*>(w); });
}

// --- App orchestration entry points ------------------------------------

void GameSessionController::ReevaluateForeground()
{
    HWND foreground = GetForegroundWindow();
    DWORD processId{};
    if (foreground)
        GetWindowThreadProcessId(foreground, &processId);
    HandleProductionForegroundChanged(foreground, processId);
}

void GameSessionController::EnsureRenderVerification()
{
    if (Runtime().suspended || !Runtime().hudEnabled)
        return;
    const auto& context = gameDetectionCoordinator_.Context();
    const DWORD processId = context.candidateProcessId;
    const auto generation = context.generation;
    const bool committed = context.state == GameDetectionState::Committed;
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

    StopRenderVerification(L"target-handoff", false);
    Log(L"[GameDetection] verifier.start pid=" + std::to_wstring(processId) +
        L" gen=" + std::to_wstring(generation));
    const bool started = gameRenderVerifier_.Start(processId, generation,
        [this](const GameRenderVerifierEvent& event)
        {
            auto* update = new GameRenderVerifierUpdate{event};
            if (!PostMessageW(messageWindow_, kGameRenderVerifierUpdate,
                reinterpret_cast<WPARAM>(update), 0))
                delete update;
        });
    if (started)
        Log(L"[GameDetection] verifier.api2-ready pid=" + std::to_wstring(processId) +
            L" gen=" + std::to_wstring(generation));
    else
    {
        RuntimeLogger::Log(RuntimeLogLevel::Error,
            L"[GameDetection] verifier.start-failed pid=" +
            std::to_wstring(processId) + L" gen=" + std::to_wstring(generation));
        if (!committed)
            ReleaseProductionGameCandidate(L"verifier-start-failed");
    }
}

void GameSessionController::StartCandidateRenderVerification()
{
    EnsureRenderVerification();
}

void GameSessionController::StopRenderVerification(const wchar_t* reason,
    bool clearLatestFps)
{
    if (gameRenderVerifier_.ProcessId())
    {
        const DWORD processId = gameRenderVerifier_.ProcessId();
        Log(L"[GameDetection] verifier.stop pid=" +
            std::to_wstring(processId) + L" reason=" + reason);
        gameRenderVerifier_.Stop();
    }
    if (clearLatestFps)
        hooks_.stopFpsSampling();
}

void GameSessionController::ReleaseCommittedIfForegroundGone()
{
    const auto& context = gameDetectionCoordinator_.Context();
    if (context.state == GameDetectionState::Committed &&
        context.candidateProcessId &&
        !ProcessAlive(context.candidateProcessId))
        ReleaseCommittedProductionTarget(L"game-exited");
}

void GameSessionController::ClearCandidateIfNotCommitted(const wchar_t* reason)
{
    if (gameDetectionCoordinator_.Context().state != GameDetectionState::Committed)
        ClearProductionCandidate(reason);
}

// --- narrow queries ---------------------------------------------------

void GameSessionController::ReconcileForeground()
{
    foregroundTracker_.Reconcile();
}

DWORD GameSessionController::TrackedProcessId() const noexcept
{
    return foregroundTracker_.TrackedProcessId();
}

bool GameSessionController::ForegroundIsTrackedProcess() const noexcept
{
    return foregroundTracker_.ForegroundIsTrackedProcess();
}

DWORD GameSessionController::VerifierProcessId() const noexcept
{
    return gameRenderVerifier_.ProcessId();
}

std::uint64_t GameSessionController::VerifierGeneration() const noexcept
{
    return gameRenderVerifier_.Generation();
}

bool GameSessionController::VerifierRunning() const noexcept
{
    return gameRenderVerifier_.Running();
}

bool GameSessionController::CommittedProcessAliveOrNone() const
{
    const auto& context = gameDetectionCoordinator_.Context();
    if (context.state != GameDetectionState::Committed || !context.candidateProcessId)
        return true;
    return ProcessAlive(context.candidateProcessId);
}

// --- shutdown --------------------------------------------------------

void GameSessionController::StopSources()
{
    productionGameWindowSource_.Stop();
    productionProcessLifetimeWatcher_.Disarm();
    foregroundTracker_.Stop();
    StopRenderVerification(L"app-shutdown", true);
    steamRunningAppIdSource_.Stop();
    DiscardPendingEvents();
}

// --- production game detection (moved verbatim from App) ---------------

void GameSessionController::HandleProductionForegroundChanged(HWND window, DWORD processId)
{
    if (!ShouldConsiderForegroundProductionTarget(
        Runtime().hudEnabled, Runtime().suspended))
        return;
    if (TryCommitReadyCandidateFromForeground(window, processId))
        return;
    const auto& context = gameDetectionCoordinator_.Context();
    if (context.state == GameDetectionState::Committed)
    {
        if (ProcessAlive(context.candidateProcessId))
        {
            hooks_.ensureGraphicsApiProbe(context.candidateProcessId);
            EnsureRenderVerification();
            return;
        }
        ReleaseCommittedProductionTarget(L"game-exited");
    }
    if (context.state == GameDetectionState::Ready)
        return;
    const auto evidence = genericForegroundTrigger_.Inspect(window, processId);
    if (evidence)
        ApplyProductionEvidence(GameDetectionTrigger::GenericForeground,
            evidence->window, evidence->processId);
}

void GameSessionController::HandleMicrosoftGameEvidence(
    const MicrosoftGameTriggerEvidence& evidence)
{
    if (!ShouldConsiderForegroundProductionTarget(
        Runtime().hudEnabled, Runtime().suspended))
        return;
    if (!ProcessAlive(evidence.processId))
    {
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[GameDetection] microsoft.stale-evidence pid=" +
            std::to_wstring(evidence.processId));
        return;
    }
    ApplyProductionEvidence(GameDetectionTrigger::MicrosoftGameIdentity,
        evidence.window, evidence.processId);
}

void GameSessionController::HandleProductionWindowEvent(
    const ProductionWindowEvent& event)
{
    if (const auto evidence = microsoftGameTrigger_.InspectWindowEvent(event))
    {
        RuntimeLogger::Log(RuntimeLogLevel::Info,
            L"[GameDetection] microsoft.evidence pid=" +
            std::to_wstring(evidence->processId) + L" hwnd=" +
            HwndText(evidence->window) + L" sequence=" +
            std::to_wstring(evidence->sourceSequence));
        auto* update = new MicrosoftGameEvidenceUpdate{*evidence};
        if (!PostMessageW(messageWindow_, kMicrosoftGameEvidence,
            reinterpret_cast<WPARAM>(update), 0))
            delete update;
    }
    if (event.type != ProductionWindowEventType::Create &&
        event.type != ProductionWindowEventType::Show)
        return;
    if (!ShouldConsiderForegroundProductionTarget(
        Runtime().hudEnabled, Runtime().suspended))
        return;

    const auto& context = gameDetectionCoordinator_.Context();
    if (context.state != GameDetectionState::Armed ||
        context.steamAppId == 0 || !event.immediateTopLevel ||
        event.processId == 0 || event.processId == GetCurrentProcessId())
        return;
    if (!InspectProductionTargetProcess(event.processId) ||
        !ProcessAlive(event.processId))
        return;

    const DWORD previousProcessId = context.candidateProcessId;
    const auto previousGeneration = context.generation;
    const auto transition = gameDetectionCoordinator_.ObserveWake({
        GameDetectionTrigger::SteamRunningAppId,
        event.processId, event.window, context.steamAppId, false});
    if (transition.transition == GameDetectionTransition::None)
        return;

    const wchar_t* eventName = event.type ==
        ProductionWindowEventType::Create ? L"Create" : L"Show";
    RuntimeLogger::Log(RuntimeLogLevel::Debug,
        L"[GameDetection] steam.window-candidate pid=" +
        std::to_wstring(event.processId) + L" hwnd=" + HwndText(event.window) +
        L" appId=" + std::to_wstring(context.steamAppId) +
        L" event=" + eventName);
    HandleGameDetectionTransition(transition,
        GameDetectionTrigger::SteamRunningAppId,
        previousProcessId, previousGeneration);
}

void GameSessionController::ApplyProductionEvidence(GameDetectionTrigger trigger,
    HWND window, DWORD processId)
{
    const DWORD previousProcessId =
        gameDetectionCoordinator_.Context().candidateProcessId;
    const auto previousGeneration = gameDetectionCoordinator_.Context().generation;
    const auto disposition = DecideCandidateDisposition(
        gameDetectionCoordinator_.Context(), trigger, processId);
    if (disposition == CandidateDisposition::Ignore)
        return;
    GameDetectionTransitionResult transition;
    if (disposition == CandidateDisposition::Replace)
        transition = gameDetectionCoordinator_.ReplaceCandidate(processId, window, trigger);
    else if (trigger == GameDetectionTrigger::MicrosoftGameIdentity)
        transition = microsoftGameTrigger_.ApplyEvidence(
            gameDetectionCoordinator_, {0, window, processId});
    else
        transition = genericForegroundTrigger_.ApplyEvidence(
            gameDetectionCoordinator_, {window, processId});
    if (trigger == GameDetectionTrigger::GenericForeground)
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[GameDetection] generic.candidate pid=" +
            std::to_wstring(processId) + L" hwnd=" + HwndText(window));
    HandleGameDetectionTransition(
        transition, trigger, previousProcessId, previousGeneration);
}

void GameSessionController::HandleGameDetectionTransition(
    const GameDetectionTransitionResult& transition,
    GameDetectionTrigger trigger,
    DWORD previousProcessId, std::uint64_t previousGeneration)
{
    switch (transition.transition)
    {
    case GameDetectionTransition::Armed:
        Log(L"[GameDetection] transition old=Idle new=Armed "
            L"transition=Armed trigger=Steam appId=" +
            std::to_wstring(gameDetectionCoordinator_.Context().steamAppId));
        Log(L"[GameDetection] steam.armed appId=" +
            std::to_wstring(gameDetectionCoordinator_.Context().steamAppId));
        break;
    case GameDetectionTransition::CandidateStarted:
        ArmProductionProcessLifetime(transition.processId,
            transition.generation);
        hooks_.stopFpsSampling();
        Log(L"[GameDetection] candidate.start trigger=" +
            std::wstring(GameDetectionTriggerName(trigger)) +
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
    case GameDetectionTransition::CandidateUpdated:
    {
        const auto& context = gameDetectionCoordinator_.Context();
        const std::wstring state(GameDetectionStateName(context.state));
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[GameDetection] candidate.merge trigger=" +
            std::wstring(GameDetectionTriggerName(trigger)) +
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
    case GameDetectionTransition::CandidateReplaced:
        ArmProductionProcessLifetime(transition.processId,
            transition.generation);
        StopRenderVerification(L"candidate-replaced", true);
        hooks_.stopGraphicsApiProbe();
        Log(L"[GameDetection] candidate.replace oldPid=" +
            std::to_wstring(previousProcessId) + L" newPid=" +
            std::to_wstring(transition.processId) + L" trigger=" +
            std::wstring(GameDetectionTriggerName(trigger)) +
            L" oldGen=" + std::to_wstring(previousGeneration) +
            L" newGen=" + std::to_wstring(transition.generation));
        Log(L"[GameDetection] transition old=Verifying new=Verifying "
            L"transition=CandidateReplaced pid=" +
            std::to_wstring(transition.processId) + L" gen=" +
            std::to_wstring(transition.generation));
        StartCandidateRenderVerification();
        break;
    case GameDetectionTransition::RendererReady:
        Log(L"[GameDetection] transition old=Verifying new=Ready "
            L"transition=RendererReady pid=" +
            std::to_wstring(transition.processId) + L" gen=" +
            std::to_wstring(transition.generation));
        break;
    case GameDetectionTransition::Committed:
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
    case GameDetectionTransition::CandidateCleared:
        productionProcessLifetimeWatcher_.Disarm();
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[GameDetection] transition=CandidateCleared");
        break;
    case GameDetectionTransition::Reset:
        productionProcessLifetimeWatcher_.Disarm();
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[GameDetection] reset");
        break;
    case GameDetectionTransition::None:
        break;
    }
}

void GameSessionController::ArmProductionProcessLifetime(DWORD processId,
    std::uint64_t generation)
{
    const HWND messageWindow = messageWindow_;
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
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"[GameDetection] process-watch.arm-failed pid=" +
            std::to_wstring(processId) + L" gen=" +
            std::to_wstring(generation));
        return;
    }
    RuntimeLogger::Log(RuntimeLogLevel::Debug,
        L"[GameDetection] process-watch.arm pid=" +
        std::to_wstring(processId) + L" gen=" +
        std::to_wstring(generation));
}

void GameSessionController::HandleProductionProcessExit(DWORD processId,
    std::uint64_t generation)
{
    const auto& context = gameDetectionCoordinator_.Context();
    if (context.candidateProcessId != processId ||
        context.generation != generation)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"[GameDetection] process.exit-stale pid=" +
            std::to_wstring(processId) + L" gen=" +
            std::to_wstring(generation));
        return;
    }

    const auto action = DecideProductionProcessExit(
        context, processId, generation);
    if (action == ProductionProcessExitAction::Ignore)
        return;
    const auto state = std::wstring(GameDetectionStateName(context.state));
    Log(L"[GameDetection] process.exit pid=" + std::to_wstring(processId) +
        L" gen=" + std::to_wstring(generation) + L" state=" + state);
    if (action == ProductionProcessExitAction::ReleaseCommitted)
        ReleaseCommittedProductionTarget(L"game-exited");
    else
        ReleaseProductionGameCandidate(L"process-exited");
    const auto runtime = Runtime();
    if (runtime.hudEnabled && !runtime.suspended)
        ReevaluateForeground();
}

void GameSessionController::HandleGameRenderVerifierEvent(
    const GameRenderVerifierEvent& event)
{
    const auto& context = gameDetectionCoordinator_.Context();
    const auto runtime = Runtime();
    if (runtime.suspended || runtime.resumeRecoveryActive ||
        event.processId != context.candidateProcessId ||
        event.generation != context.generation)
        return;
    if (event.type != GameRenderVerifierEventType::FirstDisplayedFrame)
        return;
    if (!GameRenderVerifier::ApplyRendererEvidence(
        gameDetectionCoordinator_, event))
        return;
    Log(L"[GameDetection] renderer.first-frame pid=" +
        std::to_wstring(event.processId) + L" gen=" +
        std::to_wstring(event.generation));
    HandleGameDetectionTransition({
        GameDetectionTransition::RendererReady,
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

bool GameSessionController::TryCommitReadyCandidateFromForeground(HWND,
    DWORD foregroundProcessId)
{
    const auto& context = gameDetectionCoordinator_.Context();
    const auto runtime = Runtime();
    if (!ShouldCommitReadyCandidate(
            context, foregroundProcessId, ProcessAlive(context.candidateProcessId)) ||
        !runtime.hudEnabled || runtime.suspended)
        return false;
    const DWORD processId = context.candidateProcessId;
    const auto generation = context.generation;
    if (!gameDetectionCoordinator_.CommitCandidate(processId, generation))
        return false;
    foregroundTracker_.SetTrackedProcessId(processId);
    hooks_.startGraphicsApiProbe(processId);
    hooks_.setCommittedProcess(processId);
    hooks_.startProductionSampling();
    HandleGameDetectionTransition({
        GameDetectionTransition::Committed, generation, processId});
    hooks_.reconcileHudVisibility();
    return true;
}

void GameSessionController::ReleaseProductionGameCandidate(const wchar_t* reason)
{
    const auto& context = gameDetectionCoordinator_.Context();
    if (!context.candidateProcessId)
        return;
    StopRenderVerification(reason, true);
    hooks_.stopGraphicsApiProbeIfTarget(context.candidateProcessId);
    ClearProductionCandidate(reason);
}

void GameSessionController::ClearProductionCandidate(const wchar_t* reason)
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

void GameSessionController::ReleaseCommittedProductionTarget(const wchar_t* reason)
{
    const auto& context = gameDetectionCoordinator_.Context();
    const DWORD processId = context.candidateProcessId;
    const auto generation = context.generation;
    if (!processId)
        return;
    hooks_.clearCommittedProcess();
    const auto release = PlanCommittedTargetRelease();
    CommittedTargetReleaseOps ops;
    ops.stopRenderVerification = [this, reason]
    {
        StopRenderVerification(reason, true);
    };
    ops.stopGraphicsApiProbe = [this]
    {
        hooks_.stopGraphicsApiProbe();
    };
    ops.clearTrackedProcess = [this]
    {
        foregroundTracker_.SetTrackedProcessId(0);
    };
    ops.startGlobalTelemetry = [this]
    {
        hooks_.startProductionSampling();
    };
    ops.stopGlobalTelemetry = [this, reason]
    {
        hooks_.stopProductionSampling(false, reason);
    };
    ops.reconcileHudVisibility = [this]
    {
        hooks_.reconcileHudVisibility();
    };
    ClearProductionCandidate(L"game-exited");
    ApplyCommittedTargetReleasePlan(release, ops);
    Log(L"[GameDetection] released pid=" + std::to_wstring(processId) +
        L" gen=" + std::to_wstring(generation) + L" reason=" + reason);
}
}
