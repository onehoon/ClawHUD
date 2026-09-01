#include "GameDetection/GameSessionCutoverPolicy.h"

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
}

int main()
{
    using namespace clawhud;
    const auto gameA = Process(100, 10);
    const auto gameB = Process(200, 20);

    Check(PlanCompatibilityTargetAction(gameA, Game(ForegroundGameDecision::NeedsRendererVerification, 200)) ==
        CompatibilityTargetAction::Clear,
        "an admitted unknown foreground clears the old eligible target before verification");
    Check(PlanCompatibilityTargetAction(gameA, Game(ForegroundGameDecision::Hidden)) ==
        CompatibilityTargetAction::Clear,
        "a hidden foreground clears the compatibility target immediately");
    Check(PlanCompatibilityTargetAction(gameA, Game(ForegroundGameDecision::Eligible, 200, 20)) ==
        CompatibilityTargetAction::SetEligible,
        "a new known foreground retargets directly while the old game remains alive");
    Check(PlanCompatibilityTargetAction(gameB, Game(ForegroundGameDecision::Eligible, 200, 20)) ==
        CompatibilityTargetAction::None,
        "repeated eligible evaluations are idempotent");

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
    Check(ProcessInstanceStillMatches(gameA, gameA) &&
        !ProcessInstanceStillMatches(gameA, Process(100, 11)),
        "PID generation reuse invalidates the old compatibility target");

    std::cout << "PASS\n";
}
