#include "ProductionTargetPolicy.h"
#include "HudModel.h"

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
    const auto release = clawhud::PlanCommittedTargetRelease();
    ok &= Check(release.stopPresentMon && release.stopGraphicsApiProbe &&
        release.clearTrackedProcess && release.reconcileHudVisibility,
        "committed target release clears only game-scoped state and reconciles visibility");
    ok &= Check(release.globalTelemetry == clawhud::GlobalTelemetryAction::Keep,
        "committed target release does not own global telemetry");
    int globalStarts = 0;
    int globalStops = 0;
    int reconciles = 0;
    clawhud::CommittedTargetReleaseOps ops;
    ops.stopPresentMon = [] {};
    ops.stopGraphicsApiProbe = [] {};
    ops.clearTrackedProcess = [] {};
    ops.startGlobalTelemetry = [&] { ++globalStarts; };
    ops.stopGlobalTelemetry = [&] { ++globalStops; };
    ops.reconcileHudVisibility = [&] { ++reconciles; };
    clawhud::ApplyCommittedTargetReleasePlan(release, ops);
    ok &= Check(globalStarts == 0 && globalStops == 0,
        "committed target release never tears down or starts global telemetry");
    ok &= Check(reconciles == 1,
        "committed target release delegates global telemetry lifetime to visibility");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"steam.exe"),
        "Steam launcher is not a production target");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"steamwebhelper.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamebar.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamebarftserver.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"xboxpcappft.exe"),
        "Steam and Windows gaming shells are not production targets");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"explorer.exe"),
        "Explorer is not a production target");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(
        L"steaminputaddonforclaw.ui.exe"),
        "Steam Input Addon UI is not a production game target");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"msi center m.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"msi_center_m_launcher.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"msi_center_m_server.exe") &&
        clawhud::IsRejectedProductionTargetImage(
            L"msi_center_m_server_controlmode.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"command center.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamebar_widget.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"mcmosdinfo.exe"),
        "MSI Center M companion processes are not production targets");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"gameoverlayui.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"steamservice.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"steamerrorreporter.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"steamerrorreporter64.exe"),
        "Steam service, overlay, and error reporters are not production targets");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"xboxpcapp.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"xboxpctray.exe") &&
        clawhud::IsRejectedProductionTargetImage(
            L"xboxgamebarwidgets.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamingservicesnet.exe"),
        "Xbox and Gaming Services infrastructure are not production targets");
    ok &= Check(!clawhud::IsRejectedProductionTargetImage(L"msedgewebview2.exe"),
        "shared WebView2 runtime is not globally rejected");
    ok &= Check(clawhud::IsEligibleProductionTargetImage(L"game.exe") &&
        clawhud::IsEligibleProductionTargetImage(L"C:\\Games\\DaveTheDiver.EXE") &&
        clawhud::IsEligibleProductionTargetImage(
            L"C:\\Games\\Game-Win64-Shipping.exe"),
        "normal game images remain eligible after normalization");
    ok &= Check(clawhud::ShouldRetainCommittedProductionTarget(100, true) &&
        !clawhud::ShouldRetainCommittedProductionTarget(100, false),
        "committed target sampling follows process lifetime, not foreground");
    ok &= Check(clawhud::ShouldRetryProductionPresentMon(0, 0, 100) &&
        clawhud::ShouldRetryProductionPresentMon(100, 1, 200) &&
        !clawhud::ShouldRetryProductionPresentMon(100, 1, 100),
        "PresentMon recovery is limited to one retry per PID");
    ok &= Check(clawhud::ShouldAllowProductionPresentMonStart(
        100, 100, 100, 1, true) &&
        !clawhud::ShouldAllowProductionPresentMonStart(
            100, 100, 100, 1, false) &&
        clawhud::ShouldAllowProductionPresentMonStart(
            100, 100, 100, 0, false),
        "explicit recovery consumes exactly one retry before normal starts are blocked");
    ok &= Check(clawhud::ShouldReevaluateForegroundAfterResume(true, true) &&
        !clawhud::ShouldReevaluateForegroundAfterResume(false, true) &&
        !clawhud::ShouldReevaluateForegroundAfterResume(true, false),
        "completed resume recovery re-adopts the current foreground");
    ok &= Check(clawhud::ShouldReevaluateForegroundAfterDiagnostic(true, false, false) &&
        !clawhud::ShouldReevaluateForegroundAfterDiagnostic(true, true, false) &&
        !clawhud::ShouldReevaluateForegroundAfterDiagnostic(true, false, true) &&
        !clawhud::ShouldReevaluateForegroundAfterDiagnostic(false, false, false),
        "diagnostic stop re-adopts only when production lifecycle is available");
    ok &= Check(clawhud::ShouldRestartGraphicsApiProbe(0, 100) &&
        clawhud::ShouldRestartGraphicsApiProbe(200, 100) &&
        !clawhud::ShouldRestartGraphicsApiProbe(100, 100) &&
        !clawhud::ShouldRestartGraphicsApiProbe(0, 0),
        "committed target re-entry restarts a missing or stale graphics API probe");
    ok &= Check(!clawhud::ShouldConsiderForegroundProductionTarget(true, true, false) &&
        !clawhud::ShouldConsiderForegroundProductionTarget(true, false, true) &&
        clawhud::ShouldConsiderForegroundProductionTarget(true, false, false),
        "diagnostic and suspend states block adoption");

    clawhud::GameDetectionCoordinator genericA;
    genericA.ObserveCandidate(100, nullptr,
        clawhud::GameDetectionTrigger::GenericForeground);
    ok &= Check(clawhud::DecideCandidateDisposition(genericA.Context(),
        clawhud::GameDetectionTrigger::GenericForeground, 200) ==
        clawhud::CandidateDisposition::Replace,
        "generic verifying candidate is replaced by a newer generic PID");

    clawhud::GameDetectionCoordinator microsoftA;
    microsoftA.ObserveCandidate(100, nullptr,
        clawhud::GameDetectionTrigger::MicrosoftGameIdentity);
    ok &= Check(clawhud::DecideCandidateDisposition(microsoftA.Context(),
        clawhud::GameDetectionTrigger::GenericForeground, 200) ==
        clawhud::CandidateDisposition::Ignore,
        "MicrosoftGame candidate rejects a different generic PID");
    ok &= Check(clawhud::DecideCandidateDisposition(microsoftA.Context(),
        clawhud::GameDetectionTrigger::MicrosoftGameIdentity, 200) ==
        clawhud::CandidateDisposition::Ignore,
        "MicrosoftGame candidate rejects a different MicrosoftGame PID");
    ok &= Check(clawhud::DecideCandidateDisposition(genericA.Context(),
        clawhud::GameDetectionTrigger::MicrosoftGameIdentity, 200) ==
        clawhud::CandidateDisposition::Replace,
        "MicrosoftGame evidence replaces weaker generic candidate");

    microsoftA.MarkRendererReady(100, microsoftA.Context().generation);
    ok &= Check(clawhud::DecideCandidateDisposition(microsoftA.Context(),
        clawhud::GameDetectionTrigger::GenericForeground, 200) ==
        clawhud::CandidateDisposition::Ignore,
        "Ready candidate is protected from generic foreground");
    ok &= Check(clawhud::DecideCandidateDisposition(microsoftA.Context(),
        clawhud::GameDetectionTrigger::MicrosoftGameIdentity, 200) ==
        clawhud::CandidateDisposition::Ignore,
        "Ready candidate is protected from Microsoft replacement");
    microsoftA.CommitCandidate(100, microsoftA.Context().generation);
    ok &= Check(clawhud::DecideCandidateDisposition(microsoftA.Context(),
        clawhud::GameDetectionTrigger::GenericForeground, 200) ==
        clawhud::CandidateDisposition::Ignore,
        "Committed candidate is never replaced");
    ok &= Check(clawhud::DecideCandidateDisposition(microsoftA.Context(),
        clawhud::GameDetectionTrigger::GenericForeground, 100) ==
        clawhud::CandidateDisposition::Merge,
        "same PID evidence merges");
    return ok ? 0 : 1;
}
