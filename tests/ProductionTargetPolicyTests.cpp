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

    // Executable exclusion policy. The demonstrated field false-positives must
    // never be treated as a game.
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"windowsterminal.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"runtimebroker.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"dllhost.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"backgroundtaskhost.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"werfault.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"crashreportclient.exe"),
        "high-confidence helper and crash processes are not production targets");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"explorer.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"searchhost.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"shellexperiencehost.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"startmenuexperiencehost.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"applicationframehost.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"textinputhost.exe"),
        "shell / desktop surfaces are not production targets");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"steam.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"steamwebhelper.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"steamservice.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gameoverlayui.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"steamerrorreporter.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"steamerrorreporter64.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"steaminputaddonforclaw.ui.exe"),
        "Steam launcher / service / overlay / error reporters are not production targets");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"gamebar.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamebarftserver.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamebar_widget.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"xboxpcapp.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"xboxpcappft.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"xboxpctray.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"xboxgamebarwidgets.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamingservices.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamingservicesui.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamingservicesnet.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"pickerhost.exe"),
        "Windows gaming shells and Gaming Services infrastructure are not production targets");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"chrome.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"msedge.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"firefox.exe"),
        "browsers are not production targets");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"msi center m.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"msi_center_m_launcher.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"msi_center_m_server.exe") &&
        clawhud::IsRejectedProductionTargetImage(
            L"msi_center_m_server_controlmode.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"command center.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"mongmode.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"mcmosdinfo.exe"),
        "MSI Center M companion processes are not production targets");
    ok &= Check(!clawhud::IsRejectedProductionTargetImage(L"msedgewebview2.exe"),
        "shared WebView2 runtime is not globally rejected");

    // Eligibility normalizes the basename and lower-cases before matching.
    ok &= Check(clawhud::IsEligibleProductionTargetImage(L"game.exe") &&
        clawhud::IsEligibleProductionTargetImage(L"C:\\Games\\DaveTheDiver.EXE") &&
        clawhud::IsEligibleProductionTargetImage(
            L"C:\\Games\\Game-Win64-Shipping.exe"),
        "normal game images remain eligible after normalization");
    ok &= Check(!clawhud::IsEligibleProductionTargetImage(
            L"C:\\Windows\\explorer.exe") &&
        !clawhud::IsEligibleProductionTargetImage(L"SteamService.EXE") &&
        !clawhud::IsEligibleProductionTargetImage(L"PickerHost.exe") &&
        !clawhud::IsEligibleProductionTargetImage(
            L"STEAMINPUTADDONFORCLAW.UI.EXE"),
        "rejected images stay rejected regardless of path or case");
    ok &= Check(!clawhud::IsEligibleProductionTargetImage(L""),
        "an empty image name is not eligible");

    // The WPF Settings frontend is centrally rejected: it is ClawHUD
    // infrastructure and can never be a supported game target regardless of its
    // current window size.
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"clawhud.settings.exe"),
        "clawhud.settings.exe is rejected as a production game target");
    ok &= Check(!clawhud::IsEligibleProductionTargetImage(
            L"C:\\Program Files\\ClawHUD\\ClawHUD.Settings.EXE"),
        "ClawHUD.Settings.EXE stays rejected regardless of path or case");

    // ClawHUD-owned image identity: native runtime + Settings frontend, by bare
    // name or full path, case-insensitive.
    ok &= Check(clawhud::IsClawHudOwnedImage(L"clawhud.exe") &&
        clawhud::IsClawHudOwnedImage(L"ClawHUD.EXE") &&
        clawhud::IsClawHudOwnedImage(L"C:\\Program Files\\ClawHUD\\ClawHUD.exe") &&
        clawhud::IsClawHudOwnedImage(L"clawhud.settings.exe") &&
        clawhud::IsClawHudOwnedImage(L"ClawHUD.Settings.EXE"),
        "ClawHUD's own runtime and Settings images are owned");
    ok &= Check(!clawhud::IsClawHudOwnedImage(L"unrelated.exe") &&
        !clawhud::IsClawHudOwnedImage(L"game.exe") &&
        !clawhud::IsClawHudOwnedImage(L"clawhud.diagnostic.exe") &&
        !clawhud::IsClawHudOwnedImage(L""),
        "unrelated images (and empty) are not ClawHUD-owned");

    // PID form: 0 is never owned; the supplied own PID is always owned; an
    // uninspectable external PID is reported as not owned (never broadly
    // suppressed); a real external PID resolves through its image.
    const DWORD self = GetCurrentProcessId();
    ok &= Check(!clawhud::IsClawHudOwnedProcess(0, self),
        "PID 0 is not a ClawHUD-owned process");
    ok &= Check(clawhud::IsClawHudOwnedProcess(self, self),
        "the caller's own PID is always ClawHUD-owned (native runtime self)");
    ok &= Check(!clawhud::IsClawHudOwnedProcess(self, /*ownProcessId=*/1),
        "an external PID resolving to a non-ClawHUD image is not owned");
    ok &= Check(!clawhud::IsClawHudOwnedProcess(4, /*ownProcessId=*/1),
        "an uninspectable external PID is not suppressed as self");

    // Always FPS foreground sanitizer: self -> 0, external -> passthrough.
    ok &= Check(clawhud::ResolveAlwaysFpsForegroundTarget(self, self) == 0,
        "a ClawHUD-owned foreground sanitizes the Always FPS target to 0");
    ok &= Check(clawhud::ResolveAlwaysFpsForegroundTarget(self, /*own=*/1) == self,
        "an external foreground PID passes through unchanged");
    ok &= Check(clawhud::ResolveAlwaysFpsForegroundTarget(0, self) == 0,
        "no foreground stays PID 0");

    // Resume recovery re-adopts the current foreground only when the HUD is on
    // and the recovery attempt actually completed.
    ok &= Check(clawhud::ShouldReevaluateForegroundAfterResume(true, true) &&
        !clawhud::ShouldReevaluateForegroundAfterResume(false, true) &&
        !clawhud::ShouldReevaluateForegroundAfterResume(true, false),
        "completed resume recovery re-adopts the current foreground");

    return ok ? 0 : 1;
}
