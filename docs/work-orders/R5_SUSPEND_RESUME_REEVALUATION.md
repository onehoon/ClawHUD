# Work Order — R5: Keep Suspend/Resume as Top-Level `App` Orchestration

Status: implementation work order  
Prepared from `main` at `751996057f9da4fc2db301f1fe9004dc49983dbb` after R4 / PR #187  
Scope: R5 of `docs/APP_REFACTOR_PLAN.md`

---

## 1. Decision from the R5 re-evaluation

Re-read the current post-R4 `App.cpp` before changing architecture.

R2, R3 and R4 have already moved detailed domain state into:

```text
ProductionTelemetryController
HudController
GameSessionController
```

The remaining suspend/resume ownership in `App` is now limited and cohesive:

```text
suspended_
resumeRecoveryActive_
resumeRecoveryAttempts_

HandleSystemSuspend()
HandleSystemResume()
TryResumeRecovery()
CancelResumeRecovery()
```

These methods now coordinate the three production controllers through narrow, high-level APIs. That is exactly the type of cross-domain application orchestration that `App` is intended to retain.

### R5 architectural decision

**Do not create**:

```text
RuntimeLifecycleController
SuspendResumeCoordinator
SuspendResumeController
PowerLifecycleController
```

The current code does not justify another stateful controller solely to reduce `App.cpp` line count.

R5 is therefore a **low-risk boundary-confirmation / cleanup PR**, not another ownership extraction.

---

## 2. Goal

Complete the R5 phase by:

1. explicitly confirming that suspend/resume remains top-level `App` orchestration;
2. preserving the R1 pure policy boundary in `SuspendResumePolicy.h`;
3. verifying that all detailed HUD, telemetry and game-session operations already go through controller APIs;
4. removing only obvious suspend/resume-local redundancy if behavior is provably identical;
5. documenting the final lifecycle ownership decision in `APP_REFACTOR_PLAN.md`.

Do **not** create new architecture merely to make the diff larger or `App.cpp` smaller.

A docs-only or very small runtime diff is an acceptable and expected R5 outcome.

---

## 3. Keep the R1 pure policy boundary unchanged

`src/ClawHUD/SuspendResumePolicy.h` remains the pure decision layer.

Preserve:

```text
kResumeRecoveryIntervalMs = 500
kResumeRecoveryMaxAttempts = 6

ResumeRecoveryShouldStart
ResumeRecoveryNeedsSuspendFallback
ResumeRecoveryHasAttemptsRemaining
ResumeRecoveryCanRetainVerifier
ResumeRecoveryShouldWaitForForeground
ResumeRecoveryMayShowHud
ResumeRecoveryFrameWasPresented
```

Do not move Win32 orchestration into the policy file.

Do not add controller references, HWND ownership, timers, logging, or runtime objects to `SuspendResumePolicy`.

`kResumeRecoveryTimerId` remains application/message-loop wiring and stays with `App` unless a later independent cleanup has a concrete reason to move it.

---

## 4. Preserve `App` lifecycle state ownership

Keep these members in `App`:

```cpp
bool suspended_{};
bool resumeRecoveryActive_{};
unsigned resumeRecoveryAttempts_{};
```

There must remain one authority for this state.

Do not duplicate these flags inside:

```text
HudController
ProductionTelemetryController
GameSessionController
```

The three controllers may expose operations/queries needed by lifecycle orchestration, but they must not independently own the application suspend/resume state machine.

---

## 5. `HandleSystemSuspend()` contract

Preserve the current effective order exactly:

```text
if already suspended:
    return

suspended_ = true

CancelResumeRecovery()

HudController::HideForSuspend()

PauseProductionSamplingForSuspend()
    -> ProductionTelemetryController::StopSamplingTimersAndFps()
    -> GameSessionController::StopRenderVerification("suspend", false)
    -> ProductionTelemetryController::StopGraphicsApiProbe()
    -> ProductionTelemetryController::ResetSamplingState("suspend")

GameSessionController::DiscardPendingSuspendEvents()

log "System suspend detected"
```

Do not move the hide after telemetry shutdown.

Do not change verifier/FPS reset semantics during suspend.

Do not change pending-event discard coverage.

---

## 6. `HandleSystemResume()` contract

Preserve:

```text
if ResumeRecoveryShouldStart(resumeRecoveryActive_) == false:
    return
```

### Missed-suspend fallback

If:

```text
ResumeRecoveryNeedsSuspendFallback(suspended_)
```

preserve this sequence:

```text
HudController::HideForResumeFallback()
PauseProductionSamplingForSuspend()
GameSessionController::DiscardPendingSuspendEvents()
log "Suspend notification was missed; resume fallback prepared"
```

Then preserve:

```text
suspended_ = false
resumeRecoveryActive_ = true
resumeRecoveryAttempts_ = 0

SetTimer(
    tray message window,
    kResumeRecoveryTimerId,
    kResumeRecoveryIntervalMs,
    nullptr)

log "System resume detected"
log "HUD resume recovery started"
```

Do not add new retry timers or backoff policy.

---

## 7. `TryResumeRecovery()` contract

This method remains in `App` because it is intentionally a cross-controller recovery flow.

Preserve the current order.

### 7.1 Entry and foreground refresh

```text
if !resumeRecoveryActive_:
    return

++resumeRecoveryAttempts_

GameSessionController::ReconcileForeground()
```

Then derive:

```text
tracked process PID
process liveness
verifier-retention decision
foreground-is-tracked state
HUD enabled state
manual HUD override
HUD visibility mode
```

Do not move these decisions into `GameSessionController` or `HudController`.

---

## 8. Expected-visible calculation

Preserve current semantics:

```text
HUD must be enabled
AND

manual override, when present, is authoritative
OTHERWISE

Always mode
OR
tracked game is foreground
```

If the current expression redundantly queries `ForegroundIsTrackedProcess()` more than once, it is acceptable to simplify it to the already-captured local boolean **only if the resulting boolean expression is exactly equivalent**.

Example acceptable cleanup:

```cpp
const bool rendererForegroundActive =
    gameSession_.ForegroundIsTrackedProcess();

const bool expectedVisible = hudEnabled &&
    (manualOverride.has_value()
        ? *manualOverride
        : visibilityMode == clawhud::HudVisibilityMode::Always ||
            rendererForegroundActive);
```

This is optional.

Do not otherwise change HUD visibility policy in R5.

---

## 9. Foreground wait policy

Preserve:

```text
visibilityUsesForeground =
    no manual override
    AND visibilityMode == InGameOnly
```

Before evaluating the wait decision, keep:

```text
GameSessionController::DiscardPendingRenderVerifierEvents()
```

Then preserve:

```text
ResumeRecoveryShouldWaitForForeground(...)
```

If it returns true:

```text
re-arm the same 500 ms timer
return
```

No polling loop.

No `Sleep`.

No new worker thread.

The existing application timer remains the bounded recovery driver.

---

## 10. Graphics API probe during recovery

Preserve:

```text
tracked process alive
    -> ProductionTelemetryController::StartGraphicsApiProbe(pid)

otherwise
    -> ProductionTelemetryController::StopGraphicsApiProbe()
```

Do not move graphics-probe ownership to `App` or `GameSessionController`.

Do not change the existing probe retry policy.

---

## 11. Fresh-frame recovery is mandatory

The HUD must not simply be shown because the process returned.

Preserve the fresh-frame gate:

```text
expectedVisible == false
    -> no fresh frame required

expectedVisible == true
    -> HudController presentation must exist
    -> RenderRecoveryFrame()
    -> ResumeRecoveryFrameWasPresented(result)
```

On the first recovery attempt only, preserve the current recreation behavior for a non-`S_FALSE` render failure:

```text
if fresh frame not ready
AND render result != S_FALSE
AND attempt == 1
    -> HudController::Recreate(false)
```

Do not weaken this into an unconditional `Show()`.

---

## 12. Retry/exhaustion policy

Preserve:

```text
ResumeRecoveryMayShowHud(expectedVisible, freshFrameReady)
```

If the HUD may not yet be restored:

```text
resumeRecoveryActive_ = true
```

Then:

```text
if no attempts remaining:
    CancelResumeRecovery()
    warn "HUD resume recovery exhausted"
    return
```

Otherwise:

```text
SetTimer(... same 500 ms interval ...)
return
```

Do not increase attempts.

Do not increase timer frequency.

Do not add indefinite retries.

---

## 13. Visibility restore ordering

When the fresh-frame gate succeeds, preserve:

```text
resumeRecoveryActive_ = false

ReconcileHudVisibility()
```

Then preserve the first-attempt fallback:

```text
if expected visible
AND HUD still not visible
AND attempt == 1
    -> HudController::Recreate(true)
    -> ReconcileHudVisibility() again
```

Do not move this recreation into `HudController::ReconcileVisibility()`.

The decision to retry/recreate is lifecycle orchestration, not HUD presentation policy.

---

## 14. Successful recovery behavior

Preserve:

```text
recovered = !expectedVisible || HudVisible()
```

On recovery:

```text
log verifier.resume-retained when the existing verifier was retained
OR
log verifier.resume-restarted when a matching running verifier exists after recovery

capture completed attempt count
CancelResumeRecovery()

if ShouldReevaluateForegroundAfterResume(hudEnabled, recovered):
    GameSessionController::ReevaluateForeground()

log "HUD resume recovery completed attempt=N"
return
```

Do not restart the full game-detection session merely because of resume.

Do not clear a live committed game target solely because of suspend/resume.

---

## 15. `CancelResumeRecovery()` contract

Keep it simple and App-owned:

```text
KillTimer(tray message HWND, kResumeRecoveryTimerId)
resumeRecoveryActive_ = false
resumeRecoveryAttempts_ = 0
```

Do not add a controller merely to own this three-line state reset.

---

## 16. No lifecycle-controller extraction

The following is explicitly out of scope:

```cpp
class RuntimeLifecycleController;
class SuspendResumeController;
class SuspendResumeCoordinator;
```

Do not introduce a callback surface like:

```text
LifecycleHooks
HudLifecycleAdapter
TelemetryLifecycleAdapter
GameSessionLifecycleAdapter
```

The current `App` already is the correct composition root/mediator for these cross-domain calls.

Adding another coordinator would only move the same coupling one level sideways.

---

## 17. Controller boundaries must remain clean

### `HudController`

Owns:

```text
presentation lifecycle
HUD state/options
show/hide/recreate/render
```

Must not own:

```text
suspended_
resume attempts
resume timer
process liveness
GameSession state
```

### `ProductionTelemetryController`

Owns:

```text
sampling lifecycle
EC/system/battery/FPS state
graphics API probe
```

Must not own application suspend/resume policy.

### `GameSessionController`

Owns:

```text
foreground tracking
game candidate/session state
renderer verifier
process lifetime
Steam session context
```

Must not own application suspend/resume retry state.

---

## 18. HUD Presentation / VRR Contract — HARD STOP

R5 must not modify or work around:

```text
HudPresentation windowExStyle
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
premultiplied-alpha contract
```

Expected normal diff:

```text
HudPresentation.cpp                  ZERO DIFF
HudPresentation.h                    ZERO DIFF
HudPresentationContract.*            ZERO DIFF
HudPresentationLifecycle.*           ZERO DIFF
HudRenderer.*                        ZERO DIFF
```

If lifecycle cleanup appears to require changing one of these invariants:

```text
STOP.
```

Report the conflict instead of changing the presentation contract.

---

## 19. Background opacity remains background-only

R5 has no opacity work.

Do not change whole-window or whole-visual opacity.

Do not touch renderer alpha behavior as part of suspend/resume cleanup.

---

## 20. Game-detection contract unchanged

Suspend/resume work must not alter:

```text
Idle / Armed / Verifying / Ready / Committed
candidate precedence
generation isolation
Steam AppID semantics
Microsoft identity semantics
FirstDisplayedFrame renderer proof
final foreground commit rule
Alt+Tab committed retention
process-lifetime authoritative release
```

Resume re-evaluation may call the existing `GameSessionController::ReevaluateForeground()` only.

Do not reset the coordinator on normal resume.

---

## 21. PresentMon contract unchanged

Preserve one shared App-owned:

```text
PresentMonTelemetryProvider
```

No additional API2 client/session may be created by R5.

The verifier remains owned by `GameSessionController` and the FPS/system telemetry remains owned by `ProductionTelemetryController`.

---

## 22. Tray-only / lazy Settings UI contract

R5 must not alter startup UI residency.

Preserve:

```text
Windows startup
    -> tray/message HWND + background runtime
    -> NO SettingsWindow construction
```

`SettingsWindow` remains lazy-created only by `App::OpenSettings()`.

Closing Settings must still release it through `SettingsDestroyed()` / `settings_.reset()`.

No suspend/resume code may instantiate Settings UI.

---

## 23. Allowed runtime cleanup

Only behavior-equivalent, suspend/resume-local cleanup is allowed.

Examples:

```text
reuse an already-captured foreground-match boolean instead of querying it twice
add a concise comment documenting why lifecycle state intentionally remains in App
remove an obviously redundant local expression where equality is provable
```

Do not mix:

```text
shutdown centralization
TrayIcon timer-dispatch refactor
DebugObservationController extraction
HudSettingsStore rename
App facade cleanup
CMake cleanup
game-detection changes
telemetry changes
HUD style changes
```

Those belong to later phases.

---

## 24. Expected code diff

R5 may legitimately have **no runtime architecture diff**.

Expected files are approximately:

```text
docs/APP_REFACTOR_PLAN.md

optional:
src/ClawHUD/App.cpp
```

Do not create new `.h/.cpp` lifecycle-controller files.

If `App.cpp` changes, keep the diff small and mechanically behavior-equivalent.

---

## 25. Behavior inventory

Before changing `App.cpp`, record the current ordering for:

```text
HandleSystemSuspend
HandleSystemResume
TryResumeRecovery
CancelResumeRecovery
PauseProductionSamplingForSuspend
```

If the runtime diff is limited to an exact boolean-expression simplification, a concise PR behavior inventory is sufficient.

If any call ordering changes, explain why and prove behavior equivalence before merge.

Prefer no ordering change.

---

## 26. Tests

If runtime code changes, run at minimum:

```text
SuspendResumeRecoveryTests
HudPresentationContractTests
HudPresentationLifecycleTests
ProductionGameDetectionScenarioTests
GameRenderVerifierTests
ProductionTargetPolicyTests
AlwaysModeFpsTargetTests
FpsStaleHoldTests
```

Then run the full active CTest suite.

Current baseline after R4:

```text
46/46
```

Do not delete or weaken tests.

If R5 is docs-only, no new runtime test target is required.

---

## 27. Build verification

For any runtime code change:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Using the repository's established Ninja/BuildTools command is also acceptable.

Required result:

```text
Release build succeeds
all active CTest tests pass
```

---

## 28. Hardware validation policy

Hardware smoke remains useful follow-up validation but is **not a merge blocker by itself**.

A PR may record:

```text
Hardware smoke: deferred / not performed
```

Do not mark the PR blocked solely because supported MSI Claw hardware is unavailable.

Actual code defects, test failures, lifecycle-order regressions, or HUD presentation contract violations remain blockers.

---

## 29. Documentation update required in the R5 implementation PR

Update:

```text
docs/APP_REFACTOR_PLAN.md
```

Mark R5 complete with the explicit architectural result:

```text
R5 decision: no RuntimeLifecycleController / SuspendResumeCoordinator created.
Suspend/resume remains top-level App orchestration.
R1 SuspendResumePolicy remains the pure decision layer.
Post-R4 controller APIs make the current flow sufficiently explicit.
```

Record:

```text
PR number
head / merge commit
runtime files changed, if any
CTest result when runtime code changed
hardware smoke status (informational only)
```

Then set next work to:

```text
R6 — Optional DebugObservationController
```

Do not implement R6 in the same PR.

---

## 30. Expected end state after R5

```text
App
│
├─ HudController
├─ ProductionTelemetryController
├─ GameSessionController
├─ PresentMonTelemetryProvider
│
├─ suspend/resume top-level state
│   ├─ suspended_
│   ├─ resumeRecoveryActive_
│   └─ resumeRecoveryAttempts_
│
├─ HandleSystemSuspend
├─ HandleSystemResume
├─ TryResumeRecovery
├─ CancelResumeRecovery
│
├─ debug observation sources   # R6 optional
├─ SettingsWindow (lazy)
├─ TrayIcon
└─ TweakStartupCoordinator
```

Pure lifecycle decisions remain:

```text
SuspendResumePolicy.h
```

This is the intended architecture, not an unfinished extraction.

---

# Acceptance Criteria

R5 is complete when:

1. the current post-R4 suspend/resume flow has been explicitly re-evaluated;
2. no `RuntimeLifecycleController` / `SuspendResumeCoordinator` is introduced;
3. `suspended_` remains App-owned;
4. `resumeRecoveryActive_` remains App-owned;
5. `resumeRecoveryAttempts_` remains App-owned;
6. `SuspendResumePolicy.h` remains the pure decision layer;
7. the 500 ms retry cadence is unchanged;
8. the six-attempt maximum is unchanged;
9. missed-suspend fallback ordering is unchanged;
10. suspend hide/pause/discard ordering is unchanged;
11. fresh-frame-before-visible recovery remains mandatory;
12. first-attempt recreation semantics are unchanged;
13. retry exhaustion semantics are unchanged;
14. committed game state is not reset solely because of suspend/resume;
15. game detection remains event-driven;
16. no new PresentMon provider/session is created;
17. HudController remains the presentation owner;
18. ProductionTelemetryController remains telemetry/sampling owner;
19. GameSessionController remains game-session owner;
20. HUD presentation/VRR contract is unchanged;
21. tray-only startup still does not instantiate Settings UI;
22. any runtime diff is small and provably behavior-equivalent;
23. full Release build + CTest pass if runtime code changed;
24. hardware smoke may be deferred and is not itself a merge blocker;
25. `APP_REFACTOR_PLAN.md` records the no-controller R5 decision;
26. next work is R6, not mixed into R5.
