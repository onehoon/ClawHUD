# Work Order — HUD `Always` Visibility / Z-Order Diagnostic Logging

Status: implementation work order  
Prepared from `main` at `74618de0969648be82ca807291bd207f9a21b024`  
Date: 2026-09-02  
Scope: diagnostic-only instrumentation for an observed HUD disappearance while `HudVisibilityMode::Always` remained active

---

## 1. Decision

Add targeted **debug-only diagnostic logging** around the production HUD window/presentation lifecycle before attempting any behavioral fix.

This PR must answer one concrete question:

> When ClawHUD internally believes the HUD is already visible (`visible_ == true`), is the actual HUD HWND still visible and still in the expected topmost Z-order state?

Do **not** repair, re-show, re-topmost, recreate, or otherwise change HUD behavior in this PR.

The purpose of this work is to capture enough evidence from a real-device reproduction to distinguish among:

```text
A. logical visible state is true, but the HWND is actually hidden
B. logical visible state is true, but the HWND is no longer topmost
C. logical visible state and HWND state are both correct, but presentation/content disappeared
D. another window/presentation transition is responsible
```

The next behavioral fix must be based on the captured evidence, not on speculation.

---

## 2. Observed reproduction that motivates this work

A 2026-09-02 real-device log captured the following user-visible sequence:

```text
HUD mode = Always
Microsoft Edge is visible
Edge is maximized using the normal maximize action, not F11 fullscreen
HUD disappears
Open ClawHUD Settings from the tray
HUD becomes visible again
```

Relevant runtime evidence from the captured log:

### Edge reaches normal maximized state

At approximately `01:05:54`, the foreground Edge top-level window was logged as:

```text
class="Chrome_WidgetWin_1"
visible=1
iconic=0
rect=-11,-11,1931,1139
exStyle=0x200100
```

This is consistent with a normal maximized Chromium/Edge window rather than an F11 fullscreen transition.

### `Always` mode remains active

Later foreground transitions still logged:

```text
[PresentMonFPS] mode=Always foregroundPid=13772 fps-invalidated
```

The game-detection observer also classified Edge as non-game/hidden evidence, but `HudVisibilityMode::Always` should not depend on game admission.

### Opening Settings correlates with HUD recovery

At approximately `01:06:19`:

```text
CREATE title="ClawHUD Settings" class="ClawHUD.SettingsWindow" visible=1
```

The HUD was observed to become visible again after this tray/UI interaction.

This correlation is useful but does not prove the mechanism. We need HUD HWND state at the exact transition.

---

## 3. Current code path that must be instrumented

Read current `main` before editing.

The relevant production code is currently in:

```text
src/ClawHUD/HudPresentation.cpp
src/ClawHUD/HudPresentation.h
src/ClawHUD/HudPresentationContract.h
src/ClawHUD/HudPresentationContract.cpp
src/ClawHUD/HudPresentationLifecycle.h
src/ClawHUD/HudPresentationLifecycle.cpp
src/ClawHUD/RuntimeLogger.h
src/ClawHUD/RuntimeLogger.cpp
```

Also read:

```text
docs/HUD_PRESENTATION_REFACTOR_GUARDRAIL.md
docs/APP_REFACTOR_BEHAVIOR_INVENTORY_TEMPLATE.md
```

The current `Show()` path is important:

```cpp
HRESULT HudPresentation::Show()
{
    if (!initialized_) return E_UNEXPECTED;
    HRESULT hr = RefreshDisplayIfNeeded();
    if (FAILED(hr)) return hr;
    if (visible_) return S_OK;
    hr = CommitVisibility(true);
    if (FAILED(hr)) return hr;
    if (!SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW))
        return LastErrorResult();
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    visible_ = true;
    return S_OK;
}
```

The diagnostic target is specifically the early-return state:

```cpp
if (visible_) return S_OK;
```

Today that branch tells us only the internal boolean. It does not tell us whether the real HWND is visible/topmost or where it sits in Z-order.

---

## 4. Non-negotiable HUD / VRR safety contract

This PR is instrumentation only.

Do not modify, replace, weaken, or work around any of the following:

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
```

In particular, **do not** change this PR into a speculative fix by doing any of the following:

```text
SetWindowPos(HWND_TOPMOST) from the visible_ early-return path
ShowWindow() from the visible_ early-return path
changing visible_ from IsWindowVisible()
calling Hide()/Show() to self-heal
recreating the presentation surface
changing window styles
changing activation/hit-test behavior
changing foreground/game-detection semantics
adding polling/watchdog recovery
```

If instrumentation reveals an invariant violation, record it. Fix it in a separate reviewed change.

---

## 5. Goal

When debug logging is enabled, the log must make it possible to reconstruct:

```text
logical HUD visibility state
actual HWND validity
actual HWND visibility
actual HWND iconic/minimized state
actual extended window style
actual WS_EX_TOPMOST state
actual HUD window rectangle
foreground HWND and PID
nearest HWNDs around the HUD in Z-order
relevant HUD WM_SHOWWINDOW / WM_WINDOWPOSCHANGED / ex-style changes
whether Show() took the already-visible early return
whether a real Show()/Hide() operation was performed
```

The data must be timestamp-correlatable with the existing `WindowLifecycle`, `GameDetection`, `GameIdentity`, and `PresentMonFPS` debug logs.

---

## 6. Logging level and normal-build impact

All new diagnostics must use:

```cpp
RuntimeLogLevel::Debug
```

Do not emit these records at `Info`, `Warn`, or `Error` during normal successful operation.

`[Developer] DebugLog=0` must continue to suppress the new records through the existing logger policy.

Avoid per-frame/per-sample diagnostics.

Do not add logging inside:

```text
HudPresentation::Render() hot path
buffer acquisition loop
telemetry sampling loop
PresentMon frame/sample loop
```

The intended volume is event/lifecycle based, not frame based.

---

## 7. Add one focused HUD HWND snapshot helper

Prefer a small private helper in `HudPresentation` or a file-local helper in `HudPresentation.cpp` rather than scattering repeated Win32 probes throughout the class.

A reasonable shape is conceptually:

```cpp
void HudPresentation::LogDebugWindowState(
    std::wstring_view reason,
    const WINDOWPOS* windowPos = nullptr) const noexcept;
```

Exact API/name may differ if a smaller implementation is cleaner.

Do not create a general diagnostics framework, observer bus, watchdog, interface hierarchy, or new service.

The helper is diagnostic only.

### Required snapshot fields

For the HUD HWND, capture at minimum:

```text
reason
hwnd
initialized
logicalVisible        // current visible_
isWindow              // IsWindow(window_)
isWindowVisible       // IsWindowVisible(window_)
isIconic              // IsIconic(window_)
exStyle               // GetWindowLongPtrW(..., GWL_EXSTYLE)
exTopmost             // (exStyle & WS_EX_TOPMOST) != 0
rect                   // GetWindowRect
```

Also capture the current foreground window:

```text
foregroundHwnd
foregroundPid
```

Capture immediate Z-order neighbors when available:

```text
zPrevHwnd
zPrevPid
zNextHwnd
zNextPid
```

Use the existing Win32 APIs only; do not introduce a polling thread.

Recommended calls:

```cpp
GetForegroundWindow()
GetWindowThreadProcessId(...)
GetWindow(window_, GW_HWNDPREV)
GetWindow(window_, GW_HWNDNEXT)
GetWindowLongPtrW(window_, GWL_EXSTYLE)
GetWindowRect(window_, ...)
IsWindow(window_)
IsWindowVisible(window_)
IsIconic(window_)
```

Do not over-interpret `GW_HWNDPREV/GW_HWNDNEXT` in the log message. Record the raw neighboring HWND/PID evidence so the reproduction can be reconstructed afterward.

### Failure-safe diagnostics

Logging must never affect HUD behavior.

If any diagnostic API fails, log a neutral/unknown value and continue.

Do not return an error from `Show()`, `Hide()`, `WindowProc`, or initialization merely because a debug probe failed.

---

## 8. Required `Show()` instrumentation

### 8.1 Already-visible early-return — highest priority

Immediately before the current early return:

```cpp
if (visible_)
    return S_OK;
```

emit a snapshot with a stable reason such as:

```text
show-already-visible
```

The resulting sequence should conceptually be:

```cpp
if (visible_)
{
    LogDebugWindowState(L"show-already-visible");
    return S_OK;
}
```

Do **not** change the return behavior.

This is the most important line in the PR because it will tell us whether:

```text
visible_=1 + IsWindowVisible=0
```

or:

```text
visible_=1 + exTopmost=0
```

occurs during the Edge reproduction.

### 8.2 Real show path

After the existing successful:

```text
CommitVisibility(true)
SetWindowPos(... HWND_TOPMOST ...)
ShowWindow(... SW_SHOWNOACTIVATE)
visible_ = true
```

emit a second snapshot with a stable reason such as:

```text
show-applied
```

Do not reorder the existing operations to make logging easier.

### 8.3 Display refresh path

Do not alter `RefreshDisplayIfNeeded()` behavior.

If the refresh path recreates a visible presentation, ensure the subsequent diagnostic records remain sufficient to distinguish:

```text
ordinary Show early-return
vs
Show after display/presentation refresh
```

A dedicated debug record such as `display-refresh-recreate` is acceptable if needed, but behavior must remain identical.

---

## 9. Required `Hide()` instrumentation

Keep current `Hide()` semantics unchanged.

After an actual successful hide operation, emit a snapshot/reason such as:

```text
hide-applied
```

If `Hide()` returns early because the HUD is already logically hidden, a lightweight record such as:

```text
hide-already-hidden
```

is optional, not mandatory.

Do not create excessive noise.

The primary investigation is `Always` mode unexpectedly disappearing while `visible_` is expected to remain true.

---

## 10. Required HUD `WindowProc` diagnostics

The HUD HWND itself must report relevant visibility/Z-order mutations so we are not dependent only on App-level foreground events.

### 10.1 `WM_SHOWWINDOW`

Add a debug record for:

```text
WM_SHOWWINDOW
wParam              // shown/hidden
lParam              // status cause
```

Then include the HUD window snapshot.

This will tell us whether Windows actually sends a hide/show transition that the internal `visible_` boolean does not reflect.

Do not change the return behavior of `WM_SHOWWINDOW` beyond the existing default processing requirements.

### 10.2 `WM_WINDOWPOSCHANGED`

Log `WM_WINDOWPOSCHANGED` only when it is relevant to visibility or Z-order.

Avoid size/move noise caused by ordinary HUD geometry updates.

Recommended condition:

```cpp
const auto* pos = reinterpret_cast<const WINDOWPOS*>(lParam);
const bool zOrderChanged = pos && !(pos->flags & SWP_NOZORDER);
const bool showHideChanged = pos &&
    (pos->flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)) != 0;

if (zOrderChanged || showHideChanged)
    ...log...
```

For relevant records capture:

```text
hwndInsertAfter
flags (hex)
x
y
cx
cy
```

and the standard HUD snapshot.

This is particularly important because a topmost/Z-order transition should become visible here even if no App-level visibility state changes.

### 10.3 `WM_STYLECHANGED` for extended style

If the HUD extended style changes, record it.

For:

```cpp
message == WM_STYLECHANGED && wParam == GWL_EXSTYLE
```

record:

```text
styleOld
styleNew
oldTopmost
newTopmost
```

using `STYLESTRUCT` from `lParam`, then include/associate the HUD snapshot.

Do not modify the style from this handler.

### 10.4 Existing display/DPI handling

Current behavior:

```cpp
if (message == WM_DISPLAYCHANGE || message == WM_DPICHANGED)
{
    if (self) self->displayChangePending_ = true;
    return 0;
}
```

Preserve it.

A debug record indicating:

```text
wm-displaychange
wm-dpichanged
```

is useful because display recreation can otherwise look like spontaneous HUD loss/recovery.

Logging must not change `displayChangePending_` semantics.

### 10.5 Do not add activation behavior

Do not use this investigation as a reason to handle or alter:

```text
WM_ACTIVATE
WM_SETFOCUS
WM_KILLFOCUS
WM_MOUSEACTIVATE
WM_NCHITTEST
```

The existing no-activation and transparent hit-test behavior is contractual.

---

## 11. Recommended log format

Use a stable component prefix so the reproduction can be filtered easily.

Recommended prefix:

```text
[HudWindowState]
```

Example early-return record:

```text
[HudWindowState] reason=show-already-visible hwnd=0x123456 initialized=1 logicalVisible=1 isWindow=1 isWindowVisible=0 isIconic=0 exStyle=0x... exTopmost=1 rect=0,0,1920,44 foregroundHwnd=0x2507ae foregroundPid=13772 zPrevHwnd=0x... zPrevPid=... zNextHwnd=0x... zNextPid=...
```

Example relevant window-position record:

```text
[HudWindowState] reason=wm-windowposchanged hwnd=0x123456 logicalVisible=1 ... hwndInsertAfter=0x... flags=0x... x=0 y=0 cx=1920 cy=44
```

Example ex-style record:

```text
[HudWindowState] reason=wm-stylechanged-exstyle hwnd=0x123456 logicalVisible=1 styleOld=0x... styleNew=0x... oldTopmost=1 newTopmost=0 ...
```

Exact field ordering may differ, but keep names stable and grep-friendly.

Do not log window titles from the HUD helper unless there is a concrete need. Existing `WindowLifecycle` logs already provide foreground window metadata, and HWND/PID correlation is sufficient.

---

## 12. Do not change `Always` visibility semantics

Do not modify:

```text
App::ReconcileHudVisibility
HudController visibility mode
manual override semantics
HudVisibilityMode::Always
HudVisibilityMode::InGameOnly
foreground game detection
GameSessionController
PresentMonFPS foreground invalidation
```

The observed log already shows `mode=Always` remains active.

This work order is not a game-detection change.

Do not make Edge special-cased.

Do not add browser-specific logic.

---

## 13. No speculative presentation fix in this PR

The following tempting change is explicitly out of scope:

```cpp
if (visible_)
{
    SetWindowPos(window_, HWND_TOPMOST, ...);
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    return S_OK;
}
```

Do not implement it yet.

Likewise, do not write:

```cpp
visible_ = IsWindowVisible(window_) != FALSE;
```

or any equivalent state reconciliation.

Reason:

We do not yet know whether the failed invariant is HWND visibility, topmost state, presentation content, DComp state, or another transition. The diagnostic PR must preserve the failure if it reproduces so the log can identify it.

---

## 14. Preferred implementation footprint

Expected production-code scope should remain very small:

```text
src/ClawHUD/HudPresentation.cpp
src/ClawHUD/HudPresentation.h        // only if a private helper declaration is needed
```

Potential tests/docs as needed:

```text
tests/HudPresentationLifecycleTests.cpp
```

Do not touch unrelated game-detection, telemetry, Settings, tray, renderer, or PresentMon files.

Do not refactor `HudPresentation` while adding the diagnostics.

Keep this comfortably below the project's 500 LOC per PR guideline.

---

## 15. Automated verification

At minimum run the existing HUD/presentation tests:

```text
ClawHUD.HudPresentationContractTests
ClawHUD.HudPresentationLifecycleTests
```

Also run the full test suite with `ctest`.

Required contract checks:

```text
ProductionHudPresentationContract() unchanged
windowExStyle unchanged
WS_EX_TRANSPARENT unchanged
WS_EX_NOACTIVATE unchanged
WS_EX_TOPMOST behavior unchanged
existing WS_EX_LAYERED behavior unchanged
WM_NCHITTEST -> HTTRANSPARENT unchanged
WM_MOUSEACTIVATE -> MA_NOACTIVATE unchanged
independent-flip requirement unchanged
Presentation API / DirectComposition path unchanged
premultiplied-alpha contract unchanged
```

No new unit-test abstraction is required merely to unit test Win32 logging strings.

If implementation extracts any pure diagnostic formatting/state-classification helper, small tests are acceptable, but do not increase architecture solely for testability.

---

## 16. Required real-device reproduction after implementation

Run with:

```ini
[Developer]
DebugLog=1
```

and HUD settings:

```text
HUD enabled
Visibility = Always
```

### Reproduction A — original case

1. Start ClawHUD normally.
2. Confirm HUD is visible.
3. Open Microsoft Edge.
4. Keep Edge as foreground.
5. Maximize Edge using the normal title-bar maximize behavior.
6. Do **not** use F11 for the primary reproduction.
7. If the HUD disappears, leave the system untouched for several seconds so all event logs flush naturally.
8. Open ClawHUD Settings from the tray, matching the original recovery sequence.
9. Observe whether the HUD returns.
10. Save the runtime log.

### Reproduction B — maximize/restore control

Repeat:

```text
Edge normal window
-> maximize
-> restore
-> maximize
```

Capture whether HUD loss follows a consistent transition.

### Reproduction C — optional F11 comparison

After the original reproduction is captured, optionally compare:

```text
normal maximize
vs
Edge F11 fullscreen
```

This is comparison evidence only. Do not change scope based on the F11 result in this PR.

---

## 17. What to look for in the reproduced log

The implementation is successful if the log can distinguish at least the following cases.

### Case 1 — logical/actual visibility mismatch

```text
reason=show-already-visible
logicalVisible=1
isWindowVisible=0
```

This would strongly support a stale internal visibility-state problem.

### Case 2 — topmost invariant lost

```text
reason=show-already-visible
logicalVisible=1
isWindowVisible=1
exTopmost=0
```

or a preceding relevant:

```text
wm-windowposchanged
wm-stylechanged-exstyle
```

showing loss/change of topmost state.

### Case 3 — HWND state is healthy

```text
logicalVisible=1
isWindowVisible=1
exTopmost=1
```

while the HUD is still visually absent.

This points the next investigation away from simple HWND visibility/topmost repair and toward the presentation/DComp/content side.

### Case 4 — tray Settings changes HWND state

The log should show exactly what changes immediately around:

```text
ClawHUD Settings foreground/create
HUD becomes visible again
```

For example:

```text
WM_WINDOWPOSCHANGED
WM_SHOWWINDOW
Show() actual path
style/topmost change
```

or no HUD HWND change at all.

That evidence will decide the follow-up fix.

---

## 18. Debug-off sanity check

Also run once with:

```ini
[Developer]
DebugLog=0
```

Confirm:

```text
no [HudWindowState] debug records are written
HUD behavior is unchanged
no new polling/thread/timer exists
normal tray/background operation remains unchanged
```

---

## 19. PR description requirements

The implementation PR should state clearly:

```text
This PR adds diagnostics only. It intentionally does not fix the observed HUD disappearance.
```

Include:

1. the original reproduction sequence,
2. which `HudWindowState` events were added,
3. confirmation that the production HUD presentation/VRR contract is unchanged,
4. automated test results,
5. real-device reproduction result if available.

If the real-device reproduction catches the failure, include a short filtered excerpt around:

```text
Edge maximize
HUD disappears
show-already-visible / WM_WINDOWPOSCHANGED / WM_SHOWWINDOW
ClawHUD Settings opens
HUD returns
```

Do not implement the behavioral fix in the same PR after seeing the log. Preserve the diagnostic PR boundary and review the evidence first.

---

## 20. Acceptance criteria

The work is complete only when all are true:

- [ ] `HudPresentation::Show()` logs the actual HUD HWND state before the `visible_ == true` early return.
- [ ] The normal real-show path logs post-show state.
- [ ] Actual hide operations are observable in debug logs.
- [ ] Relevant `WM_SHOWWINDOW` transitions are observable.
- [ ] Relevant Z-order/show-hide `WM_WINDOWPOSCHANGED` transitions are observable without logging every geometry update.
- [ ] Extended-style changes are observable without modifying the style.
- [ ] Foreground HWND/PID is included for timeline correlation.
- [ ] Immediate HUD Z-order neighbor HWND/PIDs are included when available.
- [ ] All new records are Debug level.
- [ ] No per-frame/per-telemetry logging was added.
- [ ] No polling, timer, worker, watchdog, or recovery loop was added.
- [ ] `HudVisibilityMode::Always` behavior is unchanged.
- [ ] `if (visible_) return S_OK;` behavior is unchanged except for logging before return.
- [ ] No speculative `SetWindowPos`, `ShowWindow`, visibility reconciliation, or presentation recreation was added.
- [ ] Production HUD presentation/VRR contract is unchanged.
- [ ] `ClawHUD.HudPresentationContractTests` pass.
- [ ] `ClawHUD.HudPresentationLifecycleTests` pass.
- [ ] Full `ctest` suite passes.
- [ ] DebugLog OFF produces none of the new debug records.
- [ ] Real-device Edge maximize reproduction is attempted and the resulting evidence is retained for follow-up review.

---

## 21. Stop conditions

Stop and report for review instead of expanding scope if implementation appears to require changing any of:

```text
ProductionHudPresentationContract()
windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST contract
WS_EX_LAYERED behavior
WM_NCHITTEST
WM_MOUSEACTIVATE
independent flip
Presentation API / DirectComposition path
premultiplied alpha
Always/InGameOnly visibility policy
```

This PR's job is to observe the failure precisely, not to solve it prematurely.
