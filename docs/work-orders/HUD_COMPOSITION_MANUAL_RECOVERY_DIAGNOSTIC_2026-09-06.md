# HUD Composition Manual Recovery Diagnostic

**Status:** implementation work order  
**Date:** 2026-09-06  
**Target repository:** `onehoon/ClawHUD`  
**Code baseline:** `main` @ `398b443e4110e36a6894e24cd354c80f2d02a776` (`Add HUD Presentation API state diagnostics (#236)`)  
**Delivery:** **one diagnostic PR**

---

## 1. Goal

Add two **manual, developer-only, global hotkey diagnostics** that can be triggered while the HUD is visually covered by a maximized Microsoft Edge or File Explorer window.

The diagnostics must determine which internal presentation layer needs to be refreshed before the HUD becomes visible again:

- **Test A:** rebind only the existing DirectComposition visual content;
- **Test B:** if Test A does not restore visibility, recreate only the Presentation-backed resources while preserving the existing HUD HWND and DirectComposition host objects.

This PR is **not a production recovery PR**.

It must not automatically react to Edge, Explorer, foreground changes, maximize events, failed frames, elapsed time, or any other runtime condition.

The user must explicitly trigger each diagnostic hotkey during the real-device reproduction.

---

## 2. Why this diagnostic is now justified

The 2026-09-06 field log from `C:\GoogleDrive\ClawHUD\logs\0906` resolves the diagnostic branches introduced by PR #236.

The reported visual behavior is not that the HUD appears to stop rendering. It appears to remain alive but become covered by another screen-filling window.

### 2.1 Win32 HUD state remains healthy

During the reproduction, the HUD reports:

```text
logicalVisible=1
isWindow=1
isWindowVisible=1
isIconic=0
exTopmost=1
```

This is observed while Edge or Explorer is foreground.

There is no corresponding production evidence of:

```text
Hide()
ShowWindow(SW_HIDE)
WS_EX_TOPMOST removal
relevant HUD WM_WINDOWPOSCHANGED Z-order demotion
extended-style mutation
```

Do not treat this as an `Always` visibility-policy or normal HWND topmost problem.

### 2.2 Presentation submission remains healthy

PR #236 diagnostics show successful Presentation activity continuing while the HUD is visually missing.

In the second 0.1.98 session, for example:

```text
01:27:54  successfulPresentCount=252
01:27:59  successfulPresentCount=268
01:28:04  successfulPresentCount=284
01:28:09  successfulPresentCount=300
01:28:14  successfulPresentCount=318
```

with:

```text
noBufferActive=0
submissionFailureActive=0
failureCount=0
```

The same session shows a File Explorer window becoming screen-filling while the HUD Present counter continues advancing.

Therefore the current field evidence is the PR #236 work order's **Case C**:

```text
HWND visible + topmost
Presentation buffers available
SetBuffer succeeds
Present returns S_OK repeatedly
successful Present count continues advancing
HUD is nevertheless not visible to the user
```

### 2.3 The remaining boundary

Investigation should now distinguish between:

```text
A. DirectComposition visual/content binding state
```

and:

```text
B. PresentationManager / PresentationSurface / displayable-buffer resource state
```

without changing the HWND or the production presentation contract.

---

# 3. Non-negotiable HUD / VRR contract

This diagnostic PR MUST NOT modify, replace, weaken, bypass, or experimentally alter any production HUD presentation invariant.

The following are frozen:

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
Presentation API / DirectComposition production presentation path
premultiplied-alpha presentation contract
```

`src/ClawHUD/HudPresentationContract.h` should have **zero diff**.

Also preserve:

```text
texture format
sample count
buffer count = 3
displayable/shared resource flags
DXGI_ALPHA_MODE_PREMULTIPLIED
identity transform
letterboxing margins
```

The diagnostic must not use a contract change as a way to make the HUD visible.

---

# 4. Explicitly forbidden approaches

Do not add any of the following:

```cpp
SetWindowPos(window_, HWND_TOPMOST, ...);   // diagnostic recovery
ShowWindow(window_, SW_HIDE);
ShowWindow(window_, SW_SHOWNOACTIVATE);
SetWindowLongPtrW(...);                     // style mutation
```

Do not call the existing full-HUD lifecycle as the diagnostic action:

```cpp
HudController::Recreate(...)
HudPresentation::Shutdown()
HudPresentation::Initialize(...)
```

Those paths may destroy/recreate the HWND or change too many layers at once and would invalidate the experiment.

Also forbidden:

```text
periodic topmost refresh
foreground-triggered recovery
Edge-specific code
Explorer-specific code
maximize detection
watchdog
polling
Sleep-based sequencing
retry loops
automatic Presentation recreation on S_FALSE
automatic recreation on SetBuffer/Present failure
```

No Settings UI button.

Opening Settings would itself change foreground/composition state and make the field experiment less reliable.

No Control IPC protocol operation is required for this PR.

---

# 5. Delivery shape

Implement this as **one diagnostic PR**.

A single implementation commit is acceptable because Test A and Test B form one diagnostic instrument and share the same debug-hotkey wiring.

Keep the diff narrow. Expected production files are approximately:

```text
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/RuntimeMessageWindow.cpp
src/ClawHUD/HudController.h
src/ClawHUD/HudController.cpp
src/ClawHUD/HudPresentation.h
src/ClawHUD/HudPresentation.cpp
```

Tests/CMake may change only when needed for real regression coverage.

Do not introduce a new service, background thread, timer, watcher, settings surface, or IPC protocol.

---

# 6. Developer-only global hotkeys

## 6.1 Registration gate

Register both new diagnostic hotkeys only when the existing runtime setting is:

```text
[Developer]
DebugLoggingEnabled=true
```

Use the existing `debugLoggingEnabled_` value already loaded by `App` at startup.

This gate is based on the runtime developer setting, **not** `_DEBUG` compiler configuration. The real-device diagnostic release build must still be usable when debug logging is enabled.

Normal users with debug logging disabled must not have these hotkeys registered.

## 6.2 Recommended hotkeys

Use:

```text
Ctrl + Alt + Shift + 9  -> Test A: DirectComposition visual rebind
Ctrl + Alt + Shift + 0  -> Test B: Presentation resource recreation
```

Recommended IDs:

```cpp
constexpr int kHudToggleHotkeyId = 1;                 // existing F8, unchanged
constexpr int kHudCompositionRebindHotkeyId = 2;      // new
constexpr int kHudPresentationRecreateHotkeyId = 3;   // new
```

Register against the existing `RuntimeMessageWindow` HWND using:

```cpp
MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT
```

and the normal top-row virtual-key codes for `9` and `0`.

## 6.3 Existing F8 contract

Do not change:

```text
F8 registration
F8 id
F8 non-persistent HUD override semantics
F8 handler behavior
```

The new hotkeys are independent diagnostics.

## 6.4 Registration failure

Each new hotkey registration is best-effort.

If one fails:

- log a warning naming that diagnostic hotkey;
- continue normal application startup;
- do not fail HUD initialization;
- do not disable F8.

Track registration state so only successfully registered hotkeys are unregistered during `StopRuntimeSources()`.

## 6.5 Runtime dispatch

Extend the existing `RuntimeMessageWindow::WindowProc` `WM_HOTKEY` handling with two explicit cases that forward to `App` handlers.

Suggested App entry points:

```cpp
void HandleHudCompositionRebindHotkey();
void HandleHudPresentationRecreateHotkey();
```

The handlers should re-check the developer/debug gate and must execute on the existing main runtime thread.

Do not spawn work on another thread.

---

# 7. Test A — rebind only the existing DirectComposition content

## 7.1 Purpose

Test whether the HUD becomes visible again when only this relationship is refreshed:

```text
existing IDCompositionVisual
        ↕
existing compositionSurface_
```

Everything else must remain the same:

```text
same HWND
same window styles
same Z-order
same D3D device
same D2D device/context
same IDCompositionDevice
same IDCompositionTarget
same IDCompositionVisual
same PresentationFactory
same PresentationManager
same PresentationSurface
same surface handle
same presentation buffers
same textures
same presentation epoch
```

If Test A restores the HUD, the evidence points toward the DComp visual/content binding or final DWM composition state rather than the Presentation resource generation.

## 7.2 Suggested API boundary

Expose a narrow developer-diagnostic method through `HudController`, for example:

```cpp
HRESULT RunCompositionRebindDiagnostic();
```

which delegates to a `HudPresentation` method such as:

```cpp
HRESULT RebindCompositionContentForDiagnostic();
```

Do not expose the concrete `HudPresentation` object to `App`.

`HudController` remains the owner/boundary for presentation operations.

## 7.3 Preconditions

Test A is meaningful only when:

```text
HudPresentation is initialized
HUD logical visible_ == true
visual_ exists
compositionDevice_ exists
compositionSurface_ exists
```

If the diagnostic is triggered while the HUD is disabled/hidden/uninitialized, do not show it and do not mutate visibility.

Return/log a clear `skipped` / not-applicable result.

Do not call `Show()`.

## 7.4 Required sequence

The diagnostic should conceptually perform:

```cpp
visual_->SetContent(nullptr);
compositionDevice_->Commit();
compositionDevice_->WaitForCommitCompletion();

visual_->SetContent(compositionSurface_.Get());
compositionDevice_->Commit();
compositionDevice_->WaitForCommitCompletion();
```

Use `WaitForCommitCompletion()` for the manual diagnostic so the detach commit is actually processed before the attach commit. Do not replace this with `Sleep()`.

This wait is allowed only in the explicit developer diagnostic path. Do not add it to normal rendering or visibility operations.

If the first detach/commit fails, log the exact stage/HRESULT and stop the diagnostic.

If the reattach/commit fails, log the exact stage/HRESULT and stop. Do not invoke full recreation as an automatic fallback.

## 7.5 Render after a successful Test A

After a successful rebind, request exactly one normal current HUD render through the existing `HudController` render callback path.

Do not fabricate telemetry and do not start a separate render loop.

The request should happen only when the HUD is enabled and logically visible.

The normal production timer/render path continues afterward unchanged.

## 7.6 Epoch semantics

Test A does **not** create a new Presentation resource generation.

Therefore:

```text
presentationEpoch_ must not change
successfulPresentCount must not be reset
HudPresentationDiagnosticState must not be reset
```

This is useful evidence: if the HUD becomes visible again without an epoch change, a full Presentation resource recreation was not required.

---

# 8. Test B — recreate only Presentation-backed resources

## 8.1 When Test B is used

Test B is manually triggered only when:

```text
HUD is visually covered
Test A was triggered
Test A completed successfully
HUD is still not visible
```

Do not automatically invoke Test B after Test A.

The operator must make the A/B decision visually.

## 8.2 Purpose

Test whether the HUD becomes visible after replacing the Presentation resource generation while keeping the HWND and DComp host objects alive.

### Must remain unchanged

```text
HWND
window class / styles / extended styles
HWND Z-order
D3D11 device
D3D11 device context
D2D device context
DWrite factory
HudRenderer
IDCompositionDevice
IDCompositionTarget
IDCompositionVisual
monitor/window geometry
HUD options
visibility state
```

### Recreate only

```text
surfaceHandle_
compositionSurface_
IPresentationFactory
IPresentationManager
IPresentationSurface
three IPresentationBuffer objects
three D3D11 displayable textures
three D2D bitmap targets
```

Use the exact same `ProductionHudPresentationContract()` constants as normal initialization.

## 8.3 Suggested API boundary

Through `HudController`, expose a narrow diagnostic method such as:

```cpp
HRESULT RunPresentationResourceRecreateDiagnostic();
```

which delegates to:

```cpp
HRESULT RecreatePresentationResourcesForDiagnostic();
```

inside `HudPresentation`.

Again, do not expose `HudPresentation*` to `App`.

## 8.4 Preconditions

As with Test A, Test B must not make a hidden HUD visible.

Require initialized + logically visible state and the required graphics/DComp objects.

If preconditions are not met, log `skipped` and do nothing.

## 8.5 Required detach boundary

Before releasing the old Presentation-backed resources:

```cpp
visual_->SetContent(nullptr);
compositionDevice_->Commit();
compositionDevice_->WaitForCommitCompletion();
```

Do not call `Hide()` or `ShowWindow()`.

The HUD HWND remains a visible topmost HWND throughout the experiment.

## 8.6 Clear the D2D target before releasing old bitmap resources

The D2D device context may retain the last bitmap target set by `Render()`.

Before releasing the old `bitmapTarget` objects/textures, explicitly clear the target:

```cpp
d2dContext_->SetTarget(nullptr);
```

This prevents the D2D context from retaining the previous generation's render target through the diagnostic resource teardown.

Do not recreate the D2D context.

## 8.7 Release order

After the visual detach commit has completed and D2D target is cleared, release the old Presentation-backed generation in a clear order, conceptually:

```text
buffer.bitmapTarget
buffer.presentationBuffer
buffer.texture
presentationSurface_
presentationManager_
presentationFactory_
compositionSurface_
surfaceHandle_
```

Close the old `surfaceHandle_` and set it back to `INVALID_HANDLE_VALUE`.

Do not release:

```text
visual_
compositionTarget_
compositionDevice_
device_
deviceContext_
d2dContext_
writeFactory_
renderer_
window_
```

## 8.8 Recreate using the existing production creation path

Reuse the existing production Presentation creation helpers wherever possible:

```cpp
CreatePresentationSurface();
CreateBitmapTargets();
```

Do not duplicate the Presentation contract with a second set of hard-coded flags/constants.

If a small private helper extraction is necessary to reuse the exact resource-creation contract safely, keep it narrowly scoped and behavior-preserving.

Do not perform a broad renderer/presentation refactor in this diagnostic PR.

## 8.9 Attach the new generation

After the new Presentation-backed resources exist:

```cpp
visual_->SetContent(compositionSurface_.Get());
compositionDevice_->Commit();
compositionDevice_->WaitForCommitCompletion();
```

Do not call `SetWindowPos`, `ShowWindow`, or any full HWND lifecycle operation.

## 8.10 Epoch and diagnostic-state semantics

On successful replacement of the Presentation-backed generation:

```text
increment presentationEpoch_
reset HudPresentationDiagnosticState
```

Record the previous epoch and previous successful Present count before reset.

The next normal rendered frame should therefore begin the new epoch's Present count again from the new diagnostic state.

In `_DEBUG` builds, reset the existing per-generation alpha-validation latch if required so the new buffers can be validated normally.

## 8.11 Render after successful Test B

A newly created PresentationSurface has no current HUD frame until normal rendering submits a buffer.

After successful recreation and reattachment, request one normal current HUD render through the existing `HudController` render callback.

Expected result:

```text
new epoch created
normal Render()
SetBuffer()
Present() == S_OK
present-heartbeat eventually reports new epoch
```

Do not directly synthesize a Present inside the diagnostic method if the normal render path can do it.

## 8.12 Failure behavior

This diagnostic must never crash because a partially recreated member is subsequently dereferenced by normal Render().

If Test B fails after the old generation has already been detached/released:

- log the exact failing stage and HRESULT;
- leave the presentation in an explicitly non-renderable/safe state so subsequent normal Render/Show paths return a normal failure instead of dereferencing null resources;
- require application restart for further field testing;
- **do not** automatically call the existing full `Recreate()` / `Shutdown()` / `Initialize()` fallback, because that would destroy the HWND and invalidate the experiment.

Prefer a small, explicit safe-state mechanism over retry machinery.

Do not add a general production state machine solely for this diagnostic.

---

# 9. Diagnostic logging

Use a separate low-noise prefix:

```text
[HudCompositionDiag]
```

The existing `[HudPresentationState]` and `[HudWindowState]` logs must remain unchanged.

## 9.1 Test A logs

Suggested records:

```text
[HudCompositionDiag] action=visual-rebind-begin epoch=1 hwnd=0x... visible=1 successfulPresentCount=...

[HudCompositionDiag] action=visual-rebind-stage stage=detach-commit hr=0x00000000
[HudCompositionDiag] action=visual-rebind-stage stage=detach-wait hr=0x00000000
[HudCompositionDiag] action=visual-rebind-stage stage=attach-commit hr=0x00000000
[HudCompositionDiag] action=visual-rebind-stage stage=attach-wait hr=0x00000000

[HudCompositionDiag] action=visual-rebind-complete epoch=1 hwnd=0x... hr=0x00000000
```

On failure:

```text
[HudCompositionDiag] action=visual-rebind-failed stage=... epoch=1 hwnd=0x... hr=0x...
```

On invalid state:

```text
[HudCompositionDiag] action=visual-rebind-skipped reason=not-visible
```

Do not flood logs during normal rendering.

## 9.2 Test B logs

Suggested records:

```text
[HudCompositionDiag] action=presentation-recreate-begin oldEpoch=1 hwnd=0x... visible=1 oldSuccessfulPresentCount=...

[HudCompositionDiag] action=presentation-recreate-stage stage=detach-commit hr=0x00000000
[HudCompositionDiag] action=presentation-recreate-stage stage=detach-wait hr=0x00000000
[HudCompositionDiag] action=presentation-recreate-stage stage=release-old hr=0x00000000
[HudCompositionDiag] action=presentation-recreate-stage stage=create-surface hr=0x00000000
[HudCompositionDiag] action=presentation-recreate-stage stage=create-bitmap-targets hr=0x00000000
[HudCompositionDiag] action=presentation-recreate-stage stage=attach-commit hr=0x00000000
[HudCompositionDiag] action=presentation-recreate-stage stage=attach-wait hr=0x00000000

[HudCompositionDiag] action=presentation-recreate-complete oldEpoch=1 newEpoch=2 hwnd=0x... hr=0x00000000
```

On failure:

```text
[HudCompositionDiag] action=presentation-recreate-failed stage=... oldEpoch=1 hwnd=0x... hr=0x...
```

After a successful B action, the ordinary PR #236 diagnostics should provide evidence such as:

```text
[HudPresentationState] reason=present-heartbeat epoch=2 ...
```

Do not duplicate the whole heartbeat state into every `[HudCompositionDiag]` record.

---

# 10. HudController integration

`HudController` owns the concrete `HudPresentation` and should remain the boundary used by `App`.

Recommended behavior for each diagnostic wrapper:

```text
1. verify presentation exists;
2. call the corresponding HudPresentation diagnostic method;
3. if method succeeds and HUD is enabled + visible, request one normal render;
4. return/log HRESULT without changing persisted HUD state or manual override.
```

Do not mutate:

```text
enabled_
manualOverride_
visibilityMode
sampling state
settings persistence
```

The diagnostic is not a visibility command.

---

# 11. App / runtime-message integration

## 11.1 Registration

In `App::Run()`, after the runtime message window exists and alongside the existing F8 registration, register the two developer diagnostic hotkeys only when `debugLoggingEnabled_` is true.

Do not move or rewrite the existing F8 registration just to share code unless a tiny helper clearly reduces duplication without changing behavior.

## 11.2 Shutdown

In `StopRuntimeSources()`, unregister only diagnostic hotkeys that were actually registered.

No leaked global hotkey registrations after exit.

## 11.3 Dispatch

`RuntimeMessageWindow::WindowProc` should dispatch the new hotkey ids to the two explicit App handlers.

Do not overload `HandleHudToggleHotkey()`.

The two diagnostics are not HUD visibility overrides.

---

# 12. Tests and regression requirements

## 12.1 Existing regression suite

Debug and Release builds must remain clean.

Run the project's normal native CTest baseline (with only the repository's already-documented interactive test exclusions, if any).

No existing HUD/VRR contract regression may be removed, weakened, skipped, or rewritten to accommodate the diagnostic.

Preserve tests/assertions for:

```text
click-through behavior
no activation
topmost behavior
transparent hit testing
independent flip
premultiplied alpha
ProductionHudPresentationContract()
```

## 12.2 Contract diff check

Before PR completion:

```text
git diff main...HEAD -- src/ClawHUD/HudPresentationContract.h
```

Expected: **no diff**.

Also inspect the final diff to confirm no changes to the required `WM_NCHITTEST` / `WM_MOUSEACTIVATE` returns or the HUD window extended-style contract.

## 12.3 Hotkey behavior checks

At minimum verify manually or with suitable narrow tests:

```text
DebugLoggingEnabled=false:
  F8 registration unchanged
  diagnostic 9/0 hotkeys not registered

DebugLoggingEnabled=true:
  F8 registration unchanged
  Test A hotkey registered
  Test B hotkey registered

shutdown:
  every successfully registered hotkey is unregistered
```

Do not build an elaborate fake COM framework just to unit-test `IDCompositionDevice` calls.

The value of this PR is the real-device field experiment.

---

# 13. Real-device acceptance procedure

The implementation is not complete until the following procedure is documented in the PR description and run on the affected MSI Claw device.

## 13.1 Setup

```text
ClawHUD 0.1.98-or-newer diagnostic build
HUD Enabled = true
HUD Visibility = Always
DebugLoggingEnabled = true
```

Confirm the HUD is initially visible.

## 13.2 Primary reproduction: File Explorer

Use File Explorer first because the 0906 log shows the same symptom outside Edge.

1. Open File Explorer.
2. Maximize it / make it screen-filling.
3. Confirm visually that the HUD is covered/not visible.
4. Do **not** Alt+Tab to Settings or another app.
5. Keep Explorer foreground.
6. Press `Ctrl+Alt+Shift+9` once.
7. Observe the HUD immediately.

### If HUD becomes visible after Test A

Record:

```text
A = RESTORED
B = NOT RUN
```

Do not run Test B in that reproduction.

Save the log.

### If HUD remains covered after Test A

Keep Explorer foreground and continue:

8. Press `Ctrl+Alt+Shift+0` once.
9. Observe the HUD immediately.

Record either:

```text
A = NO CHANGE
B = RESTORED
```

or:

```text
A = NO CHANGE
B = NO CHANGE
```

Save the log.

## 13.3 Secondary reproduction: Microsoft Edge

Repeat the same procedure using normal maximized/screen-filling Edge.

F11 fullscreen is not the primary scenario unless the ordinary maximize case cannot be reproduced.

Again, do not switch to Settings between the visual failure and the diagnostic hotkey.

## 13.4 Repeatability

If A or B restores the HUD, test at least one additional foreground/maximize cycle:

```text
restore HUD
-> leave/minimize target window
-> return to target window
-> maximize/screen-fill again
-> check whether the visual covering condition returns
```

This determines whether the action repairs a one-time stale state or whether the problematic state is recreated by a normal foreground/maximize composition transition.

---

# 14. Interpretation matrix

The field result determines the next engineering branch.

## Result 1 — Test A restores HUD

```text
A = RESTORED
```

Interpretation:

- existing PresentationManager remains usable;
- existing PresentationSurface remains usable;
- existing buffers remain usable;
- successful Present sequence remains in the same epoch;
- refreshing the DComp visual/content binding is sufficient.

Next investigation should focus on:

```text
IDCompositionVisual content binding
DComp commit state
DWM composition ordering/state around the visible topmost HWND
```

Do not implement Presentation resource recreation as the production fix if A alone is sufficient.

## Result 2 — A fails, B restores HUD

```text
A = NO CHANGE
B = RESTORED
```

Interpretation:

- merely rebinding the existing DComp surface is insufficient;
- a new Presentation resource generation restores visibility;
- investigation should focus on manager/surface/displayable-buffer/scanout allocation state.

The eventual production fix must still preserve the production contract and should be based on the smallest proven recovery boundary.

## Result 3 — neither A nor B restores HUD

```text
A = NO CHANGE
B = NO CHANGE
```

Interpretation:

The problem is likely below the in-process DComp/Presentation resource binding boundary, for example in final DWM/display-plane/driver/independent-flip interaction.

At that point:

- do not add repeated recreation loops;
- do not begin periodic HWND topmost enforcement;
- do not weaken independent flip;
- do not change the production presentation contract.

Stop and perform the next design/research pass with the captured logs.

## Result 4 — diagnostic action itself fails

If A or B returns an HRESULT failure, preserve the exact stage/HRESULT in the log and treat that as separate evidence.

Do not silently fall through to a stronger recovery action.

---

# 15. PR description requirements

The PR description must explicitly state:

1. This is a **manual diagnostic PR**, not an automatic recovery.
2. PR #236 field evidence classified the current failure as successful ongoing Present activity while visual output is missing/covered.
3. Test A changes only the DComp visual/content binding.
4. Test B changes only the Presentation-backed resource generation and keeps the same HUD HWND / DComp host.
5. The two hotkeys exist only when `DebugLoggingEnabled=true`.
6. Existing F8 semantics are unchanged.
7. `ProductionHudPresentationContract()` and all VRR-critical window/presentation invariants are unchanged.
8. No Edge/Explorer-specific production behavior was added.
9. No automatic recovery, polling, retry loop, timer, or watchdog was added.
10. Debug/Release build and CTest results.

After field testing, append the observed matrix result:

```text
Explorer: A=..., B=...
Edge:     A=..., B=...
```

with the relevant `[HudCompositionDiag]`, `[HudPresentationState]`, and `[HudWindowState]` time range.

---

# 16. Completion checklist

- [ ] One diagnostic PR only.
- [ ] Baseline is current `main` after PR #236.
- [ ] New global diagnostics are registered only when `DebugLoggingEnabled=true`.
- [ ] Existing F8 behavior is unchanged.
- [ ] Test A is manual only.
- [ ] Test B is manual only.
- [ ] No Settings UI controls added.
- [ ] No Control IPC protocol changes.
- [ ] Test A does not recreate Presentation resources or change epoch.
- [ ] Test B keeps the same HUD HWND.
- [ ] Test B keeps the same DComp device/target/visual.
- [ ] Test B recreates only Presentation-backed resources and bitmap targets.
- [ ] D2D target is cleared before old bitmap-target release.
- [ ] DComp detach/attach commits use commit completion, not `Sleep()`.
- [ ] No `SetWindowPos` recovery added.
- [ ] No `ShowWindow` hide/show recovery added.
- [ ] No style changes added.
- [ ] No automatic foreground/maximize/browser/explorer recovery added.
- [ ] No watchdog/poll/retry loop added.
- [ ] `HudPresentationContract.h` has zero diff.
- [ ] Existing click-through/no-activation/topmost/hit-test/independent-flip/premultiplied-alpha tests remain.
- [ ] Existing `[HudPresentationState]` diagnostics remain intact.
- [ ] Existing `[HudWindowState]` diagnostics remain intact.
- [ ] New `[HudCompositionDiag]` logs identify every manual action and failure stage.
- [ ] Test B cannot leave normal Render able to dereference partially released null Presentation resources after a failed recreation.
- [ ] Debug build clean.
- [ ] Release build clean.
- [ ] Native CTest baseline clean.
- [ ] Explorer real-device A/B test performed.
- [ ] Edge real-device A/B test performed.
- [ ] PR description records the final A/B result matrix.

---

# 17. Final implementation rule

The only question this PR is allowed to answer is:

> **When the HUD remains Win32-visible/topmost and continues successful Presentation API Present calls but is visually covered, does rebinding the existing DComp content restore it, or is a new Presentation resource generation required?**

Do not turn this diagnostic PR into the final fix.

Preserve the production HUD/VRR contract unchanged, collect the A/B evidence on hardware, and choose the production recovery design only after that evidence exists.
