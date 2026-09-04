#include "GameSessionController.h"

#include "../RuntimeLogger.h"
#include "../Win32Format.h"

#include <string>

namespace clawhud
{
namespace
{
// Production game-session WM_APP ids. Numeric values are shared with the
// application message window and stay distinct from the telemetry timer ids
// (WM_APP + 1 is unused).
constexpr UINT kForegroundChanged = WM_APP + 2;
constexpr UINT kSteamRunningAppIdChanged = WM_APP + 5;
constexpr UINT kMicrosoftGameEvidence = WM_APP + 6;
constexpr UINT kGameRenderVerifierUpdate = WM_APP + 7;
constexpr UINT kProductionWindowEvent = WM_APP + 8;

struct MicrosoftGameEvidenceUpdate
{
    MicrosoftGameTriggerEvidence evidence;
};

struct ProductionWindowEventUpdate
{
    ProductionWindowEvent event;
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
            HandleGameRenderVerifierUpdate(update->request, update->type);
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

void GameSessionController::RevalidateCurrentForegroundGame()
{
    if (currentForegroundGameProcess_)
    {
        const auto current = QueryGameProcessInstance(
            currentForegroundGameProcess_->processId);
        if (!current || *current != *currentForegroundGameProcess_)
            EvaluateCurrentForeground(L"current-game-process-changed");
    }
}

void GameSessionController::ResetForegroundGameSession(const wchar_t* reason)
{
    StopRenderVerification(reason, true);
    currentForegroundGameProcess_.reset();
    hooks_.clearInGameForegroundProcess();
    hooks_.reconcileHudVisibility();
}

// --- narrow queries ---------------------------------------------------

void GameSessionController::ReconcileForeground()
{
    foregroundTracker_.Reconcile();
}

bool GameSessionController::CurrentForegroundGameActive() const noexcept
{
    return currentForegroundGameProcess_.has_value();
}

DWORD GameSessionController::CurrentForegroundGameProcessId() const noexcept
{
    return currentForegroundGameProcess_
        ? currentForegroundGameProcess_->processId
        : 0;
}

DWORD GameSessionController::VerifierProcessId() const noexcept
{
    return gameRenderVerifier_.ProcessId();
}

bool GameSessionController::VerifierRunning() const noexcept
{
    return gameRenderVerifier_.Running();
}

// --- shutdown --------------------------------------------------------

void GameSessionController::StopSources()
{
    productionGameWindowSource_.Stop();
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
    if (WindowEventIsRedundantExcludedForegroundLocationChange(
            event, current, foreground, foregroundPid))
        return false;
    return WindowEventAffectsCurrentScreen(event, foreground, foregroundPid,
        current.window, current.processId);
}

void GameSessionController::ApplyForegroundEvaluation(
    const ForegroundGameEvaluation& evaluation, const wchar_t* reason)
{
    const auto& current = evaluation.current;
    const auto action = PlanForegroundGameTargetAction(currentForegroundGameProcess_, current);
    if (action == ForegroundGameTargetAction::SetEligible)
    {
        const DWORD processId = current.process->processId;
        currentForegroundGameProcess_ = current.process;
        hooks_.setInGameForegroundProcess(processId);
        hooks_.startProductionSampling();
        Log(L"[GameDetection] foreground.target-set pid=" + std::to_wstring(processId));
        hooks_.reconcileHudVisibility();
    }
    if (current.decision == ForegroundGameDecision::Eligible)
    {
        if (activeRendererRequest_)
            StopRenderVerification(L"eligible", false);
        return;
    }

    if (action == ForegroundGameTargetAction::Clear)
    {
        Log(L"[GameDetection] foreground.target-clear oldPid=" +
            std::to_wstring(currentForegroundGameProcess_->processId) + L" reason=" + reason);
        currentForegroundGameProcess_.reset();
        hooks_.clearInGameForegroundProcess();
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
    // Every accepted production window event first wakes the canonical
    // foreground authority: ForegroundTracker::Reconcile() re-reads
    // GetForegroundWindow()/PID and fires its callback only on an actual change,
    // closing the gap where EVENT_SYSTEM_FOREGROUND was missed (a game loader
    // window that swaps content without a foreground transition, a Ghost window
    // handoff). This is cheap when nothing changed.
    foregroundTracker_.Reconcile();
    if (WindowEventAffectsCurrentForeground(event) &&
        ShouldReevaluateOnNameChange(nameChangeDebounce_, event,
            event.receivedTickMs))
        EvaluateCurrentForeground(L"window-event");
}

void GameSessionController::HandleGameRenderVerifierUpdate(
    const RendererVerificationRequest& request, GameRenderVerifierEventType type)
{
    if (type != GameRenderVerifierEventType::FirstDisplayedFrame)
        return;
    // A trusted already-posted completion always contributes its exact
    // process-generation renderer evidence, even after the verifier worker has
    // been handed off to a newer foreground request. R3/R2 stop an old
    // generation from overwriting newer evidence for the same numeric PID
    // (PR #202). Only the matching active adapter request may be cleared: a
    // stale completion must never disturb a newer active verification.
    foregroundGameDetector_.CompleteRendererVerification({request, true});
    if (RendererCompletionClearsActiveRequest(activeRendererRequest_, request))
        activeRendererRequest_.reset();
    Log(L"[GameDetection] renderer.first-frame pid=" +
        std::to_wstring(request.process.processId) + L" requestId=" +
        std::to_wstring(request.requestId));
    const auto runtime = Runtime();
    if (!runtime.suspended && !runtime.resumeRecoveryActive)
        EvaluateCurrentForeground(L"renderer-completion");
}
}
