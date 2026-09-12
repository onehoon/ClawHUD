# HUD Already-Visible TOPMOST Reassert POC — 2026-09-12

Status: implementation work order / real-device POC  
Target repository: `onehoon/ClawHUD`  
Prepared against `main` at `1e79a97e1d34a7bcd98eb367de834020b97e6214` (`Add event-driven Presentation Statistics diagnostics (#239)`)  
Scope: minimal Win32 Z-order reassertion when the HUD is already logically visible

---

## 1. Objective

Test and, if hardware validation confirms the behavior, retain the smallest possible fix for the remaining HUD visibility regression where ClawHUD is visible immediately after startup but becomes physically hidden/covered when a fullscreen or screen-filling application appears.

Observed user sequence:

```text
Windows boot / ClawHUD startup
  -> HUD appears normally
  -> fullscreen / screen-filling surface appears shortly afterward
     (Steam Big Picture, Edge/other full-window surface, etc.)
  -> HUD is no longer physically visible
```

Existing diagnostics have repeatedly shown that while the HUD is physically missing:

```text
HudPresentation logical visible = true
IsWindowVisible(HWND) = true
IsIconic(HWND) = false
WS_EX_TOPMOST = present
Presentation buffers remain available
SetBuffer() continues to succeed
Present() continues to succeed
successfulPresentCount continues increasing
```

The current `HudPresentation::Show()` path contains an asymmetry that is now the highest-value concrete candidate:

```cpp
if (visible_)
{
    LogDebugWindowState(L"show-already-visible");
    return S_OK;
}
```

When a foreground/fullscreen transition causes `HudController::ReconcileVisibility()` to resolve to visible, `presentation_->Show()` is called again, but the already-visible branch performs **no HWND Z-order transaction at all**.

By contrast, a real show uses `SetWindowPos(... HWND_TOPMOST ...)`, and the previously observed game/FPS-width recovery path also performs `SetWindowPos(... HWND_TOPMOST ...)` inside `ResizeContentWidth()`.

This POC changes only the already-visible `Show()` branch so it reasserts the existing topmost placement without moving, resizing, activating, showing/hiding, or mutating Presentation/DirectComposition state.

This is a behavior-changing POC, not a logging-only change.

---

## 2. Current code finding

### 2.1 Current `HudPresentation::Show()`

Current `main`:

```cpp
HRESULT HudPresentation::Show()
{
    if (!initialized_)
        return E_UNEXPECTED;
    HRESULT hr = RefreshDisplayIfNeeded();
    if (FAILED(hr)) return hr;
    if (visible_)
    {
        LogDebugWindowState(L"show-already-visible");
        return S_OK;
    }
    hr = CommitVisibility(true);
    if (FAILED(hr)) return hr;
    if (!SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW))
        return LastErrorResult();
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    visible_ = true;
    LogDebugWindowState(L"show-applied");
    return S_OK;
}
```

The first-show path explicitly enters/reasserts the topmost band. The already-visible path does not.

### 2.2 Current visibility reconciliation

`HudController::ReconcileVisibility()` already calls `Show()` whenever the resolved visibility is true:

```cpp
if (resolvedShow)
{
    const bool wasVisible = presentation_->Visible();
    const HRESULT hr = presentation_->Show();
    ...
}
```

Therefore no timer, polling loop, foreground-specific workaround, or new notification source is required.

The existing foreground/window lifecycle naturally provides the opportunity to reassert topmost placement when the desktop/fullscreen environment changes.

### 2.3 Current `Hide()` is not the other application's bug

ClawHUD currently hides using:

```cpp
HRESULT HudPresentation::Hide()
{
    if (!initialized_ || !visible_) return S_OK;
    HRESULT hr = CommitVisibility(false);
    if (FAILED(hr)) return hr;
    ShowWindow(window_, SW_HIDE);
    visible_ = false;
    LogDebugWindowState(L"hide-applied");
    return S_OK;
}
```

It does **not** call:

```cpp
SetWindowPos(window_, HWND_TOP, ..., SWP_HIDEWINDOW)
```

and therefore does not reproduce the separate application's `Hide()` Z-order mutation bug.

Do not redesign ClawHUD `Hide()` in this PR.

---

## 3. Why this is preferred over the 1-pixel resize idea

Do **not** implement a periodic `width +/- 1px` nudge.

A width nudge would touch both HWND geometry and the Presentation source rectangle through the current content-width resize path:

```cpp
SetWindowPos(window_, HWND_TOPMOST, ... new width ...);
presentationSurface_->SetSourceRect(&sourceRect);
```

That intentionally changes presentation geometry and may cause Windows to reevaluate composition / MPO / independent-flip eligibility. ClawHUD is explicitly VRR-sensitive, so a periodic presentation-geometry mutation is not an acceptable first fix.

The proposed already-visible TOPMOST reassertion instead keeps all of these unchanged:

```text
HUD X/Y
HUD width/height
Presentation source rect
Presentation transform
Presentation buffers
D3D/D2D resources
DirectComposition visual/content
Presentation manager
Present cadence
alpha mode
window styles
```

Only the existing HWND's topmost Z-order placement is reasserted.

---

## 4. Required implementation

Modify only the already-visible branch of `HudPresentation::Show()`.

Target behavior:

```cpp
HRESULT HudPresentation::Show()
{
    if (!initialized_)
        return E_UNEXPECTED;

    HRESULT hr = RefreshDisplayIfNeeded();
    if (FAILED(hr))
        return hr;

    if (visible_)
    {
        if (!SetWindowPos(
                window_,
                HWND_TOPMOST,
                0, 0, 0, 0,
                SWP_NOMOVE |
                SWP_NOSIZE |
                SWP_NOACTIVATE |
                SWP_NOOWNERZORDER))
        {
            return LastErrorResult();
        }

        LogDebugWindowState(L"show-already-visible-topmost-reasserted");
        return S_OK;
    }

    hr = CommitVisibility(true);
    if (FAILED(hr))
        return hr;

    if (!SetWindowPos(
            window_,
            HWND_TOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE |
            SWP_NOSIZE |
            SWP_NOACTIVATE |
            SWP_SHOWWINDOW))
    {
        return LastErrorResult();
    }

    ShowWindow(window_, SW_SHOWNOACTIVATE);
    visible_ = true;
    LogDebugWindowState(L"show-applied");
    return S_OK;
}
```

The exact formatting may follow repository style.

### Required flags for the already-visible reassertion

Use:

```text
HWND_TOPMOST
SWP_NOMOVE
SWP_NOSIZE
SWP_NOACTIVATE
SWP_NOOWNERZORDER
```

Do not add `SWP_SHOWWINDOW` in the already-visible branch because the purpose is **not** to show/hide the HWND again; Windows already reports it visible.

Do not use `SWP_NOZORDER`, because that would make `HWND_TOPMOST` ineffective and defeat the POC.

---

## 5. Important semantic boundary

This change must be treated as **reasserting an existing invariant**, not introducing a new window policy.

The production contract already requires:

```cpp
WS_EX_TOPMOST
```

and current creation/show/resize paths already use:

```cpp
SetWindowPos(window_, HWND_TOPMOST, ...)
```

Do not change `ProductionHudPresentationContract()`.

Do not add a second topmost mechanism.

There is no WinUI `OverlappedPresenter::IsAlwaysOnTop` equivalent to add here because the HUD host is a native Win32 HWND, not a WinUI `AppWindow`.

---

## 6. HUD / VRR safety contract — non-negotiable

This PR must have zero changes to all of the following:

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

Current contract must remain byte-for-byte equivalent:

```cpp
constexpr HudPresentationContract ProductionHudPresentationContract() noexcept
{
    return {
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT |
            WS_EX_LAYERED | WS_EX_TOPMOST,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        1,
        3,
        D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
            D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE,
        DXGI_ALPHA_MODE_PREMULTIPLIED,
        true,
        0.0f, 0.0f, 0.0f, 0.0f,
        true
    };
}
```

---

## 7. Explicitly forbidden changes

Do not add or change any of the following in this POC:

```text
periodic timer / watchdog
5-second TOPMOST timer
1-pixel resize/nudge
SetSourceRect() reassertion
SetTransform() mutation
Presentation buffer recreation
Presentation manager recreation
DComp visual detach/reattach
DComp Commit() recovery
ShowWindow(SW_HIDE) / show-hide cycling as recovery
SetForegroundWindow()
Window activation
focus stealing
windowExStyle mutation
WS_EX_TOPMOST toggling off/on
MPO enable/disable
DWM restart/reset
ForceVSyncInterrupt()
SetPreferredPresentDuration()
SetTargetTime()
CancelPresentsFrom()
browser-specific logic
Steam-specific logic
game-specific logic
fullscreen detection added only for this workaround
```

Do not change `ResizeContentWidth()` for this POC.

Do not modify the Presentation Statistics implementation in this PR. The known `GetNextPresentStatistics()` / `E_FAIL` diagnostic issue should be handled separately so the causality of this POC stays clean.

---

## 8. Logging

Keep logging minimal.

The existing `LogDebugWindowState()` already captures:

```text
logicalVisible
IsWindowVisible
IsIconic
exStyle
exTopmost
rect
foreground HWND/PID
zPrev/zNext
```

Change the existing already-visible reason from:

```text
show-already-visible
```

to:

```text
show-already-visible-topmost-reasserted
```

only after successful `SetWindowPos()`.

If `SetWindowPos()` fails, return `LastErrorResult()` and allow the existing `HudController::ReconcileVisibility()` failure logging path to report the failure.

Do not add another high-frequency log stream.

---

## 9. Expected file scope

Prefer one implementation PR.

Expected production file:

```text
src/ClawHUD/HudPresentation.cpp
```

Potential tests/docs only if needed:

```text
tests/HudPresentationContractTests.cpp
```

Do not edit `HudPresentationContract.h`.

Do not refactor `HudController`, game detection, foreground tracking, telemetry, Settings, IPC, or diagnostics unless compilation strictly requires it.

Expected implementation delta should be very small.

---

## 10. Tests and verification

### 10.1 Existing contract regression tests

All existing tests must remain green, especially assertions covering:

```text
WS_EX_LAYERED
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
shared displayable buffers
premultiplied alpha
identity transform
zero letterbox margins
independent flip required
```

`ProductionHudPresentationContract()` must have zero semantic diff.

Also manually verify source still contains:

```cpp
if (message == WM_NCHITTEST) return HTTRANSPARENT;
if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
```

### 10.2 Build/test

Run the repository's normal validation:

```text
Debug build
Release build
native CTest suite
Settings/WPF tests if part of normal CI
```

No new warnings.

### 10.3 Functional state checks

Verify that the already-visible path does **not** change:

```text
window rectangle
window size
logical visibility
activation / foreground ownership
Presentation epoch
Presentation source rectangle
Presentation resource generation
```

A successful already-visible reassertion should produce a Win32 Z-order transaction only.

---

## 11. Required real-device A/B test

This POC exists for hardware validation. Do not declare the root cause fixed from CI alone.

### Setup

Use the same HUD configuration that reproduced the issue:

```text
HUD Enabled=true
HUD Visibility=Always
```

For the cleanest causality test, prefer `DebugLoggingEnabled=false` first so Presentation Statistics / debug observers are not active.

### Reproduction A — boot/startup ordering

This is the highest-value test because the user reports the failure can happen immediately after boot:

```text
1. Start Windows / start ClawHUD.
2. Confirm HUD initially appears.
3. Let the normal fullscreen/screen-filling application appear after startup,
   or open Steam Big Picture immediately.
4. Confirm whether HUD remains physically visible.
5. Switch between desktop / fullscreen surfaces several times.
```

Expected result:

```text
HUD remains visible without resize, render-resource recreation, or manual recovery.
```

### Reproduction B — Edge / screen-filling window

```text
1. HUD visible in Always mode.
2. Maximize / fullscreen Edge using the same sequence that previously covered HUD.
3. Return to desktop.
4. Repeat several times.
```

### Reproduction C — Steam Big Picture

```text
1. HUD visible in Always mode.
2. Enter Steam Big Picture.
3. Keep it open long enough to reproduce the previous disappearance.
4. Exit and re-enter Big Picture.
```

### Reproduction D — game launch / FPS-width transition

```text
1. Start from the desktop with HUD visible.
2. Launch the same game used in the previous reproduction.
3. Confirm the HUD is already visible before FPS becomes valid.
4. Confirm the later FPS content-width change is no longer required to recover the HUD.
```

This is important because the previous recovery correlation was:

```text
FPS becomes valid
  -> content width changes
  -> ResizeContentWidth()
  -> SetWindowPos(HWND_TOPMOST, ...)
  -> HUD becomes visible again
```

If this POC works, the HUD should already be visible before that width transition occurs.

---

## 12. VRR validation

The purpose of this implementation shape is to avoid touching Presentation geometry or pacing, but hardware validation is still required.

Check a known VRR-working game and verify:

```text
VRR still engages normally
no periodic visible hitch
no periodic HUD geometry movement
no focus/activation change
no loss of click-through
no new presentation recreation
```

There must be no timer-based reassertion. The call should happen only through the existing visibility reconciliation flow.

If VRR behavior changes, stop and report the conflict. Do not compensate by changing the production Presentation contract.

---

## 13. Interpretation after hardware test

### Case A — issue disappears

If the startup/fullscreen covering issue stops reproducing and the HUD stays visible before any content-width resize:

```text
Strong evidence:
foreground/fullscreen transitions can leave the logically-visible HUD without an effective current topmost placement/order transaction, and reasserting the existing HWND_TOPMOST invariant during visibility reconciliation repairs it.
```

If VRR and presentation behavior remain normal, this minimal behavior can be considered for retention as the production fix.

### Case B — issue still occurs

If the HUD still disappears despite successful already-visible TOPMOST reassertions:

```text
Do not add a periodic TOPMOST timer.
Do not move to the 1px resize workaround.
Do not change SetSourceRect / DComp / Presentation policy.
```

The result would substantially weaken the pure Win32 Z-order hypothesis and move investigation back toward DWM/final-composition/display-plane behavior.

### Case C — HUD stays visible but VRR regresses

Do not keep the change as a production fix.

Preserve the existing presentation contract and report the conflict for explicit design review.

---

## 14. PR review checklist

```text
[ ] only already-visible Show() behavior changed
[ ] HWND_TOPMOST reused; no new topmost mechanism
[ ] SWP_NOMOVE present
[ ] SWP_NOSIZE present
[ ] SWP_NOACTIVATE present
[ ] SWP_NOOWNERZORDER present
[ ] SWP_SHOWWINDOW absent from already-visible branch
[ ] SWP_NOZORDER absent from reassertion call
[ ] no SetSourceRect change
[ ] no resize/nudge
[ ] no timer/watchdog
[ ] no Show/Hide recovery cycling
[ ] no focus/activation behavior
[ ] no ProductionHudPresentationContract change
[ ] WM_NCHITTEST -> HTTRANSPARENT preserved
[ ] WM_MOUSEACTIVATE -> MA_NOACTIVATE preserved
[ ] independent-flip requirement preserved
[ ] premultiplied-alpha contract preserved
[ ] Presentation API / DirectComposition path preserved
[ ] normal build/tests green
[ ] real-device startup/fullscreen A/B completed before declaring root cause fixed
[ ] VRR hardware smoke remains normal
```

---

## 15. Summary

Implement exactly one behavioral experiment:

```text
existing visible HUD
  + visibility reconcile requests Show()
  -> reassert HWND_TOPMOST
  -> no move
  -> no resize
  -> no activation
  -> no Presentation/DComp mutation
```

Do not add a general Z-order manager and do not add periodic recovery machinery.

The goal is to determine whether the already-observed recovery from `ResizeContentWidth()` was caused by its `SetWindowPos(HWND_TOPMOST, ...)` transaction rather than by the resize or `SetSourceRect()` change.
