# Work Order — Auto-Show ClawHUD While SteamAddon Overlay Is Visible

> **Repository:** `onehoon/ClawHUD`  
> **Date:** 2026-10-07  
> **PR base:** `integration/steamaddon`  
> **Target branch after merge:** `integration/steamaddon`  
> **Recommended implementation branch:** `feature/steamaddon-overlay-auto-hud-visibility`  
> **Reviewed ClawHUD branch:** current `integration/steamaddon`  
> **Reviewed SteamAddon baseline:** current `main`, standalone Full1902 architecture  
> **Expected PR count:** 1 focused ClawHUD PR  
> **SteamAddon code change:** none

---

## 1. Goal

When the SteamAddon Quick Settings Overlay is visible, ClawHUD should temporarily show its HUD even when the persisted ClawHUD display mode is `InGameOnly` and no foreground game is active.

ClawHUD must detect this condition by observing the SteamAddon Overlay HWND itself.

Do **not** add a new SteamAddon -> ClawHUD IPC operation.

Do **not** add a new user option.

Required behavior:

```text
ClawHUD disabled
+ SteamAddon Overlay visible
=> HUD remains hidden

ClawHUD enabled
+ Display Mode = Always
=> existing behavior remains unchanged

ClawHUD enabled
+ Display Mode = InGameOnly
+ foreground game active
=> existing behavior remains unchanged

ClawHUD enabled
+ Display Mode = InGameOnly
+ no foreground game
+ SteamAddon Overlay hidden
=> HUD hidden

ClawHUD enabled
+ Display Mode = InGameOnly
+ no foreground game
+ SteamAddon Overlay visible
=> HUD shown

SteamAddon Overlay closes
=> return immediately to the existing ClawHUD visibility policy
```

The final visibility authority remains ClawHUD.

SteamAddon only owns its own Overlay window. It does not need to notify ClawHUD.

---

## 2. Mandatory architecture constraints

This PR is a ClawHUD presentation-policy feature only.

It must not change any Full1902 controller authority.

SteamAddon Full1902 remains:

```text
Runtime
  -> controller authority
  -> PID1902 / DirectInput
  -> HidHide
  -> VIIPER
  -> WING suppression
  -> Main UI / Overlay process ownership

Main UI / Overlay
  -> frontend only
```

ClawHUD remains an optional sibling feature.

A ClawHUD failure must never affect:

```text
PID1901 / PID1902
HidHide
VIIPER
physical input
virtual presentation
routing recovery
WING suppression
Center M authority
```

This work also does not reintroduce any CTW dependency.

---

## 3. Important narrow exception to the older ClawHUD integration wording

Older runtime-separation documents correctly state that ClawHUD must not detect or own the SteamAddon **lifecycle**.

Keep that rule.

This PR intentionally adds only one narrow surface observation:

```text
Is a visible top-level HWND owned by
SteamInputAddonforClaw.Overlay.exe
currently present?
```

That is **not** permission to add:

```text
SteamAddon installation probing
SteamAddon Runtime process probing
parent-process ownership
Addon heartbeat
Addon process watcher
Addon restart supervision
Addon Named Pipe dependency
launch / stop / adoption behavior
```

ClawHUD observes one existing window surface only.

It never owns SteamAddon lifecycle.

---

## 4. Current verified ClawHUD branch behavior

The implementation must be based on the current `integration/steamaddon` branch, not on newer main-only behavior.

### 4.1 Foreground tracking is insufficient

Current:

```cpp
ForegroundTracker::Start(...)
{
    ...
    hook_ = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        ...);
}
```

and:

```cpp
ForegroundTracker::Reconcile()
{
    const HWND foreground = GetForegroundWindow();
    ...
}
```

SteamAddon Overlay intentionally does not become foreground.

Current SteamAddon Overlay presentation uses:

```text
WS_EX_NOACTIVATE
SWP_NOACTIVATE
SW_SHOWNOACTIVATE
WM_MOUSEACTIVATE -> MA_NOACTIVATE
```

Therefore:

```text
SteamAddon Overlay Show
!= foreground HWND transition
```

Do not attempt to implement this feature through `ForegroundTracker`.

### 4.2 ClawHUD already has the required top-level HWND event source

Current `ProductionGameWindowSource` already observes:

```cpp
EVENT_OBJECT_CREATE
EVENT_OBJECT_SHOW
EVENT_OBJECT_HIDE
EVENT_OBJECT_LOCATIONCHANGE
EVENT_OBJECT_DESTROY
EVENT_OBJECT_NAMECHANGE
```

using `SetWinEventHook(... WINEVENT_OUTOFCONTEXT)`.

The source already filters to:

```text
OBJID_WINDOW
CHILDID_SELF
top-level/root HWND
```

and forwards:

```text
event type
HWND
PID
window thread id
sequence/timestamps
```

to `GameSessionController`.

Reuse this source.

Do not create a second WinEvent hook solely for SteamAddon Overlay detection.

### 4.3 Current HUD resolver is simple on this branch

Current integration branch:

```cpp
bool ResolveHudVisible(
    bool hudEnabled,
    HudVisibilityMode mode,
    bool foregroundActive) noexcept
{
    return hudEnabled &&
        (mode == HudVisibilityMode::Always || foregroundActive);
}
```

The current `integration/steamaddon` branch does **not** contain main's later F8 manual-override resolver changes.

Do not pull unrelated main F8/manual-override work into this PR.

### 4.4 Existing reconciliation already owns telemetry side effects

Current:

```text
App::ReconcileHudVisibility()
  -> HudController::ReconcileVisibility(...)
      -> HudPresentation::Show / Hide
      -> returns HudVisibilityEffects

App
  -> StartProductionSampling()
  -> StopProductionSampling(...)
```

Keep this ownership.

When Overlay visibility causes the HUD to show, the existing production sampling lifecycle should start naturally through this path.

When Overlay visibility no longer requires the HUD, the existing path should stop sampling naturally when no other visibility reason remains.

Do not create a separate "Overlay telemetry mode."

---

## 5. Product policy — no new option

Do not add a setting such as:

```text
Show HUD with SteamAddon Overlay
```

The product rule is fixed:

```text
HUD master Disabled
=> never show

HUD master Enabled
+ SteamAddon Overlay visible
=> Overlay is one valid temporary HUD visibility reason
```

Do not add:

- a settings.ini key;
- a Control IPC field;
- a Settings UI toggle;
- a SteamAddon UI toggle;
- another persisted state owner.

If a real user requirement for opt-out appears later, it can be a separate feature.

---

## 6. Exact SteamAddon Overlay identity

The current published executable is:

```text
SteamInputAddonforClaw.Overlay.exe
```

Match the executable basename case-insensitively after using the existing production process inspection/normalization path.

Do not identify the Overlay by:

```text
window title
window class generated by WinUI
screen position
window size
topmost state
parent PID
SteamAddon installation path
```

The executable identity is the stable product boundary for this feature.

---

## 7. Add the Overlay executable to the production-target rejection list

Current `ProductionTargetPolicy.cpp` rejects:

```text
steaminputaddonforclaw.ui.exe
```

but does not currently reject:

```text
steaminputaddonforclaw.overlay.exe
```

Add the Overlay executable to the centralized rejected production-target image set.

Current code uses:

```cpp
constexpr std::array<std::wstring_view, 46> rejected{ ... };
```

Update the array size consistently when adding the new entry.

Required result:

```text
SteamInputAddonforClaw.Overlay.exe
=> never a production game target
=> still inspectable by image name for the narrow Overlay visibility rule
```

Use the existing:

```cpp
InspectProductionTargetProcessDetailed(processId)
```

for PID -> normalized executable image resolution where practical.

Do not create a second generic process-inspection utility.

---

## 8. Runtime Overlay HWND state

Keep the runtime state minimal.

Recommended state in `GameSessionController`:

```cpp
HWND steamAddonOverlayWindow_{};
```

Do not add:

```text
OverlayStateManager
OverlayLifecycleService
PID epoch
generation id
lease
heartbeat
timer
polling worker
set of historical HWNDs
```

The current SteamAddon product owns one Overlay surface.

That is the supported scope.

### Current visibility query

Expose one narrow query from `GameSessionController`:

```cpp
bool SteamAddonOverlayVisible() const noexcept;
```

Conceptually:

```cpp
bool GameSessionController::SteamAddonOverlayVisible() const noexcept
{
    return steamAddonOverlayWindow_ != nullptr &&
        IsWindowVisible(steamAddonOverlayWindow_);
}
```

Using current HWND visibility instead of persisting a second boolean gives a simple self-correcting read if the window is already gone.

---

## 9. Runtime SHOW / HIDE / DESTROY handling

Handle SteamAddon Overlay surface events inside the existing
`GameSessionController::HandleProductionWindowEvent(...)` path.

Do not create another event source.

### SHOW

For `ProductionWindowEventType::Show`:

```text
if event.window is already the tracked Overlay HWND
    -> reconcile HUD visibility

else
    -> inspect event.processId
    -> if normalized image == steaminputaddonforclaw.overlay.exe
         track event.window
         reconcile HUD visibility
```

Conceptually:

```cpp
if (event.type == ProductionWindowEventType::Show)
{
    if (event.window == steamAddonOverlayWindow_ ||
        IsSteamAddonOverlayProcess(event.processId))
    {
        steamAddonOverlayWindow_ = event.window;
        hooks_.reconcileHudVisibility();
    }
}
```

The implementation may factor the exact-image comparison into one tiny local helper if that keeps the code clearer.

Do not create a reusable external-process framework.

### HIDE

For a tracked Overlay HWND:

```text
HIDE
-> keep or clear the HWND as implementation prefers
-> ReconcileHudVisibility()
-> SteamAddonOverlayVisible() must now resolve false
```

Keeping the HWND through a normal Hide is useful because the same warm Overlay process/window can be shown again.

### DESTROY

For the tracked Overlay HWND:

```text
DESTROY
-> steamAddonOverlayWindow_ = nullptr
-> ReconcileHudVisibility()
```

This covers the realistic Overlay-process crash/exit path.

### Other events

Do not make Overlay visibility react to:

```text
LOCATIONCHANGE
NAMECHANGE
CREATE alone
unrelated process SHOW/HIDE/DESTROY
```

A CREATE event is not proof that the surface is visible.

---

## 10. Do not short-circuit existing game detection

Overlay observation is an additional side observation of the already-delivered top-level window event.

Do not replace the existing `HandleProductionWindowEvent` behavior.

Existing logic must continue to:

```text
inspect Microsoft game evidence
reconcile ForegroundTracker
apply current-screen event rules
apply NAMECHANGE debounce
evaluate current foreground where required
```

The SteamAddon Overlay executable being in the centralized rejected-target list prevents it from becoming a game target.

Do not use an early return that accidentally skips unrelated existing event processing unless it is proven equivalent.

---

## 11. Startup synchronization — one scan only

A real supported sequence is:

```text
SteamAddon Overlay already visible
-> ClawHUD process starts afterward
```

The SHOW event has already happened before ClawHUD installed its WinEvent hook.

Therefore add one startup synchronization scan.

Required order:

```text
1. Start ProductionGameWindowSource successfully.
2. WinEvent hooks are now active.
3. EnumWindows() once.
4. Look only at currently visible top-level windows.
5. Resolve each candidate PID through the existing process inspection path.
6. If exact image == SteamInputAddonforClaw.Overlay.exe:
       remember that HWND.
7. Continue normal ClawHUD startup.
```

Install the event source **before** the one-time scan.

This closes the normal startup gap without adding a polling loop.

Conceptually:

```cpp
bool GameSessionController::StartWindowSource()
{
    const bool started = productionGameWindowSource_.Start(...);
    if (!started)
        return false;

    DetectExistingSteamAddonOverlayWindow();
    return true;
}
```

### If the window source fails

Do not apply a one-time Overlay-visible state if continuing SHOW/HIDE/DESTROY observation is unavailable.

Current degraded behavior should remain:

```text
ProductionGameWindowSource failed
-> ClawHUD continues with existing generic/Steam game detection fallback
-> no SteamAddon Overlay visibility enhancement
```

This avoids creating a stale one-time Overlay state.

---

## 12. Startup scan constraints

The one-time scan must remain small.

Use:

```cpp
EnumWindows(...)
IsWindowVisible(hwnd)
GetWindowThreadProcessId(hwnd, &pid)
InspectProductionTargetProcessDetailed(pid)
```

where appropriate.

Do not add:

```text
periodic EnumWindows
process snapshot polling
WMI process watcher
Toolhelp polling
retry timer
startup backoff
cross-process injected hook
UI Automation
```

The existing WinEvent stream is the steady-state authority after startup.

---

## 13. Visibility resolver change

Extend the current branch resolver with exactly one transient input.

Recommended signature:

```cpp
bool ResolveHudVisible(
    bool hudEnabled,
    HudVisibilityMode mode,
    bool foregroundActive,
    bool steamAddonOverlayVisible) noexcept;
```

Required policy:

```cpp
return hudEnabled &&
    (steamAddonOverlayVisible ||
     mode == HudVisibilityMode::Always ||
     foregroundActive);
```

Equivalent factoring through `ShouldShowHud(...)` is fine:

```cpp
return hudEnabled &&
    (steamAddonOverlayVisible ||
     ShouldShowHud(mode, foregroundActive));
```

Do not persist `steamAddonOverlayVisible`.

Do not add it to `HudLayoutOptions`.

It is runtime observation only.

---

## 14. HudController integration

Update:

```cpp
HudController::ReconcileVisibility(...)
```

to accept the Overlay-visible input and pass it to `ResolveHudVisible(...)`.

Conceptually:

```cpp
HudVisibilityEffects HudController::ReconcileVisibility(
    bool foregroundGameActive,
    bool steamAddonOverlayVisible)
{
    const bool resolvedShow = ResolveHudVisible(
        enabled_,
        options_.visibilityMode,
        foregroundGameActive,
        steamAddonOverlayVisible);

    ...
}
```

Keep the existing:

```text
Show / Hide calls
show/hide failure latches
startProductionSampling effect
stopProductionSampling effect
```

unchanged.

Do not add a second presentation path.

---

## 15. App reconciliation

Current `App::ReconcileHudVisibility()` remains the single top-level visibility reconciliation path.

Update it conceptually to:

```cpp
void App::ReconcileHudVisibility()
{
    if (!hudController_.HasPresentation())
        return;

    if (suspended_ || resumeRecoveryActive_)
    {
        hudController_.HideForLifecycleGate();
        return;
    }

    gameSession_.RevalidateCurrentForegroundGame();

    const bool foregroundGameActive =
        gameSession_.CurrentForegroundGameActive();

    const bool steamAddonOverlayVisible =
        gameSession_.SteamAddonOverlayVisible();

    const auto effects = hudController_.ReconcileVisibility(
        foregroundGameActive,
        steamAddonOverlayVisible);

    // preserve current effect handling
}
```

The existing lifecycle gate remains stronger than Overlay visibility.

Required:

```text
Suspend / resume recovery active
+ Overlay visible
=> HUD remains lifecycle-hidden
```

The Overlay exception must never bypass suspend/recovery safety.

---

## 16. Resume-recovery integration

This is a real supported lifecycle and must be updated consistently.

Current `App::TryResumeRecovery()` computes expected visibility using only:

```text
HUD enabled
display mode
foreground game active
```

After this feature, include current Overlay visibility in that same expected-state calculation.

Conceptually:

```cpp
const bool steamAddonOverlayVisible =
    gameSession_.SteamAddonOverlayVisible();

const bool expectedVisible = clawhud::ResolveHudVisible(
    hudEnabled,
    visibilityMode,
    rendererForegroundActive,
    steamAddonOverlayVisible);
```

Also adjust the "visibility depends on foreground" decision.

Current concept:

```cpp
const bool visibilityUsesForeground =
    visibilityMode == HudVisibilityMode::InGameOnly;
```

Required concept:

```cpp
const bool visibilityUsesForeground =
    visibilityMode == HudVisibilityMode::InGameOnly &&
    !steamAddonOverlayVisible;
```

Reason:

```text
InGameOnly
+ no foreground game
+ Overlay visible
=> HUD is legitimately expected visible

Resume recovery must not wait for a foreground game that is no longer
required for visibility.
```

Do not add a separate resume state machine.

Use the existing recovery path with the corrected visibility input.

---

## 17. HUD Enable/Disable behavior

No special new code should be required outside the normal reconciliation path.

### Disabled

```text
SetHudEnabled(false)
-> existing StopHud()
-> presentation hidden/shut down
-> Overlay visibility does not revive it
```

### Enabled while Overlay is already visible

A realistic Addon flow is:

```text
SteamAddon Overlay visible
-> user enables ClawHUD
-> ClawHUD Managed runtime starts or HUD master becomes enabled
```

The startup scan / continuously tracked Overlay HWND must make the existing:

```text
SetHudEnabled(true)
-> Ensure()
-> ReevaluateForeground()
-> ReconcileHudVisibility()
```

show the HUD immediately.

Do not add a SteamAddon reassert message for this case.

---

## 18. Telemetry behavior

No new telemetry architecture is required.

When Overlay visibility is the only reason for an `InGameOnly` HUD to be visible:

```text
HUD shows
-> existing ReconcileVisibility effect requests StartProductionSampling()
-> global HUD telemetry becomes active
-> FPS remains subject to the existing target/game availability rules
```

When Overlay closes and no foreground game is active:

```text
HUD hides
-> existing effect requests StopProductionSampling(...)
```

If a game is already active:

```text
Overlay close
-> foreground game remains a valid visibility reason
-> HUD remains visible
-> sampling remains active
```

Do not fake a game target just because the Overlay is visible.

Do not set:

```text
foregroundGameActive = true
in-game PID = SteamAddon PID
FPS target = Overlay PID
```

Overlay visibility is only a HUD-presentation reason.

---

## 19. Privilege boundary

The current Full1902 architecture runs the SteamAddon Runtime elevated and its Overlay inherits that token.

The managed ClawHUD launched by SteamAddon also inherits the Addon launch token through the existing process path.

Do not add:

```text
privilege broker
cross-integrity helper
service
UIAccess
alternate process-query mechanism
```

solely for this feature.

If process-image inspection is unavailable in an unsupported cross-integrity/foreign-session arrangement, do not expand the product scope in this PR.

Supported product scope remains one interactive user/session.

---

## 20. Expected production files

Expected focused changes:

```text
src/ClawHUD/ProductionTargetPolicy.cpp
    add SteamInputAddonforClaw.Overlay.exe to rejected targets

src/ClawHUD/GameDetection/GameSessionController.h
    tracked Overlay HWND
    SteamAddonOverlayVisible() query
    minimal private startup/event helpers

src/ClawHUD/GameDetection/GameSessionController.cpp
    startup one-shot visible-window scan
    SHOW/HIDE/DESTROY handling
    exact executable identification
    existing reconcileHudVisibility hook reuse

src/ClawHUD/HudModel.h
src/ClawHUD/HudModel.cpp
    resolver gets transient Overlay-visible input

src/ClawHUD/HudController.h
src/ClawHUD/HudController.cpp
    pass Overlay-visible input through existing reconcile path

src/ClawHUD/App.cpp
    ReconcileHudVisibility input
    TryResumeRecovery expected-visible input
```

`App.h` should not need new Overlay state.

Do not add a new manager/service source file unless current code makes the small helpers impossible to keep readable.

---

## 21. Required unit tests

### 21.1 HudModelTests

Extend `tests/HudModelTests.cpp`.

Required cases:

```text
hudEnabled=false
mode=Always
foreground=true
overlay=true
=> hidden

hudEnabled=true
mode=Always
foreground=false
overlay=false
=> visible

hudEnabled=true
mode=InGameOnly
foreground=false
overlay=false
=> hidden

hudEnabled=true
mode=InGameOnly
foreground=false
overlay=true
=> visible

hudEnabled=true
mode=InGameOnly
foreground=true
overlay=false
=> visible

hudEnabled=true
mode=InGameOnly
foreground=true
overlay=true
=> visible
```

This is the primary deterministic contract for the feature.

### 21.2 ProductionTargetPolicyTests

Extend `tests/ProductionTargetPolicyTests.cpp`.

Required:

```cpp
IsRejectedProductionTargetImage(
    L"steaminputaddonforclaw.overlay.exe")
== true
```

Also prove normalized eligibility rejects path/case variants:

```text
C:\...\SteamInputAddonforClaw.Overlay.EXE
=> not eligible production target
```

### 21.3 Existing window-source tests

Do not rewrite `ProductionGameWindowSourceTests`.

Its existing SHOW/HIDE/DESTROY event mapping and top-level filtering remain the source contract.

Only extend it if implementation changes a pure source-level helper that genuinely belongs there.

Do not add dependency injection solely to unit-test Windows executable-name resolution.

---

## 22. Required build / CI validation

Run the normal `integration/steamaddon` build path.

At minimum:

```text
CMake configure
Release x64 build
CTest Release
SteamAddon Managed Runtime packaging/build checks used by this branch
```

All existing ClawHUD tests must remain green.

No SteamAddon repository PR is required.

---

## 23. Required hardware/manual validation

Validate on the supported MSI Claw / SteamAddon environment.

### Case A — InGameOnly, no game

```text
HUD enabled
Display Mode = InGameOnly
no foreground game
Overlay closed
=> HUD hidden

open SteamAddon Overlay
=> HUD appears

close Overlay
=> HUD hides
```

### Case B — game already active

```text
HUD enabled
InGameOnly
supported game foreground
=> HUD visible

open Overlay
=> HUD stays visible

close Overlay
=> HUD stays visible
```

### Case C — Always

```text
HUD enabled
Always
=> HUD visible before / during / after Overlay
```

No unnecessary presentation flicker should be introduced.

### Case D — HUD disabled

```text
HUD disabled
open Overlay
=> HUD remains hidden
```

### Case E — Overlay already visible when ClawHUD starts

```text
SteamAddon Overlay visible
-> start Managed ClawHUD
-> startup one-shot scan detects visible Overlay
-> HUD enabled + InGameOnly + no game
=> HUD appears
```

This proves the startup scan.

### Case F — Enable HUD while Overlay is already visible

```text
Overlay visible
HUD disabled
-> enable HUD
=> HUD appears without closing/reopening Overlay
```

### Case G — Overlay process exits/crashes while visible

```text
Overlay visible
HUD visible only because of Overlay
-> Overlay HWND destroyed
=> ClawHUD returns to normal policy
=> no game + InGameOnly => HUD hides
```

Do not build a crash watchdog for this test.

The existing DESTROY observation is sufficient.

### Case H — suspend/resume

```text
Overlay visible
HUD visible because of Overlay
-> suspend
=> lifecycle hide

-> resume
=> existing recovery completes
=> if Overlay is still visible, HUD may show again
=> if Overlay is gone, normal policy applies
```

No Overlay condition may bypass the existing suspend/recovery gate.

---

## 24. Explicit non-goals

Do not modify SteamAddon.

Do not add:

- ClawHUD Control IPC operation;
- protocol version change;
- SteamAddon frontend contract change;
- SteamAddon Overlay message;
- new ClawHUD setting;
- new SteamAddon setting;
- polling;
- heartbeat;
- lease;
- epoch;
- window-state manager;
- generic process monitor;
- generic external-surface framework;
- process parent tracking;
- Addon Runtime lifecycle detection;
- Addon installation detection;
- game-detection redesign;
- PresentMon target changes;
- FPS-target substitution;
- HUD renderer changes;
- VRR presentation changes;
- controller lifecycle changes;
- CTW integration.

Do not merge unrelated main-branch F8/manual-override changes into `integration/steamaddon` as part of this PR.

---

## 25. Logging

Keep logging small and transition-based.

Useful Info/Debug examples:

```text
[SteamAddonOverlay] detected hwnd=... pid=...
[SteamAddonOverlay] visible
[SteamAddonOverlay] hidden
[SteamAddonOverlay] destroyed
[SteamAddonOverlay] startup-visible hwnd=... pid=...
```

Do not log every unrelated top-level window event.

Do not dump process lists.

Process-image inspection failure for an unrelated window is not an error.

A failure to identify the Overlay simply means the temporary visibility enhancement is unavailable for that event.

---

## 26. Failure policy

This feature is optional presentation convenience.

Required fail-safe behavior:

```text
Overlay detection unavailable
=> existing ClawHUD Always / InGameOnly behavior continues

SteamAddon Overlay not found at startup
=> normal behavior

one unrelated process cannot be inspected
=> ignore it

ClawHUD HUD Show fails
=> existing HudController failure behavior

ClawHUD HUD Hide fails
=> existing HudController failure behavior
```

Never turn an Overlay-detection failure into:

```text
ClawHUD process exit
SteamAddon controller recovery
PID transition
HidHide mutation
VIIPER action
```

---

## 27. Acceptance criteria

The PR is complete when all of the following are true:

- `SteamInputAddonforClaw.Overlay.exe` is centrally rejected as a production game target.
- No SteamAddon code change exists.
- No new ClawHUD/SteamAddon IPC operation exists.
- No new user option exists.
- Existing `ProductionGameWindowSource` is reused.
- ClawHUD tracks the exact SteamAddon Overlay top-level HWND.
- SHOW causes visibility reconciliation.
- HIDE causes visibility reconciliation.
- DESTROY clears the tracked HWND and reconciles.
- ClawHUD startup performs one visible-window scan after WinEvent observation is active.
- No polling/timer/heartbeat is added.
- HUD disabled always stays hidden.
- Always mode behavior is unchanged.
- InGameOnly shows while Overlay is visible even with no foreground game.
- Overlay close returns to the existing game/mode policy.
- Existing telemetry start/stop effects are reused.
- Suspend/resume lifecycle gate remains stronger than Overlay visibility.
- Resume expected-visibility logic includes Overlay visibility.
- Existing game detection remains authoritative for game state.
- Existing controller/Full1902 ownership is untouched.
- Release x64 build and CTest pass.
- Manual Overlay open/close, startup-visible, process-exit, and suspend/resume cases pass.

---

## 28. PR review focus

Treat as blocking only concrete regressions that are realistically reachable in supported product use, for example:

- Overlay open does not show enabled InGameOnly HUD outside a game;
- Overlay close leaves the HUD shown with no remaining visibility reason;
- ClawHUD startup misses an already-visible Overlay;
- Overlay executable becomes a game/FPS target;
- Overlay visibility bypasses suspend/resume hiding;
- telemetry stays active after the HUD should have hidden;
- game-active HUD hides when Overlay closes;
- HUD disabled is revived by Overlay;
- SteamAddon IPC/protocol is unnecessarily modified;
- a polling/process-watcher architecture is introduced;
- Full1902/controller ownership is coupled to this feature.

Do **not** block for theoretical event interleavings that require artificial scheduler timing when the existing HWND observation and reconciliation path converges correctly during normal window lifecycle.

Do not add locks, epochs, barriers, leases, retry frameworks, or another state authority solely for speculative races.

The intended implementation is deliberately small:

```text
existing HWND event source
+ one startup scan
+ one tracked Overlay HWND
+ one visibility input
+ existing HUD reconcile path
```
