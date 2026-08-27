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
    ok &= Check(!VrrDiagnosticShouldForceTerminatePresentMon(false, false),
        "successful diagnostic lets PresentMon finish its timed capture");
    ok &= Check(VrrDiagnosticShouldForceTerminatePresentMon(true, false),
        "cancelled diagnostic force-stops PresentMon");
    ok &= Check(VrrDiagnosticShouldForceTerminatePresentMon(false, true),
        "failed diagnostic may stop PresentMon early");
    return ok ? 0 : 1;
}
