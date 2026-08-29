#include "VrrDiagnostic.h"

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
    ok &= Check(!DiagnosticHudModeUsesPeriodicUpdates(DiagnosticHudMode::Off), "OFF stops periodic updates");
    ok &= Check(!DiagnosticHudModeUsesPeriodicUpdates(DiagnosticHudMode::Static), "STATIC stops periodic updates");
    ok &= Check(DiagnosticHudModeUsesPeriodicUpdates(DiagnosticHudMode::Dynamic), "DYNAMIC enables periodic updates");
    ok &= Check(kDiagnosticMockHudTimerIntervalMs == 500,
        "DYNAMIC diagnostic updates use a 500 ms interval");
    ok &= Check(VrrDiagnosticStopReportsCancellation(VrrDiagnosticState::WaitingForTrigger),
        "waiting-for-trigger stop reports cancellation");
    ok &= Check(!VrrDiagnosticStopReportsCancellation(VrrDiagnosticState::Running),
        "running stop uses phase cancellation status");
    ok &= Check(!VrrDiagnosticCanWaitForF8(false),
        "VRR diagnostic does not wait when F8 is unavailable");
    ok &= Check(VrrDiagnosticCanWaitForF8(true),
        "VRR diagnostic waits when F8 is registered");
    ok &= Check(VrrDiagnosticCompletionSoundAllowed(true, true) &&
        !VrrDiagnosticCompletionSoundAllowed(true, false) &&
        !VrrDiagnosticCompletionSoundAllowed(false, true),
        "completion sound requires successful phases and HUD restoration");
    ok &= Check(VrrDiagnosticStatusRequiresForegroundReevaluation(L"Passed") &&
        VrrDiagnosticStatusRequiresForegroundReevaluation(L"Inconclusive") &&
        VrrDiagnosticStatusRequiresForegroundReevaluation(L"Failed") &&
        !VrrDiagnosticStatusRequiresForegroundReevaluation(L"Cancelled") &&
        !VrrDiagnosticStatusRequiresForegroundReevaluation(L"Running"),
        "completed VRR diagnostics re-evaluate the current foreground");
    return ok ? 0 : 1;
}
