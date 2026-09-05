# HUD Presentation Diagnostics + First-Visible Warm-up Removal

**Status:** implementation work order  
**Date:** 2026-09-05  
**Target repository:** `onehoon/ClawHUD`  
**Code baseline:** `main` @ `7e861eeb609477e666511a0a8fdf863b59f1d80d`  
**Delivery:** **one PR, exactly two implementation commits**

---

## 1. Goal

Prepare a clean real-device diagnostic build for the remaining HUD disappearance seen when Microsoft Edge becomes maximized while HUD visibility mode is `Always`.

This PR has exactly two purposes:

1. **Remove only the PR #232 Part C first-visible one-shot HUD presentation warm-up workaround.**
2. **Add low-noise Presentation API diagnostics that can distinguish buffer starvation, submission failure, and successful ongoing Present activity while the HUD is visually missing.**

Do **not** implement a new recovery mechanism in this PR.

The field failure must remain observable if it still exists.

---

# 2. Mandatory commit structure

This PR MUST contain the implementation as **two separate commits in this order**.

Do not squash the two implementation steps together while developing the PR.

Recommended commit subjects:

```text
1. Remove first-visible HUD presentation warm-up
2. Add HUD Presentation API state diagnostics
```

The purpose of this split is reviewability:

- commit 1 establishes the clean production baseline by deleting the failed empirical workaround;
- commit 2 adds diagnostic-only behavior on top of that baseline;
- reviewers must be able to inspect each responsibility independently.

If ancillary test/CMake edits are required for one part, keep them in the corresponding commit.

The final PR may later be squash-merged according to repository merge policy; this requirement concerns the PR's implementation history and review structure.

---

# 3. Field evidence from `logs/0905`

The reported reproduction is:

```text
HUD enabled
visibility mode = Always
Microsoft Edge foreground
Edge becomes normal maximized / screen-filling
HUD is no longer visually visible
```

At the relevant Edge transition around `23:26:26.440`, the Edge top-level window was observed as:

```text
hwnd=0x1e039a
pid=11420
class=Chrome_WidgetWin_1
visible=1
iconic=0
rect=-11,-11,1931,1139
style=0x17cf0000
exStyle=0x200100
```

The HUD did **not** show evidence of a normal Win32 hide or loss of topmost state.

Later, while Edge was foreground, the HUD state still reported:

```text
logicalVisible=1
isWindow=1
isWindowVisible=1
isIconic=0
exTopmost=1
rect=391,0,1528,40
```

There was no corresponding HUD `hide-applied`, relevant `WM_SHOWWINDOW`, extended-style mutation, or Z-order `WM_WINDOWPOSCHANGED` at the disappearance transition.

Therefore this PR must **not** treat the problem as an `Always` visibility decision, game-detection decision, HWND visibility issue, or ordinary topmost-style loss.

The existing `[HudWindowState]` diagnostics have already served their purpose and must remain intact.

---

# 4. Current code observation that motivates the new diagnostics

The current production rendering path is conceptually:

```text
HudController::Render
  -> HudPresentation::Render
      -> RefreshDisplayIfNeeded
      -> FormatHud / renderer draw
      -> TryAcquireAvailableBuffer
      -> presentationSurface_->SetBuffer(...)
      -> presentationManager_->Present()
```

`TryAcquireAvailableBuffer()` checks each registered `IPresentationBuffer` using `IsAvailable()`.

When every presentation buffer is unavailable, it returns `S_FALSE`.

`HudPresentation::Render()` currently propagates that `S_FALSE` without submitting a new buffer:

```cpp
HudFrameBuffer* buffer{};
hr = TryAcquireAvailableBuffer(buffer);
if (FAILED(hr) || hr == S_FALSE)
    return hr;
```

At the `HudController` layer, `S_FALSE` is not a failed HRESULT, so the existing `HUD render failed` path does not log it as an error.

That leaves an important current observability gap:

```text
HUD HWND remains visible/topmost
all Presentation buffers unavailable
Render() returns S_FALSE
SetBuffer() is not called
Present() is not called
no error is logged
```

This is **not yet asserted to be the root cause**.

The diagnostic PR must determine which of the following actually happens during the Edge reproduction:

### Case A — presentation-buffer starvation

```text
Edge maximize
-> all Presentation buffers unavailable
-> repeated Render() == S_FALSE
-> no new successful Present
```

### Case B — Presentation submission failure

```text
Edge maximize
-> SetBuffer or Present returns failure HRESULT
-> successful submission stops
```

### Case C — Presentation remains healthy but final screen composition loses the HUD visually

```text
Edge maximize
-> buffers remain available
-> SetBuffer succeeds
-> Present returns S_OK repeatedly
-> successful Present counter continues advancing
-> HUD HWND remains visible/topmost
-> user still cannot see HUD
```

Case C would be especially strong evidence that further investigation belongs below ordinary HWND visibility/Z-order logic, in the Presentation/DComp/display-composition path.

---

# 5. Commit 1 — remove PR #232 Part C warm-up only

## 5.1 Product decision

The PR #232 first-visible presentation warm-up was an empirical workaround, not a proven root-cause fix.

The 2026-09-05 field reproduction occurred after that workaround was already present in production main.

For the next diagnostic build, remove the workaround so that:

- Presentation lifetime is not intentionally perturbed once after startup;
- every reproduced failure can be attributed to the normal production lifecycle;
- diagnostics do not need to distinguish a naturally created presentation from an automatic warm-up recreation;
- future fixes are based on captured Presentation evidence rather than another empirical recreation workaround.

This decision does **not** revert PR #232 as a whole.

## 5.2 Keep PR #232 Parts A and B

Do not revert or weaken any of the following PR #232 behavior:

```text
production IGCL Graphics API probe removal
ProductionGameWindowSource EVENT_OBJECT_NAMECHANGE support
ForegroundTracker::Reconcile() from accepted production window events
NAMECHANGE direct-evaluation debounce
foreground authority repair
```

Those changes address separate verified problems and remain production behavior.

## 5.3 Remove warm-up wiring from `HudController`

Remove warm-up-only API/state from:

```text
src/ClawHUD/HudController.h
src/ClawHUD/HudController.cpp
```

Remove:

```cpp
SetPresentationWarmupScheduler(...)
RunFirstVisiblePresentationWarmup()
scheduleWarmup_
firstVisiblePresentationWarmupAttempted_
```

Remove the warm-up scheduling branch from `HudController::Render()`.

After removal, successful/failed rendering must continue through the existing normal code path only.

Do not otherwise change visibility, rendering, recreation, or settings behavior in this commit.

## 5.4 Remove App/runtime-message wiring

Remove the warm-up-only message and handler from:

```text
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/RuntimeMessageWindow.cpp
```

Remove:

```cpp
kHudPresentationWarmupMessage
App::HandleHudPresentationWarmup()
hudController_.SetPresentationWarmupScheduler(...)
RuntimeMessageWindow WM_APP warm-up branch
```

Do not renumber unrelated existing `WM_APP` messages merely because `WM_APP + 13` becomes unused.

Leaving an unused numeric gap is preferable to unrelated protocol/message churn.

## 5.5 Remove warm-up policy helper only

Current `HudPresentationLifecycle.h` also contains display-refresh behavior used outside the warm-up workaround.

Remove only:

```cpp
ShouldScheduleFirstVisibleHudWarmup(...)
```

and its warm-up-specific comments.

Keep:

```cpp
HudPresentationRefreshPlan
BuildHudPresentationRefreshPlan(...)
```

and current display/DPI refresh semantics unchanged.

Do not delete `HudPresentationLifecycle.h` if it still owns production display-refresh policy.

## 5.6 Update tests

In:

```text
tests/HudPresentationLifecycleTests.cpp
```

remove only tests for the deleted first-visible warm-up policy.

Keep all tests for display-change / presentation refresh policy.

Tree-search after commit 1. Production code should contain none of:

```text
ShouldScheduleFirstVisibleHudWarmup
SetPresentationWarmupScheduler
RunFirstVisiblePresentationWarmup
firstVisiblePresentationWarmupAttempted_
kHudPresentationWarmupMessage
HandleHudPresentationWarmup
HUD first-visible presentation warm-up
```

Historical documentation for PR #232 may remain as historical documentation; do not rewrite old work orders to pretend the workaround never existed.

---

# 6. Commit 2 — add Presentation API state diagnostics

## 6.1 Diagnostic-only scope

Commit 2 must add observability without changing presentation recovery behavior.

Do not:

```text
recreate the presentation on failure
recreate the presentation on S_FALSE
Hide()/Show() as recovery
SetWindowPos(HWND_TOPMOST) as recovery
re-CommitVisibility(true) as recovery
retry Present in a loop
sleep or delay
poll DWM
special-case Edge, Steam, Chromium, games, or fullscreen windows
change visibility mode semantics
change game detection
```

If the reproduction persists, the failure must remain visible to the tester.

## 6.2 Preferred component prefix

Use a new stable debug prefix:

```text
[HudPresentationState]
```

Do not overload `[HudWindowState]` with Presentation API internals.

`[HudWindowState]` remains the Win32/HWND evidence source.

`[HudPresentationState]` becomes the buffer/submission evidence source.

## 6.3 Track a presentation epoch

Add a small diagnostic epoch/generation counter owned by `HudPresentation`.

Required behavior:

- increment on each successful `Initialize()` of the same `HudPresentation` object;
- do not reset it merely because `Shutdown()` occurs before a same-object `Initialize()` during `Recreate()`;
- include the current epoch in every new Presentation diagnostic record.

The HWND must also be included when available, because destroying/recreating the host window naturally gives another identity signal.

This is diagnostic state only; it must not influence behavior.

Example:

```text
[HudPresentationState] reason=initialized epoch=2 hwnd=0x... surface=1137x40 bufferCount=3
```

If implementation simplicity makes a monotonically increasing object-local epoch awkward across complete `HudPresentation` object replacement, HWND + object-local epoch is sufficient. Do not add a global service merely to allocate diagnostic IDs.

## 6.4 Initialization and shutdown records

Add event-level Debug records for successful initialization and shutdown.

Recommended fields for initialization:

```text
reason=initialized
epoch
hwnd
surfaceWidthPx
heightPx
bufferCount
visible
```

The existing production initialization log:

```text
HUD presentation initialized backend=PresentationAPI independentFlip=required alpha=premultiplied ...
```

may remain unchanged.

The new state record should not duplicate every contract field.

Recommended shutdown record:

```text
reason=shutdown
epoch
hwnd
visible
successfulPresentCount
lastSuccessfulPresentTickMs
noBufferActive
consecutiveNoBufferCount
submissionFailureActive
```

Log before diagnostic state is cleared.

Do not turn normal shutdown into Warn/Error.

## 6.5 Capture buffer availability mask

Extend the private buffer-acquisition implementation so diagnostics can know which registered buffers were available on that Render attempt.

A reasonable internal shape is conceptually:

```cpp
HRESULT TryAcquireAvailableBuffer(
    HudFrameBuffer*& selected,
    UINT& availableMask) noexcept;
```

Exact signature may differ.

For each buffer index `i`, set bit `i` when `IsAvailable()` reports true.

For the current 3-buffer contract:

```text
0b000 = none available
0b001 = buffer 0 available
0b010 = buffer 1 available
0b100 = buffer 2 available
...
```

This is diagnostic information only.

Do not change buffer count, order, selection policy, or resource flags.

## 6.6 No-buffer state transition diagnostics

Maintain small per-presentation diagnostic state such as:

```cpp
bool noBufferActive_{};
uint64_t noBufferStartedTickMs_{};
uint64_t consecutiveNoBufferCount_{};
```

Exact types/names may differ.

When `TryAcquireAvailableBuffer()` first returns `S_FALSE` after a non-starved state, emit exactly one transition record:

```text
[HudPresentationState] reason=no-buffer-enter
    epoch=...
    hwnd=...
    availableMask=0x0
    consecutiveNoBuffer=1
    successfulPresentCount=...
    lastSuccessfulPresentTickMs=...
```

Repeated `S_FALSE` results while already in the same no-buffer episode must **not** produce one log line per Render.

They should only increment the internal consecutive counter.

Do not classify a single `S_FALSE` as an error or warning.

### Recovery definition

Treat the no-buffer episode as recovered only after a later frame successfully completes the normal submission path and `presentationManager_->Present()` returns `S_OK`.

At that point emit:

```text
[HudPresentationState] reason=no-buffer-recovered
    epoch=...
    hwnd=...
    durationMs=...
    consecutiveNoBuffer=...
    successfulPresentCount=...
```

Then clear the no-buffer episode state.

This avoids falsely reporting recovery merely because a buffer became available if `SetBuffer()` or `Present()` then failed.

## 6.7 Submission failure diagnostics

Do not collapse `SetBuffer()` and `Present()` failures into a generic render failure without stage information.

Capture the actual stage.

Recommended stable format:

```text
[HudPresentationState] reason=submit-failed stage=set-buffer hr=0x........ ...
```

or:

```text
[HudPresentationState] reason=submit-failed stage=present hr=0x........ ...
```

Maintain a small failure-episode state so repeated identical failures do not flood the log.

At minimum retain:

```text
failure active/not active
first failure tick
last failure HRESULT
last failure stage
failure count
```

On the first failure of an episode, log immediately.

If the same failure repeats, update counters without logging every Render.

If the stage or HRESULT materially changes, an additional transition record is acceptable.

When a later `Present()` returns `S_OK`, emit:

```text
[HudPresentationState] reason=submit-recovered
    previousStage=...
    previousHr=0x........
    durationMs=...
    failureCount=...
    successfulPresentCount=...
```

Then clear the submission failure episode.

The existing `HudController` `HUD render failed` error behavior may remain; this diagnostic layer supplies the missing stage/timing context.

## 6.8 Successful Present counter and timestamp

For each `presentationManager_->Present()` returning exactly `S_OK`:

```text
successfulPresentCount++
lastSuccessfulPresentTickMs = GetTickCount64()
```

These values are diagnostic only.

Do not change rendering cadence or scheduling based on them.

A successful `S_OK` should also close any active no-buffer or submission-failure episode as described above.

## 6.9 Low-rate successful-Present heartbeat

State-transition logging alone cannot prove Case C, because a completely healthy Presentation path would otherwise be silent while the HUD is visually missing.

Therefore add a **very low-rate Debug heartbeat** from the successful Present path.

Recommended cadence:

```text
approximately once every 5 seconds while successful Presents continue
```

Do not add a timer.

Use the existing Render/Present activity and an elapsed tick check.

Recommended record:

```text
[HudPresentationState] reason=present-heartbeat
    epoch=...
    hwnd=...
    successfulPresentCount=...
    lastSuccessfulPresentTickMs=...
    availableMask=0x...
    visible=...
```

The heartbeat exists for one specific diagnostic question:

> Did successful Presentation API submission continue across the exact Edge maximize interval while the HUD was visually absent?

Five-second cadence is intentionally low. Do not log every Present or every telemetry sample.

When normal runtime debug logging is disabled, these Debug records must remain suppressed through the existing logger policy.

Do not add a new diagnostic timer/thread/scheduler.

## 6.10 HRESULT handling must remain identical

The instrumented code must preserve current return semantics exactly.

Conceptually:

```cpp
if (FAILED(setBufferHr))
{
    RecordSubmissionFailure(...);
    return setBufferHr;
}

const HRESULT presentHr = presentationManager_->Present();
if (presentHr == S_OK)
    RecordSuccessfulPresent(...);
else if (FAILED(presentHr))
    RecordSubmissionFailure(...);

return presentHr;
```

For the buffer acquisition path:

```cpp
if (hr == S_FALSE)
{
    RecordNoBuffer(...);
    return S_FALSE;
}
if (FAILED(hr))
    return hr;
```

Instrumentation must not translate HRESULTs, retry operations, or hide failures.

If `IsAvailable()` itself fails, existing failure propagation must remain unchanged. A diagnostic failure-stage record such as `stage=is-available` is optional if it can be added without complicating the implementation.

---

# 7. Non-negotiable HUD / VRR safety boundary

This entire PR must preserve the production HUD presentation contract unchanged.

Do **not** modify, replace, weaken, bypass, or compensate around any of the following:

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
Presentation API production path
DirectComposition production path
premultiplied-alpha presentation contract
presentation buffer texture format
presentation buffer count
presentation resource flags
sample count
alpha mode
identity transform
letterboxing contract
click-through behavior
no-activation behavior
```

The existing production contract currently uses:

```text
DXGI_FORMAT_B8G8R8A8_UNORM
3 buffers
D3D11_RESOURCE_MISC_SHARED
D3D11_RESOURCE_MISC_SHARED_NTHANDLE
D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE
DXGI_ALPHA_MODE_PREMULTIPLIED
independent flip required
```

These are evidence boundaries, not candidate fixes in this PR.

If implementation appears to require changing one of these items, stop and report the conflict instead of changing it.

---

# 8. Opacity boundary

Do not use this investigation to modify opacity semantics.

No window-wide/visual-wide opacity redesign belongs in this PR.

Do not alter current `WS_EX_LAYERED` or `SetLayeredWindowAttributes` behavior as part of Presentation diagnostics.

The reported Edge disappearance must be investigated independently of opacity changes.

---

# 9. Existing diagnostics to preserve

Do not remove or weaken current `[HudWindowState]` coverage, including evidence around:

```text
show-already-visible
show-applied
hide-applied
WM_SHOWWINDOW
relevant WM_WINDOWPOSCHANGED
extended-style changes
display/DPI refresh
foreground HWND/PID
actual IsWindowVisible
actual WS_EX_TOPMOST state
Z-order neighbors
```

The two diagnostic streams should complement each other:

```text
[HudWindowState]       -> Win32 host/window evidence
[HudPresentationState] -> Presentation buffer/submission evidence
```

Do not merge them into a generic diagnostic framework.

---

# 10. Preferred implementation footprint

Expected production code changes should be concentrated in:

```text
src/ClawHUD/HudController.h
src/ClawHUD/HudController.cpp
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/RuntimeMessageWindow.cpp
src/ClawHUD/HudPresentationLifecycle.h
src/ClawHUD/HudPresentation.h
src/ClawHUD/HudPresentation.cpp
tests/HudPresentationLifecycleTests.cpp
```

A small pure helper/test file for diagnostic transition policy is acceptable if it materially improves deterministic unit testing.

Do not introduce:

```text
general observer framework
new service object
global diagnostics manager
new worker thread
new polling source
new timer
DWM integration
browser-specific source
alternate presentation backend
```

Keep the implementation local to the existing HUD presentation ownership boundary.

---

# 11. Unit-test expectations

The diagnostic state transitions should be testable without requiring real Presentation API hardware where practical.

Prefer extracting only the small pure state-decision portion rather than mocking COM/D3D/DComp.

Minimum policy coverage should include:

### Warm-up removal

```text
no first-visible warm-up helper remains
no warm-up scheduling member remains
no WM_APP warm-up message remains
existing display-refresh plan tests still pass
```

### No-buffer episode

```text
first S_FALSE -> enter/log transition
second/third S_FALSE -> count only, no repeated enter log
successful S_OK -> recovered/log transition
new later S_FALSE -> starts a new episode
```

### Submission-failure episode

```text
first SetBuffer failure -> failed/log transition with stage
repeat same failure -> count only
first later S_OK -> recovered/log transition
Present failure is distinguished from SetBuffer failure
new later failure -> starts a new episode
```

### Heartbeat gating

```text
first eligible successful Present can establish heartbeat baseline
successful Presents inside interval -> no heartbeat
first successful Present after interval -> heartbeat
subsequent inside interval -> no heartbeat
heartbeat state has no behavioral effect on Render result
```

Do not attempt to unit-test actual independent flip, DComp composition, or real PresentationManager behavior using fake abstractions solely for this PR.

Those remain hardware acceptance concerns.

---

# 12. Build and regression requirements

Run the repository's current native Debug and Release build/test baseline.

At minimum verify:

```text
Debug build clean
Release build clean
native CTest baseline clean
```

Preserve all existing HUD/VRR contract tests/assertions for:

```text
click-through behavior
no activation
WS_EX_TOPMOST
transparent hit testing
independent flip requirement
premultiplied alpha
ProductionHudPresentationContract()
Presentation API production backend
DirectComposition production path
```

Any regression in those tests is blocking.

Tree-search the final PR for stale warm-up identifiers listed in section 5.6.

Also inspect the final diff and confirm no accidental modifications to:

```text
HudPresentationContract.h
contract values
window style creation
WM_NCHITTEST
WM_MOUSEACTIVATE
buffer resource description
alpha mode
```

If `HudPresentationContract.h` appears in the implementation diff, treat that as a review stop unless the only change is demonstrably non-semantic documentation; preferably it should have **zero diff**.

---

# 13. Required real-device acceptance run

This PR is primarily intended to produce the next trustworthy field log.

Use the same reproduction class as the 0905 report.

## 13.1 Setup

```text
1. Build/install the diagnostic PR.
2. Enable developer DebugLog.
3. Start a fresh ClawHUD process.
4. HUD enabled.
5. Visibility mode = Always.
6. Confirm HUD is visually visible before the test.
7. Open Microsoft Edge in a non-maximized state.
```

Do not deliberately manipulate HUD settings during the primary reproduction unless testing a separate presentation recreation case.

## 13.2 Reproduction

```text
1. Bring Edge foreground.
2. Confirm HUD is visible over the non-maximized browser.
3. Maximize Edge using the normal maximize action.
4. Observe whether HUD disappears.
5. Keep Edge maximized for at least ~15 seconds so multiple 5-second heartbeats can be captured if Present remains healthy.
6. Restore Edge.
7. Observe whether HUD returns.
8. Repeat maximize/restore at least three times if the symptom remains reproducible.
9. Exit normally so final shutdown state is captured.
```

Do not use F11 as the primary test unless separately labeled. The known field case is normal Edge maximize/screen-fill behavior.

## 13.3 Interpretation matrix

### Result A — buffer starvation confirmed

Expected evidence:

```text
Edge maximize timestamp
[HudPresentationState] reason=no-buffer-enter
successfulPresentCount stops advancing
no present-heartbeat advancement during visual disappearance
```

Then the next investigation should focus on Presentation buffer availability/retirement lifecycle.

Do not automatically add recreation as the fix without understanding why all buffers remain unavailable.

### Result B — submission failure confirmed

Expected evidence:

```text
Edge maximize timestamp
[HudPresentationState] reason=submit-failed stage=...
HRESULT captured
successfulPresentCount stops advancing
```

Then investigate the returned HRESULT and the exact failing Presentation API stage.

### Result C — Present remains healthy while HUD is invisible

Expected evidence:

```text
Edge maximize timestamp
HUD visually disappears
[HudWindowState] still visible/topmost
[HudPresentationState] present-heartbeat continues
successfulPresentCount continues increasing
no no-buffer-enter
no submit-failed
```

This is the strongest evidence that ordinary app visibility/render scheduling is not the failure and that investigation should move to the final Presentation/DComp/display-composition behavior while preserving the production contract.

### Result D — no longer reproducible after removing warm-up

Record that result explicitly.

Do not infer causality from one non-reproduction. Repeat the maximize/restore cycle and restart the process for another run before concluding the warm-up itself caused instability.

---

# 14. Explicit non-goals

This PR does not:

```text
fix Edge specifically
fix Steam specifically
change game detection
change Always mode
change foreground authority
change PresentMon API2
change FPS targeting
change HUD geometry
change HUD styling
change opacity behavior
change VRR Fix
change production presentation contract
add a self-healing presentation watchdog
add retries
add presentation recreation on failure
add topmost repair
add DWM polling
```

The outcome is **evidence**, not another speculative workaround.

---

# 15. PR description requirements

The PR description must clearly state:

1. the 0905 field symptom;
2. Win32 HUD visibility/topmost remained normal in the captured log;
3. PR #232 Part C warm-up was empirical and failed to eliminate the field issue;
4. only Part C is removed — PR #232 foreground and IGCL work stays;
5. the new diagnostics distinguish no-buffer, submit failure, and ongoing successful Present;
6. no automatic recovery is added;
7. the production HUD/VRR contract has zero semantic diff;
8. hardware Edge maximize validation is still required after CI.

Include a short expected log example in the PR description so the hardware tester knows what to grep:

```text
[HudPresentationState]
[HudWindowState]
```

---

# 16. Completion checklist

## Commit 1 — warm-up removal

- [ ] `ShouldScheduleFirstVisibleHudWarmup` removed.
- [ ] `SetPresentationWarmupScheduler` removed.
- [ ] `RunFirstVisiblePresentationWarmup` removed.
- [ ] `firstVisiblePresentationWarmupAttempted_` removed.
- [ ] warm-up scheduler callback/state removed.
- [ ] `kHudPresentationWarmupMessage` removed.
- [ ] `HandleHudPresentationWarmup` removed.
- [ ] RuntimeMessageWindow warm-up branch removed.
- [ ] warm-up-only tests removed.
- [ ] display-refresh lifecycle policy retained.
- [ ] PR #232 Parts A/B retained.
- [ ] commit is independently reviewable as warm-up removal only.

## Commit 2 — Presentation diagnostics

- [ ] `[HudPresentationState]` added.
- [ ] presentation epoch/identity logged.
- [ ] buffer availability mask available to diagnostics.
- [ ] first no-buffer transition logged once.
- [ ] repeated no-buffer frames counted without log flooding.
- [ ] successful recovery from no-buffer logged.
- [ ] SetBuffer failure stage logged.
- [ ] Present failure stage logged.
- [ ] repeated identical submission failures do not flood logs.
- [ ] successful submission recovery logged.
- [ ] successful Present counter maintained.
- [ ] last successful Present tick maintained.
- [ ] ~5-second successful-Present heartbeat implemented without a timer.
- [ ] heartbeat only observes; it never changes behavior.
- [ ] existing HRESULT return semantics preserved.
- [ ] no recovery/retry/recreate behavior added.
- [ ] commit is independently reviewable as diagnostics only.

## Safety / regression

- [ ] `ProductionHudPresentationContract()` unchanged.
- [ ] HUD `windowExStyle` unchanged.
- [ ] `WS_EX_TRANSPARENT` unchanged.
- [ ] `WS_EX_NOACTIVATE` unchanged.
- [ ] `WS_EX_TOPMOST` unchanged.
- [ ] `WS_EX_LAYERED` behavior unchanged.
- [ ] `WM_NCHITTEST -> HTTRANSPARENT` unchanged.
- [ ] `WM_MOUSEACTIVATE -> MA_NOACTIVATE` unchanged.
- [ ] independent-flip requirement unchanged.
- [ ] Presentation API production path unchanged.
- [ ] DirectComposition production path unchanged.
- [ ] premultiplied-alpha contract unchanged.
- [ ] presentation buffer count/format/resource flags unchanged.
- [ ] existing `[HudWindowState]` diagnostics preserved.
- [ ] Debug build clean.
- [ ] Release build clean.
- [ ] native CTest clean.
- [ ] final diff reviewed for accidental HUD/VRR contract changes.

## Hardware follow-up

- [ ] fresh-process Always-mode Edge test performed.
- [ ] Edge normal maximize used for primary reproduction.
- [ ] maximized state held long enough for multiple heartbeats.
- [ ] `[HudWindowState]` and `[HudPresentationState]` timestamps correlated.
- [ ] result classified as A/B/C/D from section 13.3.
- [ ] no speculative fix added before reviewing the captured evidence.

---

# 17. Final implementation rule

The purpose of this PR is to return ClawHUD to an unmodified normal Presentation lifecycle and then make that lifecycle observable.

**Delete the failed empirical warm-up first. Instrument second. Keep those changes in two separate commits. Do not repair what has not yet been proven broken. Preserve the VRR-critical production presentation contract exactly.**
