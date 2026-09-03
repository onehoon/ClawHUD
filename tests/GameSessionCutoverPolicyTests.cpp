#include "GameDetection/GameSessionCutoverPolicy.h"
#include "AlwaysModeFpsTarget.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}

clawhud::GameProcessInstance Process(DWORD pid, ULONGLONG creation)
{
    return {pid, creation};
}

clawhud::CurrentForegroundGame Game(clawhud::ForegroundGameDecision decision,
    DWORD pid = 0, ULONGLONG creation = 0)
{
    clawhud::CurrentForegroundGame result;
    result.decision = decision;
    result.processId = pid;
    if (decision == clawhud::ForegroundGameDecision::Eligible)
        result.process = Process(pid, creation);
    return result;
}

clawhud::ProductionWindowEvent WindowEvent(
    clawhud::ProductionWindowEventType type, HWND window, DWORD pid)
{
    clawhud::ProductionWindowEvent event;
    event.type = type;
    event.window = window;
    event.processId = pid;
    return event;
}

clawhud::CurrentForegroundGame Foreground(clawhud::ForegroundGameDecision decision,
    clawhud::GameScreenAdmissionReason reason, HWND window, DWORD pid)
{
    clawhud::CurrentForegroundGame result;
    result.decision = decision;
    result.admissionReason = reason;
    result.window = window;
    result.processId = pid;
    return result;
}
}

int main()
{
    using namespace clawhud;
    const auto gameA = Process(100, 10);
    const auto gameB = Process(200, 20);

    Check(PlanForegroundGameTargetAction(gameA, Game(ForegroundGameDecision::NeedsRendererVerification, 200)) ==
        ForegroundGameTargetAction::Clear,
        "an admitted unknown foreground clears the old eligible target before verification");
    Check(PlanForegroundGameTargetAction(gameA, Game(ForegroundGameDecision::Hidden)) ==
        ForegroundGameTargetAction::Clear,
        "a hidden foreground (Explorer) clears the In-Game target immediately");
    Check(PlanForegroundGameTargetAction(gameA, Game(ForegroundGameDecision::Eligible, 200, 20)) ==
        ForegroundGameTargetAction::SetEligible,
        "a new known foreground retargets directly while the old game remains alive");
    Check(PlanForegroundGameTargetAction(gameB, Game(ForegroundGameDecision::Eligible, 200, 20)) ==
        ForegroundGameTargetAction::None,
        "a redundant eligible re-evaluation for the same generation is not a destructive reset");
    Check(PlanForegroundGameTargetAction(std::nullopt, Game(ForegroundGameDecision::Hidden)) ==
        ForegroundGameTargetAction::None,
        "no current target plus a hidden foreground is a no-op");
    Check(PlanForegroundGameTargetAction(Process(200, 20),
        Game(ForegroundGameDecision::Eligible, 200, 21)) ==
        ForegroundGameTargetAction::SetEligible,
        "PID reuse with a different creation time is treated as a target change");

    // End-to-end PID-reuse -> InGameOnly FPS invalidation contract: prior
    // target PID 5000/creation A, Windows reuses PID 5000 for a new process
    // generation B. The exact-generation transition must still be SetEligible
    // (never collapsed by numeric-PID equality), and while InGameOnly is
    // active every SetEligible must invalidate the shared FPS state -
    // ProductionTelemetryController::SetInGameForegroundProcess must never
    // gate that invalidation on the numeric PID being unchanged.
    Check(PlanForegroundGameTargetAction(Process(5000, 10),
        Game(ForegroundGameDecision::Eligible, 5000, 11)) ==
        ForegroundGameTargetAction::SetEligible &&
        InGameTargetChangeInvalidatesFps(HudVisibilityMode::InGameOnly),
        "PID reuse on the same numeric PID is a SetEligible transition that invalidates InGameOnly FPS");

    const HWND minecraft = reinterpret_cast<HWND>(0x1000);
    const HWND other = reinterpret_cast<HWND>(0x2000);
    Check(WindowEventAffectsCurrentScreen(
        WindowEvent(ProductionWindowEventType::LocationChange, minecraft, 100),
        minecraft, 100, minecraft, 100),
        "current LOCATIONCHANGE requests a fresh admission evaluation");
    Check(WindowEventAffectsCurrentScreen(
        WindowEvent(ProductionWindowEventType::Hide, minecraft, 100),
        other, 300, minecraft, 100),
        "HIDE of the last current window requests a fresh evaluation");
    Check(WindowEventAffectsCurrentScreen(
        WindowEvent(ProductionWindowEventType::Destroy, minecraft, 100),
        other, 300, minecraft, 100),
        "DESTROY of the last current window requests a fresh evaluation");
    Check(!WindowEventAffectsCurrentScreen(
        WindowEvent(ProductionWindowEventType::Create, minecraft, 100),
        minecraft, 100, minecraft, 100),
        "CREATE is discovery-only and does not select a target");

    const RendererVerificationRequest requestA{1, gameA};
    const RendererVerificationRequest requestB{2, gameB};
    Check(!ShouldStartRendererVerification(requestA, true, requestA) &&
        ShouldStartRendererVerification(requestA, true, requestB),
        "repeated requests are deduplicated while a distinct foreground request hands off");

    // An already-posted request-A completion consumed after the verifier worker
    // was handed off to request B: A's exact evidence is still applied by the
    // caller, but the active B request must not be cleared or replaced.
    Check(!RendererCompletionClearsActiveRequest(requestB, requestA),
        "a stale already-posted completion never clears the active verifier request");
    Check(RendererCompletionClearsActiveRequest(requestA, requestA),
        "the matching active request is cleared on its own completion");
    Check(!RendererCompletionClearsActiveRequest(std::nullopt, requestA),
        "a completion with no active request clears nothing");
    Check(ProcessInstanceStillMatches(gameA, gameA) &&
        !ProcessInstanceStillMatches(gameA, Process(100, 11)),
        "PID generation reuse invalidates the old current-foreground-game target");

    // --- redundant excluded-foreground LOCATIONCHANGE suppression -----------
    // ClawHUD.Settings.exe: classified Hidden/ExcludedExecutable, and it is both
    // the authoritative current foreground evaluation and the live foreground.
    const HWND settings = reinterpret_cast<HWND>(0x3000);
    constexpr DWORD settingsPid = 900;
    const auto excluded = Foreground(ForegroundGameDecision::Hidden,
        GameScreenAdmissionReason::ExcludedExecutable, settings, settingsPid);

    Check(WindowEventIsRedundantExcludedForegroundLocationChange(
        WindowEvent(ProductionWindowEventType::LocationChange, settings, settingsPid),
        excluded, settings, settingsPid),
        "same-foreground ExcludedExecutable LOCATIONCHANGE is redundant");

    for (auto type : {ProductionWindowEventType::Show, ProductionWindowEventType::Hide,
        ProductionWindowEventType::Destroy})
        Check(!WindowEventIsRedundantExcludedForegroundLocationChange(
            WindowEvent(type, settings, settingsPid), excluded, settings, settingsPid),
            "SHOW/HIDE/DESTROY for an excluded executable is never suppressed");

    Check(!WindowEventIsRedundantExcludedForegroundLocationChange(
        WindowEvent(ProductionWindowEventType::LocationChange, settings, settingsPid),
        Foreground(ForegroundGameDecision::Hidden,
            GameScreenAdmissionReason::NotFullscreenLike, settings, settingsPid),
        settings, settingsPid),
        "NotFullscreenLike LOCATIONCHANGE still reevaluates (windowed -> fullscreen)");

    Check(!WindowEventIsRedundantExcludedForegroundLocationChange(
        WindowEvent(ProductionWindowEventType::LocationChange, settings, settingsPid),
        Foreground(ForegroundGameDecision::NeedsRendererVerification,
            GameScreenAdmissionReason::Admitted, settings, settingsPid),
        settings, settingsPid),
        "a renderer-verification candidate LOCATIONCHANGE still reevaluates");

    Check(!WindowEventIsRedundantExcludedForegroundLocationChange(
        WindowEvent(ProductionWindowEventType::LocationChange, settings, settingsPid),
        excluded, other, 300),
        "an excluded evaluation that is no longer the live foreground still reevaluates");

    Check(!WindowEventIsRedundantExcludedForegroundLocationChange(
        WindowEvent(ProductionWindowEventType::LocationChange, other, 300),
        excluded, settings, settingsPid),
        "LOCATIONCHANGE for an unrelated window is not the suppression case");

    // The real controller call sequence: the redundant case short-circuits to
    // "does not affect current screen", every other case falls through to the
    // existing WindowEventAffectsCurrentScreen contract.
    Check(WindowEventAffectsCurrentScreen(
        WindowEvent(ProductionWindowEventType::LocationChange, settings, settingsPid),
        settings, settingsPid, settings, settingsPid),
        "without the exclusion optimization a current LOCATIONCHANGE still counts");

    std::cout << "PASS\n";
}
