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
    RendererVerificationRequest request;
    GameRenderVerifierEventType type{};
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
    foregroundGameDetector_.UpdateSteamSession(steamRunningAppId_);
    if (steamRunningAppId_ != 0)
        Log(L"[GameDetection] steam.session oldAppId=0 newAppId=" +
            std::to_wstring(steamRunningAppId_));
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
            HandleGameRenderVerifierEvent({update->request.process.processId,
                update->request.requestId, update->type});
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
    foregroundGameDetector_.UpdateSteamSession(current);
    Log(L"[GameDetection] steam.session oldAppId=" +
        std::to_wstring(previous) + L" newAppId=" +
        std::to_wstring(current));
    EvaluateCurrentForeground(L"steam-session");
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
    EvaluateCurrentForeground(L"reevaluate");
}

void GameSessionController::EnsureRenderVerification()
{
    if (Runtime().suspended || !Runtime().hudEnabled)
        return;
    const auto& current = foregroundGameDetector_.Current();
    if (current.decision != ForegroundGameDecision::NeedsRendererVerification ||
        !current.process)
        return;
    const auto evaluation = foregroundGameDetector_.Evaluate(
        ObserveGameScreen(current.window, current.processId));
    ApplyForegroundEvaluation(evaluation, L"ensure-verifier");
}

void GameSessionController::StartCandidateRenderVerification()
{
    EnsureRenderVerification();
}

void GameSessionController::StopRenderVerification(const wchar_t* reason,
    bool clearLatestFps)
{
    const auto request = activeRendererRequest_;
    if (gameRenderVerifier_.ProcessId())
    {
        Log(L"[GameDetection] verifier.stop pid=" +
            std::to_wstring(gameRenderVerifier_.ProcessId()) + L" reason=" + reason);
        gameRenderVerifier_.Stop();
    }
    activeRendererRequest_.reset();
    if (request)
        foregroundGameDetector_.CompleteRendererVerification({*request, false});
    if (clearLatestFps)
        hooks_.stopFpsSampling();
}

void GameSessionController::ReleaseCommittedIfForegroundGone()
{
    if (bridgedEligibleProcess_)
    {
        const auto current = QueryGameProcessInstance(
            bridgedEligibleProcess_->processId);
        if (!current || *current != *bridgedEligibleProcess_)
            EvaluateCurrentForeground(L"eligible-process-changed");
    }
}

void GameSessionController::ClearCandidateIfNotCommitted(const wchar_t* reason)
{
    StopRenderVerification(reason, true);
    if (bridgedEligibleProcess_)
        hooks_.stopGraphicsApiProbeIfTarget(bridgedEligibleProcess_->processId);
    bridgedEligibleProcess_.reset();
    foregroundTracker_.SetTrackedProcessId(0);
    hooks_.clearCommittedProcess();
    hooks_.reconcileHudVisibility();
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
    if (!bridgedEligibleProcess_)
        return true;
    const auto current = QueryGameProcessInstance(
        bridgedEligibleProcess_->processId);
    return ProcessInstanceStillMatches(bridgedEligibleProcess_, current);
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

void GameSessionController::EvaluateCurrentForeground(const wchar_t* reason)
{
    const auto runtime = Runtime();
    if (!runtime.hudEnabled || runtime.suspended)
        return;
    const HWND window = GetForegroundWindow();
    DWORD processId{};
    if (window)
        GetWindowThreadProcessId(window, &processId);
    RuntimeLogger::Log(RuntimeLogLevel::Debug, L"[GameDetection] foreground.evaluate reason=" +
        std::wstring(reason) + L" hwnd=" + HwndText(window) + L" pid=" + std::to_wstring(processId));
    ApplyForegroundEvaluation(foregroundGameDetector_.Evaluate(
        ObserveGameScreen(window, processId)), reason);
}

bool GameSessionController::WindowEventAffectsCurrentForeground(
    const ProductionWindowEvent& event) const
{
    const auto& current = foregroundGameDetector_.Current();
    const HWND foreground = GetForegroundWindow();
    DWORD foregroundPid{};
    if (foreground) GetWindowThreadProcessId(foreground, &foregroundPid);
    return WindowEventAffectsCurrentScreen(event, foreground, foregroundPid,
        current.window, current.processId);
}

void GameSessionController::ApplyForegroundEvaluation(
    const ForegroundGameEvaluation& evaluation, const wchar_t* reason)
{
    const auto& current = evaluation.current;
    const auto action = PlanCompatibilityTargetAction(bridgedEligibleProcess_, current);
    if (action == CompatibilityTargetAction::SetEligible)
    {
        const DWORD processId = current.process->processId;
        if (bridgedEligibleProcess_)
            hooks_.stopGraphicsApiProbeIfTarget(bridgedEligibleProcess_->processId);
        bridgedEligibleProcess_ = current.process;
        foregroundTracker_.SetTrackedProcessId(processId);
        hooks_.setCommittedProcess(processId);
        hooks_.startGraphicsApiProbe(processId);
        hooks_.startProductionSampling();
        Log(L"[GameDetection] foreground.eligible pid=" + std::to_wstring(processId));
        hooks_.reconcileHudVisibility();
    }
    if (current.decision == ForegroundGameDecision::Eligible)
    {
        if (activeRendererRequest_)
            StopRenderVerification(L"eligible", false);
        return;
    }

    if (action == CompatibilityTargetAction::Clear)
    {
        Log(L"[GameDetection] foreground.clear oldPid=" +
            std::to_wstring(bridgedEligibleProcess_->processId) + L" reason=" + reason);
        hooks_.stopGraphicsApiProbeIfTarget(bridgedEligibleProcess_->processId);
        bridgedEligibleProcess_.reset();
        foregroundTracker_.SetTrackedProcessId(0);
        hooks_.clearCommittedProcess();
        hooks_.reconcileHudVisibility();
    }
    if (current.decision == ForegroundGameDecision::Hidden)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Debug, L"[GameDetection] foreground.hidden pid=" +
            std::to_wstring(current.processId) + L" reason=" +
            std::to_wstring(static_cast<int>(current.admissionReason)));
        return;
    }

    const auto request = evaluation.verificationRequest;
    if (!ShouldStartRendererVerification(activeRendererRequest_,
        gameRenderVerifier_.Running(), request))
        return;
    if (activeRendererRequest_)
        StopRenderVerification(L"foreground-handoff", false);
    activeRendererRequest_ = request;
    const bool started = gameRenderVerifier_.Start(request->process.processId,
        request->requestId, [this, request = *request](const GameRenderVerifierEvent& event)
        {
            if (event.processId != request.process.processId || event.generation != request.requestId)
                return;
            auto* update = new GameRenderVerifierUpdate{request, event.type};
            if (!PostMessageW(messageWindow_, kGameRenderVerifierUpdate,
                reinterpret_cast<WPARAM>(update), 0)) delete update;
        });
    if (!started)
    {
        foregroundGameDetector_.CompleteRendererVerification({*request, false});
        activeRendererRequest_.reset();
        RuntimeLogger::Log(RuntimeLogLevel::Error, L"[GameDetection] verifier.start-failed pid=" +
            std::to_wstring(request->process.processId));
    }
}

// --- production game detection ------------------------------------------

void GameSessionController::HandleProductionForegroundChanged(HWND window, DWORD processId)
{
    EvaluateCurrentForeground(L"foreground");
}

void GameSessionController::HandleMicrosoftGameEvidence(
    const MicrosoftGameTriggerEvidence& evidence)
{
    EvaluateCurrentForeground(L"microsoft-evidence");
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
    if (WindowEventAffectsCurrentForeground(event))
        EvaluateCurrentForeground(L"window-event");
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
    const auto runtime = Runtime();
    if (!activeRendererRequest_ || event.type != GameRenderVerifierEventType::FirstDisplayedFrame ||
        event.processId != activeRendererRequest_->process.processId ||
        event.generation != activeRendererRequest_->requestId)
        return;
    const auto request = *activeRendererRequest_;
    activeRendererRequest_.reset();
    foregroundGameDetector_.CompleteRendererVerification({request, true});
    Log(L"[GameDetection] renderer.first-frame pid=" +
        std::to_wstring(event.processId) + L" requestId=" +
        std::to_wstring(event.generation));
    if (!runtime.suspended && !runtime.resumeRecoveryActive)
        EvaluateCurrentForeground(L"renderer-completion");
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
