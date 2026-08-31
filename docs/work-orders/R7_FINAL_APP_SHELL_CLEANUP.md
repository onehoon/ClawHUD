# Work Order — R7: Final `App` Shell Cleanup

Status: implementation work order  
Prepared from `main` at `0f5b51bcd615d6c2025d42ab21f93b4f9e243f63` after R6 / PR #192 and the R6 bookkeeping commit #193  
Scope: R7 of `docs/APP_REFACTOR_PLAN.md`

---

## 1. Decision after full active-code review

R7 should be the final runtime-architecture cleanup phase.

The controller extractions are already complete:

```text
HudController
ProductionTelemetryController
GameSessionController
DebugObservationController
```

`App` is now legitimately the composition root / mediator. Do **not** create another shell/controller layer merely to reduce line count.

A review of the current active production code shows that `App::Run()` and `App::ProcessMessages()` are already reasonably small and explicit. The real remaining cleanup targets are:

```text
1. duplicated runtime-stop steps in App::Exit() and App::~App()
2. TrayIcon knowing every telemetry timer id and every telemetry implementation method
3. SettingsWindow knowing App's raw message HWND + WM_APP + 1 destruction message
4. App public methods that are now internal-only or have no active caller
5. a small amount of stale include/comment/facade surface left by R0-R6
```

R7 is therefore a **shell cleanup**, not a redesign.

---

## 2. Current architecture to preserve

The intended final runtime structure remains:

```text
main.cpp
  |
  `-- App
       |
       |-- HudSettingsStore
       |-- TrayIcon
       |-- HudController
       |       `-- HudPresentation        # VRR-critical black box
       |
       |-- PresentMonTelemetryProvider    # ONE shared API2 authority
       |
       |-- ProductionTelemetryController
       |
       |-- GameSessionController
       |
       |-- optional DebugObservationController
       |
       |-- lazy SettingsWindow
       |
       `-- TweakStartupCoordinator
```

`App` remains responsible for:

```text
composition / wiring
startup order
shutdown order
SettingsWindow / TrayIcon facade
settings persistence around controller mutations
cross-controller reactions
suspend/resume orchestration
message loop
single-instance / update / hardware gate
```

That is a valid final architecture.

Do not create:

```text
ApplicationShell
RuntimeHost
AppController
RuntimeCoordinator
ServiceContainer
DI container
generic event bus
```

---

## 3. Files reviewed for R7 scope

Before implementation, re-read the current versions of:

```text
src/ClawHUD/main.cpp
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/TrayIcon.h
src/ClawHUD/TrayIcon.cpp
src/ClawHUD/SettingsWindow.h
src/ClawHUD/SettingsWindow.cpp
src/ClawHUD/SettingsWindow.Settings.cpp
src/ClawHUD/SettingsWindow.Tweaks.cpp

src/ClawHUD/HudController.h
src/ClawHUD/ProductionTelemetryController.h
src/ClawHUD/GameDetection/GameSessionController.h
src/ClawHUD/GameDetection/DebugObservationController.h
src/ClawHUD/Tweaks/TweakStartupCoordinator.h
src/ClawHUD/Tweaks/TweakStartupCoordinator.cpp
src/ClawHUD/SuspendResumePolicy.h

docs/APP_REFACTOR_PLAN.md
docs/APP_REFACTOR_BEHAVIOR_INVENTORY_TEMPLATE.md
docs/HUD_PRESENTATION_REFACTOR_GUARDRAIL.md
```

Also use repository-wide code search before deleting any public method.

Archive-only references do **not** keep an active production facade method alive.

---

## 4. R7 must not change production policy

This PR is cleanup only.

Do not change:

```text
HUD visibility policy
Always / InGameOnly behavior
F8 behavior
game detection state machine
candidate precedence
generation rules
Steam RunningAppID semantics
Microsoft identity semantics
FirstDisplayedFrame renderer proof
Alt+Tab retention
process-lifetime release
FPS target policy
telemetry retention
EC protocol/decoding
battery estimator behavior
graphics API retry policy
suspend/resume retry policy
startup update behavior
hardware support gate
VRR tweak behavior
```

No feature work belongs in R7.

---

# Part A — Centralize only the truly duplicated runtime-stop sequence

## 5. Current duplicate shutdown block

Both `App::~App()` and `App::Exit()` currently perform this same runtime-stop sequence:

```text
CancelResumeRecovery()
StopProductionSampling(false, "app-shutdown")
ProductionTelemetryController::StopGraphicsApiProbe()
GameSessionController::StopSources()
DebugObservationController::Stop() if present
UnregisterHotKey(F8) if registered
hudHotkeyRegistered_ = false
```

This is the correct candidate for one private helper.

Recommended name:

```cpp
void StopRuntimeSources();
```

or another equally explicit name.

Do not call it `ShutdownEverything()` or build a new lifecycle abstraction.

---

## 6. Required `StopRuntimeSources()` order

Preserve the current effective order exactly:

```text
1. CancelResumeRecovery()

2. StopProductionSampling(false, L"app-shutdown")
   -> ProductionTelemetryController::StopSamplingTimersAndFps()
   -> no GameRenderVerifier stop here because argument == false
   -> ProductionTelemetryController::ResetSamplingState("app-shutdown")

3. ProductionTelemetryController::StopGraphicsApiProbe()

4. GameSessionController::StopSources()
   -> ProductionGameWindowSource.Stop
   -> ProductionProcessLifetimeWatcher.Disarm
   -> ForegroundTracker.Stop
   -> GameRenderVerifier stop / FPS clear as currently implemented
   -> SteamRunningAppIdSource.Stop
   -> pending game-session message drain

5. if (debugObservation_)
       DebugObservationController::Stop()

6. if (hudHotkeyRegistered_ && tray_.Window())
       UnregisterHotKey(...)

7. hudHotkeyRegistered_ = false
```

No ordering changes just because the calls move into one helper.

---

## 7. Do not over-centralize final object teardown

`App::~App()` and `App::Exit()` are **not identical after the shared runtime-stop block**.

Preserve that distinction.

### Destructor must remain effectively:

```text
Log("ClawHUD exiting")
StopRuntimeSources()
HudController::DestroyPresentation()
settings_.reset()
tray_.Destroy()
release/close instance mutex
```

### Explicit `Exit()` must remain effectively:

```text
if exiting_:
    return

exiting_ = true
StopRuntimeSources()
settings_.reset()
tray_.Destroy()
PostQuitMessage(0)
```

Do **not** force `HudController::DestroyPresentation()` into the shared helper merely to make both functions textually identical.

The current explicit Exit path leaves final presentation object destruction to normal `App` destruction after the message loop exits. Preserve that unless a separate behavior-focused PR proves a need to change it.

Do not move mutex release into `Exit()`.

Do not add a new `runtimeStopped_` flag or shutdown state machine solely to suppress the existing idempotent second stop during final object destruction.

---

## 8. `TweakStartupCoordinator` lifecycle

`TweakStartupCoordinator` currently stops through its own destructor.

R7 does **not** need to redesign that ownership or add it to the common stop helper solely for symmetry.

Do not alter its retry sequence, timing, worker implementation, or Intel VRR tweak behavior in R7.

If implementation-time inspection discovers a concrete shutdown correctness issue, stop and report it separately rather than expanding R7 silently.

---

# Part B — Collapse TrayIcon telemetry timer dispatch into one App facade method

## 9. Current problem

`TrayIcon::WindowProc(WM_TIMER)` currently knows all of these implementation details:

```text
kEcHudTimerId
    -> App::SampleProductionTelemetry()

kBatteryHudTimerId
    -> App::SampleProductionBatteryTelemetry()

kGraphicsApiRetryTimerId
    -> App::TryGraphicsApiProbe()

kResumeRecoveryTimerId
    -> App::TryResumeRecovery()

kPresentMonFpsTimerId
    -> App::SampleProductionFpsTelemetry()
```

This is the deferred R2 cleanup and is also whole-refactor completion criterion #8:

> `TrayIcon` should not need to know each telemetry timer's implementation method.

R7 should finish this.

---

## 10. Add one public App timer facade

Recommended public API:

```cpp
void HandleTimer(UINT_PTR timerId);
```

Then simplify `TrayIcon` to:

```cpp
if (message == WM_TIMER)
{
    self->app_.HandleTimer(static_cast<UINT_PTR>(wParam));
    return 0;
}
```

`TrayIcon` should no longer branch on individual production telemetry timer ids.

Do not move telemetry sampling implementation into `TrayIcon`.

Do not add a generic message bus.

---

## 11. `App::HandleTimer` semantics — preserve exact guards

The current wrappers do **not** all have the same guard.

Preserve this exactly.

### EC/system sample

Current behavior:

```cpp
if (suspended_ || !hudController_.Enabled() || !HudVisible())
    return;
productionTelemetry_.SampleSystemEc();
```

### Battery sample

Current behavior:

```cpp
if (suspended_ || !hudController_.Enabled() || !HudVisible())
    return;
productionTelemetry_.SampleBattery();
```

### FPS sample

Current behavior:

```cpp
if (suspended_ || !hudController_.Enabled() || !HudVisible())
    return;
productionTelemetry_.SampleFps();
```

### Graphics API retry

Current behavior is intentionally just:

```cpp
productionTelemetry_.TryGraphicsApiProbe();
```

Do **not** accidentally add the HUD-visible/suspended guard to this timer.

### Resume recovery timer

Preserve:

```cpp
TryResumeRecovery();
```

### Unknown timer id

No-op.

No fallthrough side effects.

---

## 12. Remove obsolete per-timer App forwarding methods

After `TrayIcon` is migrated, remove these active App facade methods if repository search confirms no remaining active caller:

```text
App::SampleProductionTelemetry
App::SampleProductionBatteryTelemetry
App::SampleProductionFpsTelemetry
App::TryGraphicsApiProbe
```

Do not retain compatibility wrappers; the app is unreleased and the internal callers are migrated in the same PR.

`TryResumeRecovery()` remains implemented but should become private because `TrayIcon` no longer calls it directly.

---

## 13. Move resume timer id out of `App.h`

After `TrayIcon` no longer dispatches `kResumeRecoveryTimerId` directly, the id is App-internal message/timer wiring.

Move:

```cpp
constexpr UINT_PTR kResumeRecoveryTimerId = 5;
```

from `App.h` into the private/anonymous namespace in `App.cpp`.

Keep numeric value `5` unchanged.

Do not renumber any telemetry timer id.

Update the stale comment in `SuspendResumePolicy.h` that currently says the Win32 timer id lives in `App.h`; after R7 it should say the timer id remains App runtime/message-loop wiring, without claiming it is in the header.

`kResumeRecoveryIntervalMs = 500` and max attempts `6` remain in `SuspendResumePolicy.h` unchanged.

---

# Part C — Hide raw Settings destruction message plumbing behind App

## 14. Current problem

`SettingsWindow::WM_NCDESTROY` currently knows both:

```text
App::MessageWindow()
WM_APP + 1
```

and posts directly:

```cpp
PostMessageW(self->app_.MessageWindow(), WM_APP + 1, 0, 0);
```

This leaks raw App shell message plumbing into the Settings UI.

It also forces `App::MessageWindow()` to remain public even though no other active UI surface needs that raw HWND.

R7 should encapsulate this.

---

## 15. Add a semantic asynchronous Settings destruction facade

Recommended public method:

```cpp
void PostSettingsDestroyed();
```

Implementation concept:

```cpp
void App::PostSettingsDestroyed()
{
    if (const HWND window = tray_.Window())
        PostMessageW(window, kSettingsDestroyed, 0, 0);
}
```

Then `SettingsWindow::WM_NCDESTROY` becomes:

```cpp
self->app_.PostSettingsDestroyed();
```

Keep `kSettingsDestroyed = WM_APP + 1` private to `App.cpp`.

Do not expose the message id through a new public constant.

---

## 16. The Settings destruction notification must remain asynchronous

This is critical.

Do **not** replace the current posted message with a direct call such as:

```cpp
self->app_.SettingsDestroyed();
```

from inside `WM_NCDESTROY`.

A direct call can reset/delete the owning `SettingsWindow` object while its own window procedure is still executing.

Preserve the existing ownership-release shape:

```text
SettingsWindow WM_NCDESTROY
    -> post asynchronous App message

App::ProcessMessages
    -> receive kSettingsDestroyed
    -> App::SettingsDestroyed()
    -> settings_.reset()
```

Only the raw HWND/message-id knowledge should be hidden.

---

## 17. App Settings methods after cleanup

After migration:

```text
public:
    OpenSettings()
    PostSettingsDestroyed()

private:
    SettingsDestroyed()
```

Remove public:

```cpp
HWND MessageWindow() const;
```

if repository search confirms no remaining active caller.

Keep the lazy Settings ownership contract unchanged.

---

# Part D — Final App facade audit

## 18. Remove or privatize App methods based on actual active callers

Use repository-wide search against active source before editing.

The current review found the following candidates.

### Remove entirely

`ExecutablePath()` has no active production caller; the only code reference found outside `App` is archived diagnostic material.

Remove the public accessor:

```cpp
const std::wstring& ExecutablePath() const;
```

Keep the private `executablePath_` member because `ApplyStartupRegistration()` still uses it.

Also remove the four obsolete timer forwarding methods listed in §12 after `HandleTimer` migration.

### Move to private

These are App-internal after the current controller extractions:

```text
SettingsDestroyed
TryResumeRecovery
StopHud
RenderProductionHud
HudVisible
```

`HudVisible()` may remain a small private inline helper if it improves readability; it should no longer be public because no active external caller uses it.

### Keep public — TrayIcon facade

```text
OpenSettings
Exit
HandleSystemSuspend
HandleSystemResume
HandleTimer
HandleHudToggleHotkey
```

### Keep public — SettingsWindow facade

```text
StartWithWindows
SetStartWithWindows

HudEnabled
HudSizeOffset
HudOptions
HudFont
SetHudEnabled
SetHudAlignment
SetHudFont
SetHudBackgroundMode
SetHudOpacity
SetHudSizeOffset
SetHudVisibilityMode

IntelVrrRangeFixEnabled
SetIntelVrrRangeFixEnabled
IntelVrrLastResult

PostSettingsDestroyed
```

### Keep public — entry point

```text
Run
```

Do not expose controller references directly to SettingsWindow or TrayIcon merely to shrink App further.

---

## 19. Small SettingsWindow surface cleanup

Repository search shows:

```text
SettingsWindow::RequestClose()
```

has no active caller.

Remove it.

`SettingsWindow::UpdateGeneralControls()` is only used internally by SettingsWindow and can become private.

Keep:

```text
SettingsWindow::Show()
SettingsWindow::Window()
SettingsWindow::UpdateHudControls()
```

public because App still legitimately uses them.

Do not redesign SettingsWindow or its Win32 control layout in R7.

---

# Part E — `Run()` and message pump: simplify only where there is real value

## 20. Do not invent a startup coordinator

Current `App::Run()` is already an explicit composition-root startup sequence.

Preserve this effective order:

```text
AcquireSingleInstance
CheckForUpdates
CheckSupportedHardware
ApplyStartupRegistration
TrayIcon::Create

ProductionTelemetryController::Bind
GameSessionController::BindMessageWindow
GameSessionController::StartWindowSource
GameSessionController::StartSteamWatcher
GameSessionController::InitializeSteamSession

if DebugLog:
    create DebugObservationController lazily
    DebugObservationController::Start

RegisterHotKey(F8)

PresentMonTelemetryProvider::Initialize

GameSessionController::StartForegroundTracking

if persisted HUD enabled:
    HudController::Ensure
    on failure: AbandonEnable + warn
    on success: ReevaluateForeground + ReconcileHudVisibility

TweakStartupCoordinator::Start
ProcessMessages
```

Do not reorder these stages in R7.

In particular, preserve the existing relationship where debug `PresentActivitySource` startup occurs before the shared `PresentMonTelemetryProvider::Initialize()` call. R7 is not the place to revisit that behavior.

Do not extract a `StartupCoordinator` just to reduce visible lines.

A few local helper names are acceptable only if they make the ordering clearer without hiding it.

---

## 21. `ProcessMessages()` is already close to final form

Preserve the current sequence:

```text
GetMessage

if kSettingsDestroyed:
    SettingsDestroyed()
    continue

if GameSessionController::HandleMessage(message):
    continue

if SettingsWindow exists + visible + IsDialogMessage handles it:
    continue

TranslateMessage
DispatchMessage
```

Do not create:

```text
MessageDispatcher
MessageRouter
variant message bus
handler registry
```

The only R7-related change expected here is the Settings destruction facade cleanup around the producer side; the message pump can remain structurally the same.

---

# Part F — include and stale-surface hygiene

## 22. Remove only obviously unused includes

After R4-R6, `App.cpp` still carries historical includes that appear to be declaration-only leftovers.

At implementation time verify and remove unused includes such as:

```text
ProductionTargetPolicy.h
UninstallCleanup.h
```

from `App.cpp` if no symbol in that translation unit uses them.

Likewise remove unused standard-library includes from `App.cpp` / `App.h` only when compile/search confirms they are no longer needed.

Examples worth checking in `App.h`:

```text
<atomic>
<cstddef>
<cstdint>
```

Do not delete the underlying production policy/source files merely because App no longer includes them.

Do not turn R7 into a repo-wide include-style campaign.

---

# Part G — boundaries that are frozen

## 23. HUD presentation / VRR contract — HARD STOP

R7 should have **zero functional diff** in the HUD presentation backend.

Do not modify:

```text
HudPresentation.cpp
HudPresentation.h
HudPresentationContract.*
HudPresentationLifecycle.*
HudRenderer.*
```

Do not modify, replace, weaken, or work around:

```text
HUD windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
existing WS_EX_LAYERED behavior
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
ProductionHudPresentationContract()
Presentation API production path
DirectComposition production path
independent-flip requirement
premultiplied-alpha presentation contract
```

Do not use shell cleanup as a reason to alter presentation destruction/recreation policy.

`App::~App()` must retain its existing `HudController::DestroyPresentation()` placement relative to Settings/tray teardown unless an explicit design review approves a change.

---

## 24. Background opacity remains background-only

R7 contains no opacity work.

Do not modify opacity semantics, renderer alpha behavior, window-wide opacity, visual-wide opacity, or HUD styling.

---

## 25. GameSessionController remains production authority

Expected zero behavioral diff in:

```text
GameSessionController
GameDetectionCoordinator
GameRenderVerifier
SteamRunningAppTrigger
GenericForegroundTrigger
MicrosoftGameTrigger
ProductionGameWindowSource
ProductionProcessLifetimeWatcher
SteamRunningAppIdSource
ForegroundTracker
```

Do not move game policy back into App during cleanup.

Do not make TrayIcon or SettingsWindow aware of game-session internals.

---

## 26. ProductionTelemetryController remains telemetry owner

Expected no telemetry semantic change.

Timer **dispatch location** changes from TrayIcon to App, but the controller continues to own:

```text
sampling state
EC/system/battery/FPS values
graphics API probe
retention
FPS target cache
timer arm/kill lifecycle
```

App only applies the same top-level HUD/suspend guard before calling the existing sample method.

Do not move telemetry fields or timer lifecycle back to App.

---

## 27. DebugObservationController remains optional/lazy

Preserve R6:

```text
DebugLog OFF
    -> debugObservation_ == nullptr
    -> no WindowsGameIdentitySource worker

DebugLog ON
    -> controller exists
    -> existing debug observation startup / foreground / stop behavior
```

Do not eagerly construct it during shell cleanup.

---

## 28. Suspend/resume remains App-owned

Preserve R5:

```text
suspended_
resumeRecoveryActive_
resumeRecoveryAttempts_
```

remain in App.

R7 timer dispatch consolidation may make `TryResumeRecovery()` private, but must not change its body/order/policy.

Do not create a lifecycle controller.

---

## 29. One shared PresentMon provider

Preserve exactly one App-owned:

```text
PresentMonTelemetryProvider presentMonTelemetryProvider_;
```

The telemetry, game-session verifier, and debug observation code continue to reference the same provider.

No second provider/API2 client/session.

No `PresentMon.exe`.

---

# Part H — Settings tray-only memory contract

## 30. SettingsWindow remains lazy

Required invariant:

```text
normal Windows/tray startup
    -> SettingsWindow is NOT constructed

explicit Settings open
    -> App::OpenSettings()
    -> create SettingsWindow only when settings_ == nullptr

Settings close/minimize destroy
    -> SettingsWindow WM_NCDESTROY
    -> asynchronous App notification
    -> App message pump
    -> settings_.reset()
```

Do not move Settings construction into:

```text
App constructor
App::Run startup
TrayIcon::Create
any controller constructor
```

Do not replace the lazy `std::unique_ptr<SettingsWindow>` with an always-live object.

---

# Part I — What NOT to clean up in R7

## 31. Explicitly out of scope

Do not mix any of the following into R7:

```text
HudSettingsStore -> AppSettingsStore rename
Settings UI redesign
Diagnostics wording cleanup unrelated to this PR
CMake/test-target organization (R8)
new diagnostic console app
game detection changes
PresentMon provider/session refactor
EC helper refactor
renderer/style/font/layout changes
opacity implementation changes
Intel VRR tweak logic changes
Velopack/update behavior changes
hardware support changes
```

R7 should remain a reviewable shell cleanup.

---

# Part J — Behavior inventory required in PR

## 32. Shutdown inventory

PR body must record exact before/after ordering for:

```text
App::~App
App::Exit
new StopRuntimeSources helper
```

Explicitly state what stayed outside the helper and why:

```text
HudController::DestroyPresentation
settings_.reset
TrayIcon::Destroy
instance mutex release
PostQuitMessage
```

---

## 33. Timer inventory

Record mapping:

```text
kEcHudTimerId
    -> same HUD/suspend guard
    -> SampleSystemEc

kBatteryHudTimerId
    -> same HUD/suspend guard
    -> SampleBattery

kPresentMonFpsTimerId
    -> same HUD/suspend guard
    -> SampleFps

kGraphicsApiRetryTimerId
    -> TryGraphicsApiProbe with no new visibility/suspend guard

kResumeRecoveryTimerId
    -> TryResumeRecovery
```

Confirm numeric ids unchanged.

---

## 34. Settings-destruction inventory

Record:

```text
before:
SettingsWindow WM_NCDESTROY
 -> PostMessage(App::MessageWindow(), WM_APP + 1)

new:
SettingsWindow WM_NCDESTROY
 -> App::PostSettingsDestroyed()
 -> PostMessage(tray HWND, private kSettingsDestroyed)

message pump
 -> SettingsDestroyed()
 -> settings_.reset()
```

Explicitly confirm notification remains asynchronous.

---

# Part K — Tests and verification

## 35. Required tests

Run the full active suite.

Current baseline is:

```text
46/46
```

At minimum pay attention to:

```text
SuspendResumeRecoveryTests
HudPresentationContractTests
HudPresentationLifecycleTests
HudModelTests
HudSizeTests
HudWindowGeometryTests
HudRendererTests

ProductionGameDetectionScenarioTests
GameDetectionCoordinatorTests
GameRenderVerifierTests
ProductionTargetPolicyTests
ForegroundTrackerTests
SteamRunningAppIdSourceTests

PresentMonTelemetryProviderTests
AlwaysModeFpsTargetTests
FpsStaleHoldTests
TelemetryRetentionTests
IntelGraphicsApiProbeTests
WindowsPowerTelemetryTests
MsiEcHudTelemetryTests

WindowsGameIdentitySourceTests
ProcessLifecycleSourceTests
WindowLifecycleSourceTests
PresentActivitySourceTests
```

Do not delete or weaken tests to make cleanup pass.

No fake Win32 framework is required merely to unit-test the small imperative dispatch helper.

---

## 36. Build verification

Required:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Using the repository's established Ninja / BuildTools equivalent is acceptable.

Required result:

```text
ClawHUD.exe builds successfully
all active tests pass
no new warnings
```

---

## 37. Hardware validation policy

Hardware smoke is useful follow-up validation but is **not a merge blocker by itself**.

It is acceptable for the PR body to record:

```text
Hardware smoke: deferred / not performed
```

Do not mark R7 unmergeable solely because MSI Claw hardware was unavailable.

Actual code/test/contract defects remain blockers.

Optional later smoke:

```text
tray-only startup
Settings open/close/reopen
Exit from tray menu
HUD persisted ON/OFF
F8 toggle
Always / InGameOnly
suspend/resume
game commit / Alt+Tab / exit
DebugLog OFF / ON
VRR / click-through / no activation
```

---

# Part L — Documentation cleanup

## 38. Update `APP_REFACTOR_PLAN.md`

After successful R7, record:

```text
R7 PR number
merge commit
shared shutdown helper introduced
TrayIcon timer dispatch consolidated to App::HandleTimer
raw Settings destruction message hidden behind App facade
removed/privatized App facade methods
CTest result
hardware smoke status (informational / non-blocking)
```

Then mark:

```text
Core runtime refactor: COMPLETE at R7
```

Next work should be:

```text
R8 — optional CMake/test declaration organization
```

Make it explicit that R8 is **not required** for runtime architecture completion.

---

## 39. Update `APP_REFACTOR_BEHAVIOR_INVENTORY_TEMPLATE.md`

If the Settings destruction facade changes as described above, update the mandatory lazy-Settings section to reflect the actual final flow:

```text
SettingsWindow WM_NCDESTROY
 -> App::PostSettingsDestroyed()
 -> App message
 -> App::SettingsDestroyed()
 -> settings_.reset()
```

Do not leave the template implying a synchronous Settings object deletion from inside `WM_NCDESTROY`.

Update stale caller checklist wording if needed after App facade cleanup.

---

## 40. Update `SuspendResumePolicy.h` comment if timer id moves

If `kResumeRecoveryTimerId` moves from `App.h` to `App.cpp`, update only the explanatory comment.

Do not change any constexpr suspend/resume policy value or predicate.

---

## 41. Hardware-smoke wording in docs

Do not introduce language that makes deferred real-hardware smoke a merge blocker.

Where R7 adds new progress text, use the current project policy:

```text
hardware smoke deferred / recommended follow-up / non-blocking by itself
```

Do not rewrite historical phase records unnecessarily unless they directly conflict with the current R7 completion statement.

---

# Part M — Repository-wide final checks

## 42. TrayIcon knowledge check

After R7, search active code for:

```text
kEcHudTimerId
kBatteryHudTimerId
kGraphicsApiRetryTimerId
kPresentMonFpsTimerId
```

Expected:

```text
TrayIcon.cpp does NOT branch on them.
App/ProductionTelemetryController may still reference them as appropriate.
```

`TrayIcon::WindowProc(WM_TIMER)` should make one App call.

---

## 43. App dead-surface check

Search active source for removed/privatized methods.

Expected no active external caller for:

```text
ExecutablePath
MessageWindow
SampleProductionTelemetry
SampleProductionBatteryTelemetry
SampleProductionFpsTelemetry
TryGraphicsApiProbe
StopHud
RenderProductionHud
HudVisible
TryResumeRecovery
SettingsDestroyed
```

The last five may still exist privately where needed.

Do not keep a public method solely because archived code references it.

---

## 44. Settings message-plumbing check

After R7:

```text
SettingsWindow.cpp must not contain raw `WM_APP + 1` for owner destruction.
SettingsWindow.cpp must not request the App tray/message HWND.
kSettingsDestroyed remains App-private.
```

---

## 45. Shared provider check

Search construction of:

```text
PresentMonTelemetryProvider
```

Expected runtime owner:

```text
App only
```

Controllers retain references only.

---

## 46. Presentation diff gate

Before completion verify zero unintended diff in:

```text
HudPresentation.*
HudPresentationContract.*
HudPresentationLifecycle.*
HudRenderer.*
```

If any protected presentation contract changes appear, R7 is invalid.

---

# Expected final App shell

## 47. Expected public surface

Approximately:

```cpp
class App
{
public:
    explicit App(HINSTANCE instance);
    ~App();

    int Run();

    // Tray shell
    void OpenSettings();
    void Exit();
    void HandleSystemSuspend();
    void HandleSystemResume();
    void HandleTimer(UINT_PTR timerId);
    void HandleHudToggleHotkey();

    // Settings ownership notification
    void PostSettingsDestroyed();

    // Settings facade
    bool StartWithWindows() const noexcept;
    void SetStartWithWindows(bool enabled);

    bool HudEnabled() const noexcept;
    int HudSizeOffset() const noexcept;
    const clawhud::HudLayoutOptions& HudOptions() const noexcept;
    clawhud::HudFont HudFont() const noexcept;
    bool SetHudEnabled(bool enabled);
    void SetHudAlignment(...);
    void SetHudFont(...);
    void SetHudBackgroundMode(...);
    bool SetHudOpacity(...);
    void SetHudSizeOffset(...);
    void SetHudVisibilityMode(...);

    bool IntelVrrRangeFixEnabled() const noexcept;
    void SetIntelVrrRangeFixEnabled(bool enabled);
    std::optional<clawhud::IntelVrrRunResult> IntelVrrLastResult() const;

private:
    // startup / shell
    bool AcquireSingleInstance();
    void CheckForUpdates();
    int ProcessMessages();
    void StopRuntimeSources();

    // Settings ownership/persistence
    void SettingsDestroyed();
    ...

    // cross-controller orchestration
    void ReconcileHudVisibility();
    void RenderProductionHud(bool allowHidden = false);
    void StartProductionSampling();
    void StopProductionSampling(...);
    void PauseProductionSamplingForSuspend();
    void CancelResumeRecovery();
    void TryResumeRecovery();
    void StopHud();
    bool HudVisible() const noexcept;

    ...
};
```

Exact ordering/style may follow project convention.

The key goal is not a specific line count. The key goal is that the public facade contains only real UI/tray entry points and user-facing settings operations.

---

# Acceptance criteria

R7 is complete when all of the following are true:

1. No new controller/shell abstraction is introduced.
2. `App` remains the composition root/mediator.
3. Common runtime-stop calls are centralized without changing their order.
4. Destructor-specific presentation/mutex teardown remains distinct from explicit `Exit()` teardown.
5. No new shutdown state machine/flag is introduced solely for deduplication.
6. `TrayIcon::WindowProc(WM_TIMER)` forwards to one `App::HandleTimer()` entry point.
7. TrayIcon no longer dispatches individual telemetry timer implementations.
8. EC/battery/FPS timer guards are unchanged.
9. graphics API retry timer does not gain an accidental HUD/suspend guard.
10. resume timer semantics and numeric id are unchanged.
11. `kResumeRecoveryTimerId` is App-internal if no external caller remains.
12. `SuspendResumePolicy` values/predicates are unchanged.
13. Settings destruction remains asynchronous.
14. SettingsWindow no longer knows App's raw message HWND / `WM_APP + 1` destruction plumbing.
15. `SettingsWindow` still releases ownership through App message handling + `settings_.reset()`.
16. `SettingsWindow` remains lazy-created only by explicit Settings open.
17. `ExecutablePath()` public accessor is removed if still unused by active code.
18. obsolete per-timer App forwarding methods are removed.
19. App-internal helpers are private rather than public.
20. `SettingsWindow::RequestClose()` is removed if still dead.
21. `SettingsWindow::UpdateGeneralControls()` is private if still internal-only.
22. `Run()` startup ordering is unchanged.
23. `ProcessMessages()` remains explicit and simple; no generic dispatcher is added.
24. one shared App-owned PresentMon provider remains.
25. GameSessionController remains production game-detection authority.
26. ProductionTelemetryController remains telemetry state/timer-lifecycle owner.
27. DebugObservationController remains lazy and debug-only.
28. suspend/resume top-level state remains App-owned.
29. `HudPresentation` production contract is unchanged.
30. click-through / no activation / topmost / transparent hit-test invariants are unchanged.
31. independent flip and premultiplied-alpha requirements are unchanged.
32. background opacity semantics are untouched.
33. no legacy PresentMon.exe or in-app diagnostic path is reintroduced.
34. no CMake/R8 cleanup is mixed into R7.
35. unused include cleanup is limited and compile-verified.
36. `APP_REFACTOR_BEHAVIOR_INVENTORY_TEMPLATE.md` reflects the final lazy Settings destruction flow.
37. `APP_REFACTOR_PLAN.md` records R7 as core runtime refactor completion.
38. R8 is documented as optional only.
39. full Release build succeeds.
40. full active CTest suite passes.
41. hardware smoke may be deferred and is not itself a merge blocker.
42. final repository search confirms one clear owner for every runtime state domain.
