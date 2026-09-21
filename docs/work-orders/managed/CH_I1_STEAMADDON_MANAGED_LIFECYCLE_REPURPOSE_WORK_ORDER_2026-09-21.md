# Work Order — CH-I1 Repurpose Managed Mode for SteamAddon Ownership

**Date:** 2026-09-21  
**Status:** Ready for implementation  
**Target repository:** onehoon/ClawHUD  
**Target branch:** integration/steamaddon  
**Reviewed baseline:** integration/steamaddon at ec770bbaa8c41f531f9c209071577cca640b89c9  
**Baseline relation:** identical to main when this work order was prepared  
**Expected PR count:** 1 focused PR  
**PR base:** integration/steamaddon  
**PR head:** implementation branch created from integration/steamaddon

---

# CRITICAL BRANCH POLICY

This integration phase does NOT target main.

~~~text
Work only from integration/steamaddon.
Open the implementation PR against integration/steamaddon.
DO NOT target main.
DO NOT merge this PR directly into main.
DO NOT rebase the implementation branch onto a newer main unless explicitly requested.
~~~

If GitHub or local tooling defaults to:

~~~text
base = main
~~~

stop and correct it to:

~~~text
base = integration/steamaddon
~~~

before opening the PR.

The completed SteamAddon integration will be hardware-validated on the integration branch first. Moving the completed result to main is a later explicit step.

---

## 1. Objective

Repurpose the existing ClawHUD Managed launch mode from a generic external-frontend mode into the SteamAddonforClaw-owned runtime mode.

Keep the existing launch token:

~~~text
ClawHUD.exe --managed
~~~

Do not add a third launch mode such as AddonManaged.

Target model:

~~~text
Standalone
---------
ClawHUD owns its standalone shell lifecycle.

Managed
-------
SteamAddonforClaw is the only intended owner.
No ClawHUD tray.
No standalone Settings shell ownership.
No ClawHUD startup-task ownership.
No ClawHUD self-update ownership.
No global F8 HUD hotkey.

ClawHUD still owns:
- HUD settings/state
- HUD runtime
- telemetry
- game detection
- PresentMon prerequisite/runtime use
- EC/system/battery telemetry
- Intel VRR Fix
- suspend/resume recovery
- Control IPC
~~~

This PR prepares the ClawHUD side only.

Do not implement SteamAddon process ownership, Addon UI, packaging, or Addon-side IPC in CH-I1.

---

## 2. Superseded product assumptions

The existing runtime/frontend separation is still valid, but several assumptions in:

docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md

are superseded for the new integration direction.

Old assumptions that are no longer authoritative:

~~~text
Managed is generic for arbitrary external frontends.
ClawHUD must remain separately installed for SteamAddon integration.
ClawHUD must remain separately updated for SteamAddon integration.
SteamAddon must never bundle or update ClawHUD.
Managed may mutate the standalone Start-with-Windows preference.
Managed applies its own VeloPack update with restart=false.
~~~

New authority:

~~~text
Managed == SteamAddon-owned ClawHUD runtime.

SteamAddon will later own the bundled/pinned companion version and process lifecycle.

Managed must not own or mutate:
- ClawHUD standalone startup registration
- ClawHUD self-update lifecycle
- standalone shell surfaces

ClawHUD remains authoritative for:
- HUD state
- HUD settings validation/persistence
- HUD rendering/presentation
- telemetry
- game detection
- Intel VRR Fix implementation/state
- suspend/resume behavior
- Control IPC
~~~

Do not rewrite the older architecture document wholesale. Add only a concise supersession note if needed so later work does not accidentally follow the obsolete separate-install/update contract.

---

## 3. Later integration lifecycle contract

The later SteamAddon integration will use this deliberately simple rule:

~~~text
HUD OFF
-> SteamAddon does not launch ClawHUD Managed
-> no ClawHUD process
-> no PresentMon readiness/install work
-> no EC helper
-> no Intel VRR Fix run

HUD ON
-> SteamAddon launches ClawHUD.exe --managed
-> ClawHUD performs its normal HUD prerequisite/runtime startup
-> PresentMon is reused or installed/upgraded as required
-> Intel VRR Fix runs only according to its existing enabled setting
-> HUD runtime runs
~~~

Intel VRR Fix is a bug-fix/tweak operation.

There is no rollback or restore-to-old-profile concept to add when HUD is turned off.

Do not invent one.

SteamAddon-side start/stop based on HUD Enabled is not implemented in CH-I1.

---

## 4. Scope

Implement only the ClawHUD shell/lifecycle cleanup needed to make Managed a truthful SteamAddon-owned mode.

Required areas:

~~~text
LaunchMode comments/contracts
RuntimeLifecyclePolicy
App startup update policy
startup-registration mutation policy
Managed-mode comments/documentation
F8 global hotkey removal
F8-only manual visibility override cleanup
tests for the revised lifecycle contract
~~~

Do not expand this PR into packaging or Addon integration.

---

## 5. Redefine Managed semantics

Keep the existing enum:

~~~cpp
enum class LaunchMode
{
    Standalone,
    Managed,
};
~~~

Keep exact token parsing:

~~~text
--managed
~~~

Update comments/tests so the meaning is explicit:

~~~text
Standalone = ClawHUD standalone product shell
Managed    = SteamAddonforClaw-owned runtime
~~~

Remove generic-owner wording where it defines the Managed product contract.

ClawHUD must not infer Managed from:

~~~text
SteamAddon process detection
environment variables
registry
settings.ini
previous launch state
~~~

Managed remains explicit and command-line selected.

---

## 6. Managed must not self-update

Current behavior:

~~~text
App::Run()
-> CheckForUpdates() for Standalone and Managed

Managed update
-> VeloPack applies update
-> restart=false
-> external owner relaunches
~~~

That is no longer valid.

Target behavior:

~~~text
Standalone
-> existing ClawHUD self-update behavior remains unchanged

Managed
-> do not check the ClawHUD release feed
-> do not download a ClawHUD update
-> do not apply a ClawHUD update
-> do not exit because ClawHUD found an update
~~~

SteamAddon will later own the pinned/bundled companion version.

Recommended minimal policy:

~~~cpp
constexpr bool ShouldRunSelfUpdate(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Standalone;
}
~~~

App::Run should call CheckForUpdates only when this policy allows it.

Remove the obsolete Managed policy that applies an update with restart=false.

Remove ShouldRestartAfterVelopackUpdate if it has no valid caller after this change.

Do not redesign the Standalone updater.

Do not introduce a new update manager abstraction.

The VeloPack fast-exit hook in main.cpp may remain for now if the current executable/package build requires it. CH-I1 only requires that normal Managed startup does not perform ClawHUD update discovery/download/apply.

---

## 7. Managed must not own Start with Windows

Current startup-time registration reconciliation is already Standalone-only.

Keep that.

The remaining problem is that Control IPC still permits SetStartWithWindows while Managed is running.

Target behavior:

~~~text
Standalone + SetStartWithWindows
-> existing behavior unchanged

Managed + SetStartWithWindows
-> reject
-> do not mutate the standalone startup preference
-> do not create/delete/repair the ClawHUD Task Scheduler task
-> do not request UAC
~~~

Prefer the smallest implementation.

One acceptable shape is to change the semantic control method to return success/failure:

~~~cpp
virtual bool SetStartWithWindows(bool enabled) = 0;
~~~

and in App:

~~~cpp
bool App::SetStartWithWindows(bool enabled)
{
    if (launchMode_ == clawhud::LaunchMode::Managed)
        return false;

    // existing Standalone behavior
}
~~~

RuntimeControlWireMapping can then return the existing OperationFailed status for a Managed mutation attempt.

Do not add protocol v2 solely for this.

Do not add a capability-negotiation layer.

The settings snapshot may continue carrying the persisted Standalone startWithWindows preference for compatibility. SteamAddon simply must not expose or mutate it.

---

## 8. Remove the F8 HUD hotkey completely

F8 is no longer part of the target product UX.

Remove it from both Standalone and Managed.

Delete:

~~~text
kHudToggleHotkeyId
RegisterHotKey(... VK_F8)
UnregisterHotKey(...)
WM_HOTKEY dispatch
App::HandleHudToggleHotkey()
hudHotkeyRegistered_
F8-specific logging
~~~

Update RuntimeMessageWindow comments so they only describe real remaining responsibilities:

~~~text
WM_POWERBROADCAST
production WM_TIMER
runtime-control dispatch/shutdown wakes
~~~

Do not replace F8 with another global shortcut.

HUD control ownership becomes:

~~~text
Standalone -> standalone settings/tray UX
Managed    -> SteamAddon UI through Control IPC
~~~

---

## 9. Remove F8-only manual visibility override state

The current manual visibility override exists for the F8 behavior.

After F8 removal, do not retain a dead visibility authority with no producer.

Remove where no longer used:

~~~text
ResolveHudHotkeyOverride()
HudController::ManualOverride()
HudController::SetManualOverride()
HudController::ResetManualOverride()
manualOverride_
F8/manual-override-specific tests
~~~

Simplify resolved visibility to the persisted product rule:

~~~text
HUD Enabled
AND
(
    VisibilityMode == Always
    OR foreground game is active
)
~~~

Equivalent conceptual helper:

~~~cpp
bool ResolveHudVisible(
    bool hudEnabled,
    HudVisibilityMode mode,
    bool foregroundActive) noexcept
{
    return hudEnabled && ShouldShowHud(mode, foregroundActive);
}
~~~

Update resume-recovery expected-visibility calculation to use the same normal visibility rule without manual override state.

This is state cleanup only.

Do not refactor the concrete HUD presentation lifecycle.

---

## 10. Intel VRR Fix must remain

Intel VRR Fix is a required ClawHUD feature because SteamAddon has no equivalent implementation.

Preserve:

~~~text
TweakStartupCoordinator
IntelVrrRangeTweak
IntelArcSyncClient
AffectedPanelDetector
IntelVrrResultStore
SetIntelVrrRangeFixEnabled IPC operation
settings persistence
existing retry behavior
existing validation/fail-safe behavior
~~~

Feature contract:

~~~text
ClawHUD Managed starts because HUD is in use
-> if IntelVrrRangeFixEnabled=true
-> existing startup coordinator may apply the fix

HUD not in use
-> SteamAddon later does not launch ClawHUD
-> therefore the fix is not run/reapplied
~~~

Do not add VRR rollback.

Do not change the Intel VRR Fix algorithm in CH-I1.

---

## 11. PresentMon ownership remains unchanged in CH-I1

Keep the existing ClawHUD PresentMon prerequisite implementation.

Preserve:

~~~text
required runtime version floor
reuse of newer compatible runtime
no downgrade
service/registry/middleware validation
API compatibility validation
bundled MSI install/upgrade
elevated msiexec
post-install readiness validation
~~~

Do not move PresentMon installation into SteamAddon in CH-I1.

Do not install PresentMon at SteamAddon/controller runtime startup.

Do not make PresentMon a Full1902/controller prerequisite.

PresentMon remains a HUD-feature prerequisite.

Any Addon-specific non-interactive error UX should be a later focused integration task only if actual integration requires it.

---

## 12. NON-NEGOTIABLE presentation / TopMost / VRR safety boundary

This PR is NOT a HUD presentation refactor.

Do not modify, reorganize, modernize, simplify, or clean up:

~~~text
src/ClawHUD/HudPresentation*
src/ClawHUD/HudRenderer*
src/ClawHUD/HudWindowGeometry*
src/ClawHUD/HudPresentationContract*
src/ClawHUD/HudPresentationLifecycle*
~~~

Do not change:

~~~text
D3D11 setup
DXGI resources
Presentation API path
DirectComposition path
D2D/DWrite rendering path
shared displayable buffers
independent-flip contract
premultiplied-alpha contract
window styles
WS_EX_TOPMOST
TopMost/Z-order behavior
SetWindowPos behavior
Show/Hide presentation ordering
presentation creation/destruction ordering
source rect/transform behavior
present cadence
presentation recovery behavior
~~~

Recent production TopMost and VRR behavior is proven and sensitive.

Preferred acceptance criterion:

~~~text
zero diff in HudPresentation*/HudRenderer*/presentation-contract files
~~~

If a presentation file appears in the implementation diff for anything other than an unavoidable compile-only correction, stop and re-evaluate.

---

## 13. Full 1902 isolation requirement

SteamAddon Full 1902 controller runtime is a separate high-criticality authority.

CH-I1 must not introduce dependency from ClawHUD into:

~~~text
PID1901/PID1902 ownership
DirectInput ownership
HidHide
VIIPER
Xbox360/SteamDeck presentation switching
Center M authority
controller recovery
controller shutdown/restart policy
~~~

Likewise, ClawHUD failure must never become a controller-runtime failure.

No shared watchdog, controller authority state, or generalized supervisor is needed.

---

## 14. Expected source scope

Likely production files:

~~~text
src/ClawHUD/LaunchMode.h
src/ClawHUD/LaunchMode.cpp
src/ClawHUD/RuntimeLifecyclePolicy.h
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/RuntimeMessageWindow.h
src/ClawHUD/RuntimeMessageWindow.cpp
src/ClawHUD/RuntimeControl.h
src/ClawHUD/RuntimeControlWireMapping.cpp
src/ClawHUD/HudModel.h
src/ClawHUD/HudModel.cpp
src/ClawHUD/HudController.h
src/ClawHUD/HudController.cpp
~~~

Tests likely include:

~~~text
tests/LaunchModeTests.cpp
tests/HudModelTests.cpp
existing RuntimeControl mapping/dispatch tests
~~~

Documentation may include a narrow supersession note in:

~~~text
docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md
~~~

Exact production scope may be smaller.

Avoid new production classes unless a concrete need appears.

---

## 15. Tests

### 15.1 Launch/lifecycle policy

Prove:

~~~text
Standalone
- startup registration reconciliation allowed
- self-update allowed

Managed
- startup registration reconciliation denied
- self-update denied
~~~

Remove obsolete semantics:

~~~text
Managed updater runs but restart=false
~~~

### 15.2 Start-with-Windows mutation

Add or extend runtime-control coverage:

~~~text
Standalone SetStartWithWindows
-> existing behavior remains available

Managed SetStartWithWindows
-> OperationFailed
-> no startup mutation
~~~

Do not introduce a large Task Scheduler mock just for this check.

Use the narrow existing policy/dispatch seams.

### 15.3 F8 removal

Remove tests for:

~~~text
ResolveHudHotkeyOverride
F8 show/hide override behavior
~~~

Keep/update visibility tests for:

~~~text
HUD disabled -> hidden
Always -> visible while enabled
InGameOnly + no game -> hidden
InGameOnly + foreground game -> visible
~~~

### 15.4 Presentation regression protection

All existing presentation contract tests remain unchanged and green.

No expected assertion changes around:

~~~text
WS_EX_TOPMOST
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
independent flip
shared displayable buffers
premultiplied alpha
presentation lifecycle
~~~

### 15.5 Build

Run normal repository validation:

~~~text
Debug build
Release build
native CTest suite
Settings/WPF tests when part of normal CI
~~~

No new warnings.

---

## 16. Manual smoke matrix

### A. Standalone

~~~text
launch ClawHUD.exe
-> tray still works
-> standalone Settings still opens
-> standalone update path remains available
-> Start with Windows can still be changed
-> HUD behavior unchanged
-> F8 does nothing
~~~

### B. Managed

~~~text
launch ClawHUD.exe --managed
-> no tray
-> no standalone Settings shell ownership
-> no ClawHUD update feed check/download/apply
-> no startup-task reconciliation
-> Control IPC starts
-> HUD runtime works
-> PresentMon behavior unchanged
-> Intel VRR Fix behavior unchanged
-> F8 does nothing
~~~

### C. Suspend/resume

~~~text
Managed running with HUD enabled
-> suspend
-> resume
-> existing HUD recovery completes
~~~

No new resume authority or alternate recovery path should be added.

### D. TopMost / VRR regression smoke

Use current production scenarios:

~~~text
desktop/fullscreen transition
Steam Big Picture
known VRR-working game
game launch/exit
~~~

Pass:

~~~text
HUD TopMost behavior unchanged
HUD remains non-activating/click-through as before
VRR still engages normally
no new presentation recreation
no periodic Z-order mutation
~~~

If TopMost or VRR changes, treat it as a regression.

Do not compensate by changing presentation code in this PR.

---

## 17. Explicitly out of scope

Do not implement:

~~~text
SteamAddon process owner
SteamAddon Overlay settings tab
SteamAddon IPC client
SteamAddon packaging
ClawHUD bundled artifact target
ClawHUD source pinning in SteamAddon
PresentMon installer ownership transfer
new PresentMon update mechanism
automatic standalone-ClawHUD uninstall/migration
single-instance migration UX
new watchdog/supervisor
owner PID heartbeat
new service
new process manager framework
controller integration
HidHide integration
VIIPER integration
new HUD renderer
new presentation path
Intel VRR Fix algorithm changes
VRR rollback
~~~

---

## 18. Overengineering guard

Keep the implementation small.

Do not add:

~~~text
generic host abstraction
dependency-injection framework
plugin system
generic lifecycle state machine
capability-negotiation framework
heartbeat
epoch/barrier system
extra worker process
extra synchronization for theoretical races
~~~

The architecture has one simple ownership statement:

~~~text
Standalone -> ClawHUD owns standalone shell concerns.
Managed    -> SteamAddon owns process/package shell concerns.
ClawHUD    -> always owns HUD runtime internals.
~~~

Implement only what is necessary to make that statement true.

---

## 19. PR review checklist

~~~text
[ ] PR base is integration/steamaddon, NOT main
[ ] implementation branch was created from integration/steamaddon
[ ] Managed meaning is SteamAddon-only
[ ] no third AddonManaged launch mode added
[ ] Managed does not self-check/download/apply ClawHUD updates
[ ] Standalone updater still works
[ ] Managed does not reconcile startup registration
[ ] Managed SetStartWithWindows is rejected
[ ] no Managed UAC path for startup-task mutation
[ ] F8 registration removed
[ ] F8 WM_HOTKEY handling removed
[ ] F8 unregister path removed
[ ] F8-only manual override state removed
[ ] normal HUD visibility semantics remain correct
[ ] Intel VRR Fix implementation unchanged
[ ] PresentMon bootstrap implementation unchanged
[ ] suspend/resume behavior preserved
[ ] Control IPC remains operational
[ ] HudPresentation* files unchanged
[ ] HudRenderer* files unchanged
[ ] production presentation contract unchanged
[ ] TopMost behavior unchanged
[ ] VRR behavior unchanged on hardware smoke
[ ] no Full1902/controller authority coupling introduced
[ ] normal builds/tests green
~~~

---

## 20. Completion result

After CH-I1:

~~~text
ClawHUD.exe
-> Standalone ClawHUD product

ClawHUD.exe --managed
-> SteamAddon-owned companion runtime
-> no tray
-> no F8
-> no ClawHUD startup ownership
-> no ClawHUD self-update ownership
-> same proven HUD runtime
-> same proven presentation
-> same PresentMon telemetry
-> same game detection
-> same EC/system/battery telemetry
-> same Intel VRR Fix
-> same suspend/resume logic
-> same Control IPC
~~~

CH-I1 is complete when shell/lifecycle ownership is clean without touching the VRR-sensitive production presentation path.

The implementation PR must merge into:

~~~text
integration/steamaddon
~~~

not main.
