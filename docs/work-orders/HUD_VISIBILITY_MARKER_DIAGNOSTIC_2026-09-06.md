# HUD Visibility Marker Diagnostic Work Order — 2026-09-06

## 1. Purpose

This work order replaces the now-completed manual A/B recovery experiment from PR #237 with a passive visibility marker diagnostic.

Implementation baseline:

```text
main = 3a7750fbce3329236d1abaf9e7d510b075241202
PR #237 = Add manual HUD composition recovery diagnostics
```

The 2026-09-06 hardware test established the following:

```text
Test A: DirectComposition visual/content detach + reattach
  - executed successfully
  - did not visually restore the HUD

Test B: recreate only Presentation-backed resources
  - executed successfully
  - did not visually restore the HUD
  - repeated many times without restoring visibility
  - new Presentation epochs continued to Present successfully

Later:
  - HUD became visible again without A/B producing an immediate visual recovery
```

The existing evidence therefore no longer supports keeping the A/B mutation paths in the application.

The next question is not "which app-side object should we recreate?".

The next question is:

> What exact external window/composition transition occurs between the moment the HUD is visually covered and the moment it becomes visible again?

This PR must make that interval easy to mark in the log without changing HUD state, Presentation state, HWND state, or composition state.

---

## 2. Scope

Implement this as **one small diagnostic PR**.

The PR has two responsibilities only:

1. remove the PR #237 A/B recovery diagnostic paths;
2. add one passive global visibility-marker hotkey for real-device logging.

Do **not** add Presentation statistics, ETW, DWM/MPO instrumentation, or any production recovery behavior in this PR.

Those are follow-up work after marker-based field logs identify the exact covered -> restored boundary.

---

## 3. Non-negotiable production HUD / VRR contract

This PR is diagnostic cleanup plus passive logging only.

Do not modify, replace, weaken, bypass, or work around any of the following:

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
premultiplied-alpha presentation contract
```

No attempt to fix the visibility issue is allowed in this PR by using:

```text
SetWindowPos(HWND_TOPMOST, ...)
ShowWindow()
Hide()/Show()
window-style mutation
DComp visual rebind
Presentation resource recreation
retry loops
timers/watchdogs
foreground-specific recovery
Edge/Explorer special casing
```

The diagnostic marker must be observational only.

---

## 4. Why PR #237 A/B must be removed

PR #237 intentionally mutated two boundaries:

### Test A

```text
existing IDCompositionVisual
  detach existing compositionSurface_
  Commit + WaitForCommitCompletion
  reattach same compositionSurface_
  Commit + WaitForCommitCompletion
```

### Test B

```text
same HWND
same D3D/D2D/DComp host objects

replace:
  surfaceHandle_
  compositionSurface_
  IPresentationFactory
  IPresentationManager
  IPresentationSurface
  3 PresentationBuffer objects
  3 displayable D3D11 textures
  3 D2D bitmap targets
```

The hardware test showed that both operations could complete successfully while the HUD remained visually covered.

Therefore, retaining those mutation paths provides little further diagnostic value and increases code surface inside a VRR-critical presentation component.

Remove them cleanly instead of converting either into an automatic recovery path.

---

## 5. Remove the A/B hotkeys

Current PR #237 hotkeys:

```text
Ctrl+Alt+Shift+9 = Test A
Ctrl+Alt+Shift+0 = Test B
```

Remove both registrations and all corresponding registration-state members.

Remove:

```cpp
kHudCompositionRebindHotkeyId
kHudPresentationRecreateHotkeyId

hudCompositionRebindHotkeyRegistered_
hudPresentationRecreateHotkeyRegistered_

HandleHudCompositionRebindHotkey()
HandleHudPresentationRecreateHotkey()
```

Remove the corresponding `WM_HOTKEY` dispatch cases from `RuntimeMessageWindow::WindowProc`.

Existing F8 behavior must remain byte-for-byte/semantically unchanged except where unavoidable formatting context appears in the diff.

F8 remains:

```text
existing id
existing registration
existing HUD runtime-toggle behavior
existing non-persistent override semantics
```

---

## 6. Remove A/B controller/presentation mutation APIs

Remove from `HudController`:

```cpp
RunCompositionRebindDiagnostic()
RunPresentationResourceRecreateDiagnostic()
```

Remove from `HudPresentation`:

```cpp
RebindCompositionContentForDiagnostic()
RecreatePresentationResourcesForDiagnostic()
ReleasePresentationResources()
LogCompositionDiagnostic()
```

Remove A/B-only log output:

```text
[HudCompositionDiag]
```

No new automatic substitute should be added.

---

## 7. Remove A/B-only safe-state plumbing

PR #237 added a safe non-renderable state so a partially failed destructive Test B could not leave normal Render() dereferencing released resources.

If it is no longer used after A/B removal, remove:

```cpp
presentationResourcesReady_
```

and restore the normal pre-#237 production guards/assignments in:

```text
Initialize()
Render()
Show()
Shutdown()
```

Do this as a **behavior-restoring cleanup**, not as a new refactor.

The intended result is that normal production behavior returns to the PR #236-era semantics while keeping the PR #236 diagnostic state logging.

If any accessor/member introduced by #237 exists only to support A/B and is unused after the new marker implementation, remove it too.

Do not retain dead diagnostic API surface "just in case".

---

## 8. Preserve PR #236 diagnostics

Do not remove or weaken the existing observational logs from PR #236.

Keep:

```text
[HudPresentationState]
[HudWindowState]
```

Keep the existing Presentation diagnostics for:

```text
presentation epoch
successful Present count
last successful Present tick
buffer availability/no-buffer episodes
SetBuffer failure
Present failure
recovery from those failures
~5 second successful-render heartbeat
```

Keep the existing HWND observation for:

```text
logical visible state
IsWindow
IsWindowVisible
IsIconic
extended style
TOPMOST bit
window rect
foreground HWND/PID
zPrev/zNext HWND/PID
relevant WM_SHOWWINDOW / style / position observations
```

Do not change their normal logging cadence to accommodate the new marker.

---

## 9. Add one passive visibility-marker hotkey

Add exactly one new developer-only global hotkey:

```text
Ctrl + Alt + Shift + M
```

Suggested id after removing the two A/B ids:

```cpp
constexpr int kHudVisibilityMarkerHotkeyId = 2;
```

Register it against the existing `RuntimeMessageWindow` HWND using:

```cpp
MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT
```

and virtual key `'M'`.

### Gate

Register this marker only when:

```text
DebugLoggingEnabled == true
```

Use the existing runtime developer/debug setting.

Do not gate it on `_DEBUG` compilation.

Release builds with `DebugLoggingEnabled=true` must be able to use the marker, because the real-device reproduction is performed on release-style builds.

### Registration failure

Registration is best-effort.

If registration fails:

```text
log one warning naming Ctrl+Alt+Shift+M
continue application startup
leave F8 unaffected
```

Track registration state and unregister it in `StopRuntimeSources()` only if registration succeeded.

---

## 10. Marker semantics

One hotkey is used for both ends of a reproduction interval.

Each application session starts marker sequence at zero.

On every successful hotkey dispatch:

```text
seq 1 -> marker=covered   pair=1
seq 2 -> marker=restored  pair=1
seq 3 -> marker=covered   pair=2
seq 4 -> marker=restored  pair=2
...
```

This is an operator declaration, not automatic visibility detection.

The application is **not** claiming it can see whether pixels are actually visible.

The operator procedure defines:

```text
odd press  = "I can currently see that the HUD is covered/not visible"
even press = "I can currently see that the HUD has become visible again"
```

Suggested App state:

```cpp
std::uint64_t hudVisibilityMarkerSequence_{};
```

No persistence is required.

If an operator accidentally presses the marker out of order, that pair can simply be discarded during log analysis. Do not add reset UI/state machinery for this diagnostic.

---

## 11. Suggested integration boundary

Keep the existing ownership boundary:

```text
RuntimeMessageWindow
  -> App
  -> HudController
  -> HudPresentation
```

Suggested entry points:

```cpp
void App::HandleHudVisibilityMarkerHotkey();

void HudController::LogVisibilityMarkerDiagnostic(
    std::uint64_t sequence,
    bool coveredMarker);

void HudPresentation::LogVisibilityMarkerDiagnostic(
    std::uint64_t sequence,
    bool coveredMarker) const noexcept;
```

Names may vary slightly if a cleaner local naming convention exists.

Do not expose `HudPresentation*` to `App`.

Do not request a render after a marker.

Do not call `Show()`, `Hide()`, `CommitVisibility()`, `SetWindowPos()`, `Present()`, or any resource creation method from the marker path.

---

## 12. New log prefix

Use one new low-frequency prefix:

```text
[HudVisibilityMark]
```

A marker occurs only when the operator presses the hotkey, so a relatively detailed single record is acceptable.

Do not create a repeating timer or new heartbeat for this feature.

---

## 13. Required marker payload

Each marker should be self-contained enough to correlate the physical observation with the existing PR #236 and WindowLifecycle logs.

At minimum include:

```text
seq
pair
marker=covered|restored
markTickMs

presentation epoch
successfulPresentCount
lastSuccessfulPresentTickMs
noBufferActive
consecutiveNoBuffer
submissionFailureActive
failureCount

HUD hwnd
initialized/logical visible state
IsWindow
IsWindowVisible
IsIconic
HUD exStyle
HUD TOPMOST bit
HUD rect

foreground HWND
foreground PID
foreground visible/iconic state if cheaply available
foreground class if cheaply available
foreground rect if cheaply available
foreground exStyle if cheaply available

HUD zPrev HWND/PID
HUD zNext HWND/PID
```

The runtime logger already prepends wall-clock timestamp, so do not duplicate a custom wall-clock formatter merely for the marker.

`markTickMs=GetTickCount64()` is useful because the existing diagnostics also use tick timestamps.

### Optional but useful

If trivial using existing code/helper patterns, also include:

```text
msSinceLastSuccessfulPresent
foreground title
foreground style
```

Do not add expensive enumeration or COM/ETW queries to the hotkey path in this PR.

---

## 14. Example marker logs

Covered marker:

```text
[HudVisibilityMark] seq=1 pair=1 marker=covered markTickMs=1234567 epoch=1 successfulPresentCount=812 lastSuccessfulPresentTickMs=1234559 noBufferActive=0 consecutiveNoBuffer=0 submissionFailureActive=0 failureCount=0 hwnd=0x102a6 initialized=1 logicalVisible=1 isWindow=1 isWindowVisible=1 isIconic=0 exStyle=0x80800a8 exTopmost=1 rect=391,0,1528,40 foregroundHwnd=0x3060a foregroundPid=8700 foregroundClass="CabinetWClass" foregroundRect=0,0,1920,1128 foregroundExStyle=0x100 zPrevHwnd=... zPrevPid=... zNextHwnd=... zNextPid=...
```

Restored marker:

```text
[HudVisibilityMark] seq=2 pair=1 marker=restored markTickMs=1237890 epoch=1 successfulPresentCount=824 lastSuccessfulPresentTickMs=1237885 noBufferActive=0 consecutiveNoBuffer=0 submissionFailureActive=0 failureCount=0 hwnd=0x102a6 initialized=1 logicalVisible=1 isWindow=1 isWindowVisible=1 isIconic=0 exStyle=0x80800a8 exTopmost=1 rect=391,0,1528,40 foregroundHwnd=0x3060a foregroundPid=8700 foregroundClass="CabinetWClass" foregroundRect=0,0,1920,1128 foregroundExStyle=0x100 zPrevHwnd=... zPrevPid=... zNextHwnd=... zNextPid=...
```

The important comparison is whether the two markers show the same foreground top-level window and geometry or whether a HIDE/DESTROY/foreground/maximize/shell transition occurs between them.

---

## 15. No-presentation / hidden states

The marker is observational and should still produce a useful record if the HUD presentation does not exist.

If `HudController` has no presentation object, log for example:

```text
[HudVisibilityMark] seq=1 pair=1 marker=covered reason=no-presentation
```

If a presentation exists but is logically hidden, log the real state rather than returning without a record.

Do not make a hidden HUD visible.

The marker must never mutate persisted state, visibility override state, or sampling state.

---

## 16. Runtime threading

The global hotkey already arrives through `RuntimeMessageWindow`.

Keep marker handling on that existing main runtime thread.

Do not:

```text
spawn a worker
queue a retry
wait/sleep
block for compositor completion
start a timer
```

The marker should collect lightweight Win32/current diagnostic state and return immediately.

---

## 17. Explicitly out of scope for this PR

Do not add any of the following yet:

```text
Presentation statistics API integration
Present statistics polling
DWM ETW session
DXGI ETW session
MPO detection
independent-flip mode inference beyond existing contract state
hardware composition / plane diagnostics
Intel driver-specific APIs
automatic covered-state detection
automatic restored-state detection
automatic recovery
new Settings UI
Control IPC commands
Edge-specific logic
Explorer-specific logic
```

This PR exists to capture a precise time interval first.

A separate follow-up work order can use the marker evidence to decide which deeper instrumentation is actually justified.

---

## 18. Real-device test procedure

### Setup

```text
HUD Enabled = true
HUD Visibility = Always
DebugLoggingEnabled = true
```

Confirm:

```text
F8 still behaves exactly as before
Ctrl+Alt+Shift+M is registered
HUD initially visible
```

### Explorer primary reproduction

1. Open File Explorer.
2. Maximize/screen-fill it until the HUD becomes visually covered.
3. Keep Explorer foreground.
4. Immediately press `Ctrl+Alt+Shift+M` once.
5. Do not press A/B; those commands no longer exist.
6. Do not open Settings.
7. Do not manually toggle the HUD.
8. Wait for the HUD to become visible again naturally.
9. The moment it becomes visible, press `Ctrl+Alt+Shift+M` again.
10. Save the log.

Expected markers:

```text
seq=1 marker=covered
seq=2 marker=restored
```

### Edge secondary reproduction

Repeat exactly the same procedure using normal maximized/screen-filling Microsoft Edge.

Prefer ordinary maximize first, not F11 fullscreen.

### Repeatability

Collect at least two valid marker pairs if the issue reproduces reliably:

```text
pair 1 = Explorer
pair 2 = Edge
```

or multiple pairs for the same target if natural recovery behavior varies.

---

## 19. What the next analysis must determine

For each `covered -> restored` pair, inspect the complete interval and answer:

### Question A — same foreground surface?

Did the same Explorer/Edge top-level HWND remain:

```text
visible
foreground
same size/rect
same style/exStyle
```

from covered marker through restored marker?

If yes, natural recovery occurred while the apparent covering window remained in place. That strongly increases the value of compositor/display-plane instrumentation.

### Question B — window lifecycle transition?

Did any of these occur between markers?

```text
foreground change
Explorer/Edge HIDE
Explorer/Edge DESTROY
new top-level replacement HWND
maximize/restore resize
shell/XAML topmost window SHOW/HIDE
DPI/display change
```

If yes, that transition becomes the leading external trigger candidate.

### Question C — Presentation continued?

Between markers, did:

```text
successfulPresentCount continue increasing?
noBufferActive remain 0?
submissionFailureActive remain 0?
```

If yes, it further confirms that app-side submission stayed healthy while physical visibility changed.

---

## 20. Tests / validation

### Build/test baseline

Run the normal project validation:

```text
Debug build
Release build
native CTest baseline
WPF Settings tests if part of normal CI
```

Use only the repository's existing documented exclusion for any interactive/hanging diagnostic test; do not introduce new skips for this PR.

### A/B removal checks

Search `src/` and verify no production source reference remains to:

```text
RebindCompositionContentForDiagnostic
RecreatePresentationResourcesForDiagnostic
RunCompositionRebindDiagnostic
RunPresentationResourceRecreateDiagnostic
kHudCompositionRebindHotkeyId
kHudPresentationRecreateHotkeyId
HudCompositionDiag
```

Historical work-order documentation may still mention them; that is expected history and does not need to be rewritten.

### Marker checks

Verify:

```text
DebugLoggingEnabled=false
  F8 registered as before
  Ctrl+Alt+Shift+M not registered

DebugLoggingEnabled=true
  F8 registered as before
  Ctrl+Alt+Shift+M registered

marker press
  emits exactly one [HudVisibilityMark] record
  does not request Render
  does not Show/Hide
  does not SetWindowPos
  does not Commit DComp changes
  does not Present

shutdown
  unregisters marker hotkey if registration succeeded
```

### VRR contract checks

Run:

```text
git diff origin/main...HEAD -- src/ClawHUD/HudPresentationContract.h
```

Expected:

```text
no diff
```

Also verify no regression to existing tests/assertions for:

```text
click-through
no activation
topmost
transparent hit testing
independent flip
premultiplied alpha
ProductionHudPresentationContract()
```

---

## 21. PR description requirements

The implementation PR description must state clearly:

```text
- PR #237 A/B mutation diagnostics were removed because real-device testing showed no visual recovery from either path.
- No automatic recovery behavior was added.
- Existing PR #236 [HudPresentationState] / [HudWindowState] diagnostics remain.
- Ctrl+Alt+Shift+M is a debug-logging-only passive operator marker.
- Odd marker sequence means operator-observed covered state; even sequence means operator-observed restored state.
- The marker performs no presentation/window/composition mutation.
- ProductionHudPresentationContract.h has no diff.
```

After hardware testing, append a short result matrix such as:

```text
Explorer pair 1:
  covered marker: 17:xx:xx.xxx
  restored marker: 17:xx:xx.xxx
  same foreground HWND across interval: yes/no
  relevant lifecycle transition: ...

Edge pair 2:
  covered marker: 17:xx:xx.xxx
  restored marker: 17:xx:xx.xxx
  same foreground HWND across interval: yes/no
  relevant lifecycle transition: ...
```

---

## 22. Completion checklist

- [ ] Remove Ctrl+Alt+Shift+9 Test A registration/dispatch/state.
- [ ] Remove Ctrl+Alt+Shift+0 Test B registration/dispatch/state.
- [ ] Remove A/B App handlers.
- [ ] Remove A/B HudController APIs.
- [ ] Remove A/B HudPresentation mutation methods.
- [ ] Remove `[HudCompositionDiag]` runtime source logging.
- [ ] Remove `presentationResourcesReady_` and other A/B-only safe-state plumbing if now unused.
- [ ] Restore normal pre-#237 production Render/Show/Initialize/Shutdown semantics where #237 changed them solely for Test B safety.
- [ ] Preserve PR #236 `[HudPresentationState]` diagnostics unchanged.
- [ ] Preserve PR #236 `[HudWindowState]` diagnostics unchanged.
- [ ] Add `Ctrl+Alt+Shift+M` only when `DebugLoggingEnabled=true`.
- [ ] Keep F8 unchanged.
- [ ] Add one `[HudVisibilityMark]` record per marker press.
- [ ] Alternate operator marker labels `covered/restored` by sequence.
- [ ] Include presentation + HWND + foreground + z-order snapshot in the marker.
- [ ] Marker path performs no render/presentation/window/composition mutation.
- [ ] No timer/watchdog/polling/retry/recovery added.
- [ ] No Edge/Explorer special casing added.
- [ ] No Presentation statistics/ETW/MPO work added in this PR.
- [ ] Debug build passes.
- [ ] Release build passes.
- [ ] Normal native CTest baseline passes.
- [ ] Existing VRR/HUD contract tests remain intact.
- [ ] `HudPresentationContract.h` has zero diff.
- [ ] Real-device Explorer marker pair collected.
- [ ] Real-device Edge marker pair collected if reproducible.

---

## 23. Follow-up after this PR

Do not pre-implement the next layer in this PR.

After marker logs are collected, use the observed `covered -> restored` interval to decide whether the next diagnostic PR should add:

```text
Presentation statistics
DWM/DXGI ETW
MPO/hardware-composition evidence
independent-flip transition evidence
```

The choice must be evidence-driven from the marker interval rather than adding all possible instrumentation at once.
