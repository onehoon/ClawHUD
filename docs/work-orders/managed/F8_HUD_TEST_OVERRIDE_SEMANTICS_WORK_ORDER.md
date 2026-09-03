# F8 HUD Test Override Semantics Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Area:** HUD runtime control / test-only visibility override  
> **Analyzed main HEAD:** `35299f514270233a7c75e4babfa2705a36d555a1`  
> **Scope:** Make F8 a non-persistent show/hide override that is available only while the persisted `Enable HUD` master setting is On  
> **Expected implementation:** one small PR  
> **Status:** Ready for implementation

---

## 1. Objective

ClawHUD currently has two different HUD controls:

```text
Enable HUD
    = persisted master enable/disable setting

F8
    = global runtime visibility override used mainly for testing
```

These must have clearly separated semantics.

The intended contract is:

```text
Enable HUD = Off
    -> HUD runtime is disabled
    -> F8 does nothing
    -> F8 must not initialize, enable, show, or otherwise revive the HUD

Enable HUD = On
    -> normal configured visibility policy applies by default
    -> F8 may temporarily force the HUD shown or hidden
    -> this is a runtime/test override only
    -> it is never persisted
```

F8 is intentionally useful for testing. Therefore, while `Enable HUD = On`, the manual override is allowed to supersede the configured `Always` / `In-game only` visibility policy in **both** directions:

```text
force show
force hide
```

For example, `In-game only` with no detected game may still be force-shown by F8 for testing. That behavior is intentional.

This work is not a new user-facing HUD mode and must not become a persisted setting.

---

## 2. Current behavior on main

### 2.1 Current F8 handler

`App::HandleHudToggleHotkey()` currently contains this behavior:

```cpp
void App::HandleHudToggleHotkey()
{
    if (!hudController_.Enabled())
    {
        if (!hudController_.Ensure())
        {
            RuntimeLogger::Log(..., L"F8 HUD ON initialization failed");
            return;
        }

        hudController_.MarkEnabled(false);
        gameSession_.ReevaluateForeground();
        ...
    }

    hudController_.SetManualOverride(!HudVisible());
    ReconcileHudVisibility();
    ...
}
```

This means F8 currently does more than a visibility override.

When the persisted HUD master state is Off, F8 can:

```text
Ensure() the HUD presentation
MarkEnabled(false) -> set runtime enabled_=true without logging the transition
game-session reevaluation
start a graphics API probe
set a manual visibility override
show the HUD
```

The persisted `HUDEnabled=false` value is not rewritten, so the running state can temporarily diverge from the persisted master setting.

That is not the intended contract.

### 2.2 Current visibility resolver is already suitable

The current resolver is:

```cpp
bool ResolveHudVisible(bool hudEnabled,
    std::optional<bool> manualOverride,
    HudVisibilityMode mode,
    bool foregroundActive) noexcept
{
    return hudEnabled &&
        (manualOverride
            ? *manualOverride
            : ShouldShowHud(mode, foregroundActive));
}
```

This part is conceptually correct for the intended semantics:

```text
master disabled
    -> always hidden, even if manualOverride=true

master enabled + no override
    -> normal Always / In-game only policy

master enabled + override=true
    -> force shown

master enabled + override=false
    -> force hidden
```

Do not replace this with a larger visibility state machine.

### 2.3 Existing override lifecycle is mostly correct

Current `HudController` behavior already gives the temporary override the right lifetime:

```text
RestoreState(...)
    -> does not restore manualOverride_
    -> app restart starts with no F8 override

MarkDisabled()
    -> manualOverride_.reset()

SetHudEnabled(true)
    -> ResetManualOverride()

SetVisibilityMode(...)
    -> manualOverride_.reset()
```

Keep these properties.

---

## 3. Required final semantics

### 3.1 Master setting

`Enable HUD` is authoritative.

```text
Enable HUD = Off
    -> no HUD presentation should be created because of F8
    -> no game reevaluation should be triggered because of F8
    -> no graphics API probe should be started because of F8
    -> no production sampling should be started because of F8
    -> no F8 state should be persisted
```

The global hotkey may remain registered while HUD is disabled. The handler simply ignores it.

Do not add hotkey registration/unregistration churn around `Enable HUD` just to implement this rule.

### 3.2 F8 when HUD is enabled

When `hudController_.Enabled() == true`, F8 is a two-direction runtime override:

```text
HUD currently visible
    -> F8 sets manual override = false
    -> force hidden

HUD currently hidden
    -> F8 sets manual override = true
    -> force shown
```

The current visibility state is the toggle input.

The override remains active until another event intentionally clears it or until the process exits.

### 3.3 Interaction with visibility modes

#### Always

```text
Enable HUD = On
Visibility = Always
normal state = shown

F8
    -> force hide

F8 again
    -> force show
```

#### In-game only, foreground game active

```text
Enable HUD = On
Visibility = In-game only
foreground game active
normal state = shown

F8
    -> force hide

F8 again
    -> force show
```

#### In-game only, no foreground game

```text
Enable HUD = On
Visibility = In-game only
no foreground game
normal state = hidden

F8
    -> force show

F8 again
    -> force hide
```

The force-show case is intentional because F8 is primarily a test override.

Do not change F8 into a hide-only feature in this work order.

### 3.4 Override reset rules

The F8 override must remain non-persistent.

Required reset behavior:

```text
application restart
    -> override cleared

Enable HUD -> Off
    -> override cleared as part of normal StopHud / MarkDisabled path

Enable HUD -> On after being Off
    -> starts from configured visibility policy, not the old F8 state

visibility mode changes
    -> override cleared and new configured policy becomes authoritative
```

Current code already provides these reset points. Preserve them unless a concrete bug is found.

### 3.5 Suspend / resume

Do not invent a new suspend-specific F8 reset.

The current session-level override may survive a normal suspend/resume cycle. Resume recovery already computes expected visibility using the manual override.

Preserve the existing suspend/resume recovery architecture.

---

## 4. Required implementation change

The main functional correction is deliberately small.

### 4.1 F8 must return immediately when HUD is disabled

Replace the current runtime-enable branch with an early return.

Conceptually:

```cpp
void App::HandleHudToggleHotkey()
{
    if (!hudController_.Enabled())
    {
        RuntimeLogger::Log(RuntimeLogLevel::Debug,
            L"F8 HUD override ignored: HUD disabled");
        return;
    }

    const bool forceVisible = !HudVisible();
    hudController_.SetManualOverride(forceVisible);

    RuntimeLogger::Log(RuntimeLogLevel::Info,
        forceVisible
            ? L"F8 HUD override=show"
            : L"F8 HUD override=hide");

    ReconcileHudVisibility();

    if (settings_)
        settings_->UpdateHudControls();
}
```

The exact log wording may differ, but the functional requirements are fixed.

Delete the F8-only calls that currently attempt to revive a disabled HUD:

```text
hudController_.Ensure()
hudController_.MarkEnabled(false)
gameSession_.ReevaluateForeground()
productionTelemetry_.StartGraphicsApiProbe(...)
```

None of those belong in the disabled-F8 path.

### 4.2 Do not change `SetHudEnabled()` semantics

The persisted master control remains the only path that enables/disables the HUD runtime.

Keep the existing normal enable flow conceptually:

```text
SetHudEnabled(true)
-> Ensure presentation
-> MarkEnabled
-> reevaluate foreground
-> clear manual override
-> reconcile visibility
-> persist HUDEnabled=true
```

Keep the existing disable flow conceptually:

```text
SetHudEnabled(false)
-> StopHud
-> stop/release the appropriate runtime resources
-> clear manual override through MarkDisabled
-> persist HUDEnabled=false
```

Do not route `SetHudEnabled()` through F8 logic.

### 4.3 Optional small pure policy helper

A tiny pure helper is acceptable if it materially improves regression testing, but do not build a new hotkey subsystem or state machine.

For example:

```cpp
std::optional<bool> ResolveHudHotkeyOverride(
    bool hudEnabled,
    bool currentlyVisible) noexcept
{
    if (!hudEnabled)
        return std::nullopt; // ignore F8

    return !currentlyVisible; // true=force show, false=force hide
}
```

Usage:

```cpp
const auto override = ResolveHudHotkeyOverride(
    hudController_.Enabled(), HudVisible());

if (!override.has_value())
    return;

hudController_.SetManualOverride(*override);
ReconcileHudVisibility();
```

This helper is optional. A direct, well-tested handler fix is also acceptable.

Do not introduce generic command routing, a visibility state machine, or new persisted state for this feature.

---

## 5. Telemetry lifecycle requirements

F8 changes visibility, so the existing `ReconcileHudVisibility()` side effects must continue to own production sampling changes.

Required behavior:

```text
force show
-> HudController::ReconcileVisibility() resolves visible
-> existing startProductionSampling effect
-> App::StartProductionSampling()

force hide
-> HudController::ReconcileVisibility() resolves hidden
-> existing stopProductionSampling effect
-> StopProductionSampling(HudHidden, false)
```

Do not directly start/stop EC/FPS/base sampling inside the F8 handler.

The `HudHidden` stop cause is transient and must continue to preserve the Cleanup-1 EC helper lifetime behavior. F8 hide must not introduce a new UAC prompt cycle.

When F8 force-shows `In-game only` with no game, global/base telemetry may run while game-specific FPS/API data remains unavailable. That is acceptable and useful for testing.

---

## 6. Persistence requirements

F8 state must never be written to settings.

Do not add anything like:

```text
ManualHudOverride=
F8Visible=
HudTemporarilyVisible=
HudTemporarilyHidden=
```

Do not call:

```cpp
SaveHudEnabledSetting(...)
SaveHudSettings()
```

because of an F8 press.

The persisted `hudEnabled` value must remain unchanged by F8.

A process restart must always lose the F8 override and return to the saved `Enable HUD` + visibility-mode policy.

---

## 7. Logging requirements

Because F8 is primarily a testing aid, its state transition should be easy to identify in real-device logs.

Recommended concise logs:

```text
F8 HUD override=show
F8 HUD override=hide
F8 HUD override ignored: HUD disabled
```

Do not add per-frame or timer logging.

Existing normal visibility logs such as:

```text
HUD shown
HUD hidden
```

should remain authoritative for the actual presentation result.

The F8 log records the requested override; the existing visibility log records the resulting show/hide transition.

---

## 8. HUD / VRR presentation contract — non-negotiable

This PR is a control-semantics cleanup only.

Do **not** modify, replace, weaken, or work around any production presentation invariant, including:

```text
HUD windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
existing WS_EX_LAYERED behavior
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
ProductionHudPresentationContract()
independent-flip requirement
existing Presentation API / DirectComposition production path
premultiplied-alpha contract
```

Do not modify `HudPresentation` to implement F8 semantics.

Do not change opacity behavior.

Do not recreate the presentation merely to show/hide it through F8.

The implementation belongs in the runtime visibility/control layer only.

All existing HUD/VRR contract tests must remain unchanged and pass.

---

## 9. Expected touched files

Likely runtime changes:

```text
src/ClawHUD/App.cpp
```

Possibly, only if using the optional pure helper:

```text
src/ClawHUD/HudModel.h
src/ClawHUD/HudModel.cpp
```

Tests likely include:

```text
tests/HudModelTests.cpp
```

If a more direct existing App/hotkey test seam exists at implementation time, prefer that over creating a large new fixture.

Do not touch `HudPresentation.*` for this work.

---

## 10. Required regression coverage

At minimum, preserve/verify the existing visibility truth table and add focused F8-policy coverage.

### 10.1 Master gate

```text
HUD disabled + F8
-> ignored
-> no force-show action

HUD disabled + hypothetical manualOverride=true
-> ResolveHudVisible remains false
```

The second invariant already has coverage in `HudModelTests`; keep it.

### 10.2 Toggle direction when enabled

```text
HUD enabled + currently visible
-> next F8 override = false

HUD enabled + currently hidden
-> next F8 override = true
```

### 10.3 Visibility policy interaction

Keep or add assertions proving:

```text
Enable On + Always + manualOverride=false
-> hidden

Enable On + In-game only + no game + manualOverride=true
-> shown

Enable Off + manualOverride=true
-> hidden
```

These are the intended F8 testing semantics.

### 10.4 No persistence regression

No new settings key or persistence path is allowed for F8.

If there is an existing settings round-trip test, ensure no change is required to its schema.

### 10.5 Existing lifecycle tests

Run the normal Release CTest suite and preserve all existing:

```text
HUD lifecycle tests
suspend/resume tests
EC helper lifetime tests
visibility tests
HUD/VRR presentation-contract tests
```

Do not weaken existing assertions to make this cleanup pass.

---

## 11. Manual real-device matrix

After the PR is built, validate on the device with normal runtime logging.

### Case A — master Off

```text
Enable HUD = Off
press F8 several times
```

Expected:

```text
no HUD appears
no presentation is initialized because of F8
no telemetry starts because of F8
no UAC prompt
persisted Enable HUD stays Off
log shows F8 ignored
```

Restart the app and verify HUD is still disabled.

### Case B — Always

```text
Enable HUD = On
Visibility = Always
HUD visible
press F8
press F8 again
```

Expected:

```text
visible -> force hidden -> force shown
```

Restart the app.

Expected:

```text
manual override gone
Always policy shows normally
```

### Case C — In-game only with game

```text
Enable HUD = On
Visibility = In-game only
launch/admit a game
HUD visible
press F8
press F8 again
```

Expected:

```text
visible -> force hidden -> force shown
```

### Case D — In-game only without game

```text
Enable HUD = On
Visibility = In-game only
no game active
HUD normally hidden
press F8
press F8 again
```

Expected:

```text
normally hidden -> force shown -> force hidden
```

This force-show is intentional test behavior.

### Case E — disable while override active

```text
Enable HUD = On
use F8 to establish either force-show or force-hide
set Enable HUD = Off
set Enable HUD = On again
```

Expected:

```text
Off clears the manual override
re-enable returns to configured Always / In-game only policy
old F8 state does not return
```

### Case F — visibility mode change

```text
Enable HUD = On
establish F8 override
change Always <-> In-game only
```

Expected:

```text
manual override clears
new configured visibility policy takes authority
```

---

## 12. Explicit non-goals

Do not include any of the following in this PR:

```text
new user-facing hotkey configuration
persisting F8 state
changing the F8 key
changing Enable HUD default
changing Always / In-game only definitions
new HUD visibility modes
new tray controls
Control IPC protocol changes
SteamAddon integration
PresentMon update/bootstrap changes
EC helper changes
opacity changes
HUD layout/style changes
HudPresentation changes
VRR/presentation contract changes
new generic state machine
```

---

## 13. Completion criteria

The work is complete when all of the following are true:

- `Enable HUD = Off` is an absolute master gate for F8.
- Pressing F8 while HUD is disabled does not call `Ensure()` or `MarkEnabled()`.
- Pressing F8 while HUD is disabled does not start game/graphics/telemetry activity.
- F8 never changes the persisted `hudEnabled` setting.
- With HUD enabled, F8 can force both show and hide.
- F8 force-show may intentionally override `In-game only` with no game for testing.
- F8 force-hide may intentionally override `Always` or an active game.
- Application restart clears the F8 override.
- Disabling/re-enabling HUD clears the F8 override.
- Changing visibility mode clears the F8 override.
- Existing visibility reconciliation remains responsible for sampling start/stop.
- F8 hide remains a transient `HudHidden` lifecycle path and does not cause EC-helper UAC churn.
- No HUD presentation/VRR invariant is changed.
- Release build and CTest pass.
- Existing HUD/VRR contract tests remain intact and pass.

---

## 14. Review focus for the implementation PR

Review the implementation primarily for these realistic regressions:

1. F8 still revives a master-disabled HUD.
2. F8 accidentally persists any runtime override.
3. F8 bypasses `ReconcileHudVisibility()` and directly manipulates telemetry lifecycle.
4. F8 no longer supports force-show in `In-game only` test scenarios.
5. HUD disable/re-enable fails to clear the override.
6. visibility-mode change fails to clear the override.
7. suspend/resume behavior regresses.
8. Cleanup-1 EC helper lifetime regresses and F8 hide causes renewed UAC prompts.
9. any HUD/VRR presentation-contract code changes appear in the PR.

Do not block the PR for speculative hotkey architecture improvements outside these concrete semantics.
