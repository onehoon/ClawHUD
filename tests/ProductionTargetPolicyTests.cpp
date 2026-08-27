#include "ProductionTargetPolicy.h"

#include <iostream>

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool ok = true;
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"steam.exe"),
        "Steam launcher is not a production target");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"steamwebhelper.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamebar.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamebarftserver.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"xboxpcappft.exe"),
        "Steam and Windows gaming shells are not production targets");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"explorer.exe"),
        "Explorer is not a production target");
    ok &= Check(!clawhud::IsRejectedProductionTargetImage(L"game.exe"),
        "game process remains an eligible candidate");
    ok &= Check(!clawhud::ShouldEvaluateForegroundCandidate(100, 0) &&
        !clawhud::ShouldEvaluateForegroundCandidate(100, 200) &&
        !clawhud::ShouldEvaluateForegroundCandidate(100, 100) &&
        clawhud::ShouldEvaluateForegroundCandidate(0, 200),
        "foreground churn does not replace a live committed PID");
    ok &= Check(clawhud::ShouldReplacePendingCandidate(100, 200) &&
        !clawhud::ShouldReplacePendingCandidate(100, 100) &&
        !clawhud::ShouldReplacePendingCandidate(100, 0),
        "foreground B replaces pending candidate A");
    ok &= Check(clawhud::SelectProductionSamplingProcess(100, 200) == 100 &&
        clawhud::SelectProductionSamplingProcess(0, 200) == 200,
        "committed target is sampled before a pending candidate");
    ok &= Check(clawhud::ShouldSampleProductionPresentMon(100, 0, true) &&
        clawhud::ShouldSampleProductionPresentMon(0, 200, false) &&
        !clawhud::ShouldSampleProductionPresentMon(100, 0, false) &&
        clawhud::ShouldRetainCommittedProductionTarget(100, true) &&
        !clawhud::ShouldRetainCommittedProductionTarget(100, false),
        "committed target sampling follows process lifetime, not foreground");
    ok &= Check(clawhud::ShouldPreservePendingProductionValidation(200, 200, true) &&
        !clawhud::ShouldPreservePendingProductionValidation(200, 100, true) &&
        !clawhud::ShouldPreservePendingProductionValidation(200, 200, false),
        "hidden HUD preserves only the active pending PresentMon validation");
    ok &= Check(clawhud::ShouldDeferPendingProductionValidation(200, 0, true) &&
        !clawhud::ShouldDeferPendingProductionValidation(200, 0, false) &&
        !clawhud::ShouldDeferPendingProductionValidation(200, 100, true),
        "transient foreground loss defers a live pending candidate");
    ok &= Check(clawhud::ShouldRetryProductionPresentMon(0, 0, 100) &&
        clawhud::ShouldRetryProductionPresentMon(100, 1, 200) &&
        !clawhud::ShouldRetryProductionPresentMon(100, 1, 100),
        "PresentMon recovery is limited to one retry per PID");
    ok &= Check(clawhud::ShouldReevaluateForegroundAfterResume(true, true) &&
        !clawhud::ShouldReevaluateForegroundAfterResume(false, true) &&
        !clawhud::ShouldReevaluateForegroundAfterResume(true, false),
        "completed resume recovery re-adopts the current foreground");
    ok &= Check(clawhud::ShouldReevaluateForegroundAfterDiagnostic(true, false, false) &&
        !clawhud::ShouldReevaluateForegroundAfterDiagnostic(true, true, false) &&
        !clawhud::ShouldReevaluateForegroundAfterDiagnostic(true, false, true) &&
        !clawhud::ShouldReevaluateForegroundAfterDiagnostic(false, false, false),
        "diagnostic stop re-adopts only when production lifecycle is available");
    ok &= Check(clawhud::ShouldCancelPendingCandidateOnCommittedReturn(100, 200, 100) &&
        !clawhud::ShouldCancelPendingCandidateOnCommittedReturn(100, 100, 100) &&
        !clawhud::ShouldCancelPendingCandidateOnCommittedReturn(100, 200, 200),
        "return to committed target cancels a different pending candidate");
    ok &= Check(clawhud::ShouldRestartGraphicsApiProbe(0, 100) &&
        clawhud::ShouldRestartGraphicsApiProbe(200, 100) &&
        !clawhud::ShouldRestartGraphicsApiProbe(100, 100) &&
        !clawhud::ShouldRestartGraphicsApiProbe(0, 0),
        "committed target re-entry restarts a missing or stale graphics API probe");
    ok &= Check(!clawhud::ShouldConfirmProductionTarget(42, 43, 43, true) &&
        !clawhud::ShouldConfirmProductionTarget(42, 42, 43, true) &&
        !clawhud::ShouldConfirmProductionTarget(42, 42, 42, false) &&
        clawhud::ShouldConfirmProductionTarget(42, 42, 42, true),
        "only current foreground candidate FPS confirms the target");
    ok &= Check(!clawhud::ShouldConsiderForegroundProductionTarget(true, true, false) &&
        !clawhud::ShouldConsiderForegroundProductionTarget(true, false, true) &&
        clawhud::ShouldConsiderForegroundProductionTarget(true, false, false),
        "diagnostic and suspend states block adoption");
    return ok ? 0 : 1;
}
