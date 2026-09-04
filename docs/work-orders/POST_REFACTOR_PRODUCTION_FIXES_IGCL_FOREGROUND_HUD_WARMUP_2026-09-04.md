# Post-refactor production fixes — IGCL removal, foreground reconciliation, first-visible HUD presentation warm-up

**Status:** implementation work order  
**Date:** 2026-09-04  
**Target repository:** `onehoon/ClawHUD`  
**Baseline:** `main` @ `841a073a7046aec5ba738fdd5fbc505682ddbc96` (PR #231)  
**Delivery:** **one PR** containing all three parts below

---

## 1. Goal

Implement three production fixes discovered during post-refactor field validation:

1. **Remove the remaining production IGCL Graphics API probe path.**
   Graphics API identification is not reliable enough for product use and the HUD no longer displays the API label. The current production runtime still loads `ControlLib.dll`, probes the foreground game, retries, logs failures, and carries `graphicsApi` through the telemetry snapshot even though `FormatHud()` no longer renders it.

2. **Repair missed foreground authority updates without polling.**
   The current split between `ForegroundTracker` and `ProductionGameWindowSource` can leave the canonical foreground PID stale when Windows changes the real foreground during a window lifecycle transition without delivering a usable `EVENT_SYSTEM_FOREGROUND` notification. This affects both game detection and Always-mode FPS targeting.

3. **Add a process-lifetime, one-shot HUD presentation warm-up after the first real visible Present.**
   Field behavior indicates the first Presentation API / DirectComposition HUD instance after process launch can enter a state where Edge/Steam visually cover the HUD even though the HUD HWND remains visible and topmost. Completely restarting ClawHUD clears the behavior. As a pragmatic field workaround, after the first presentation has actually been shown and has successfully submitted one frame, tear it down once and recreate it through the exact same production presentation path.

This is intentionally one PR: all three issues were exposed by the same post-refactor field run and the IGCL portion is primarily deletion, so review/field validation is easier as one production-cleanup/fix changeset.

---

## 2. Field evidence that drives this work

### 2.1 IGCL is still executing although the HUD no longer displays Graphics API

Current `main` still contains:

- `src/ClawHUD/IntelGraphicsApiProbe.{h,cpp}`;
- `ProductionTelemetryController::StartGraphicsApiProbe` / `EnsureGraphicsApiProbe` / `StopGraphicsApiProbe` / `TryGraphicsApiProbe` and associated target/retry state;
- timer id `kGraphicsApiRetryTimerId = 4`;
- `GameSessionHooks::startGraphicsApiProbe` / `stopGraphicsApiProbeIfTarget`;
- App hook wiring and timer dispatch;
- `HudTelemetrySnapshot::graphicsApi`;
- `ProductionTelemetryController::FillSnapshot()` assigning `latestGraphicsApi_`;
- CMake/test entries for `IntelGraphicsApiProbe`.

But current `HudModel::FormatHud()` renders FPS as the Graphics segment and does **not** consume `snapshot.graphicsApi`.

The field log therefore contains real production work such as:

```text
IGCL Graphics API live state ...
IGCL Graphics API unresolved after bounded retries
Graphics API resolved api=DX12
```

with no corresponding user-visible product value.

This is dead production behavior and must be removed, not merely silenced.

### 2.2 Mafia: real game window existed, but foreground/game evaluation arrived ~80 s late

Observed field sequence for `MafiaTheOldCountry.exe`, PID `14312`:

```text
19:01:39.412 CREATE UnrealWindow visible=0
19:01:39.432 SHOW   UnrealWindow visible=1
19:01:44.874 NAMECHANGE -> title "마피아: 올드 컨트리"
                         rect=0,0,1920,1200
```

The production detector did not evaluate PID `14312` at that transition. PresentMon Always-mode sampling remained on Explorer PID `8856` until approximately `19:02:59`.

Once an eventual foreground transition was observed:

```text
foreground.evaluate ... pid=14312
renderer.first-frame pid=14312
foreground.target-set pid=14312
PresentMonFPS pid=14312 displayed≈90 presented≈90
```

Therefore:

- PresentMon API2 works for the game;
- renderer verification works;
- the failure is before measurement, in foreground/window-event authority;
- the decisive lifecycle event in this run was `EVENT_OBJECT_NAMECHANGE`, which production currently does not subscribe to.

### 2.3 American Truck Simulator: game detection recovered, Always FPS authority stayed on a stale Ghost PID

Observed field sequence:

```text
Windows Ghost foreground pid=1988
...
Ghost HIDE
real ATS window SHOW pid=15172
HudWindowState foregroundPid=15172
GameDetection eventually targets pid=15172
```

However Always-mode FPS continued sampling PID `1988` for a significant interval.

This demonstrates the architectural gap:

- `GameSessionController` can re-read `GetForegroundWindow()` from production window events;
- `ProductionTelemetryController::OnForegroundProcessChanged()` is only driven by the `ForegroundTracker` callback;
- `ForegroundTracker` itself only wakes from `EVENT_SYSTEM_FOREGROUND` today;
- therefore game detection and Always FPS can disagree about the current foreground.

### 2.4 Edge/Steam HUD disappearance is not a normal HWND visibility/topmost failure

At the reported disappearance, debug state showed:

```text
logicalVisible=1
isWindowVisible=1
isIconic=0
exTopmost=1
```

for the HUD. There was no `hide-applied` transition.

The Edge and Steam windows in the captured log were ordinary maximized/work-area windows, not a special exclusive-fullscreen ownership case.

The relevant field observation is:

> after completely exiting ClawHUD from the tray and restarting it, the same Edge maximize behavior no longer reproduced.

The objective of Part C is therefore not to claim a proven root cause. It is a bounded, one-shot production workaround that reproduces the effective part of the manual recovery: **allow the first production presentation to become real/visible and submit a frame, then recreate the presentation once.**

---

## 3. Non-negotiable HUD / VRR safety boundary

Part C MUST NOT modify, replace, weaken, bypass, or compensate around any existing production HUD presentation invariant.

Do **not** change:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- current `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()` / `kHudPresentationContract` values;
- independent-flip requirement;
- Presentation API production path;
- DirectComposition production path;
- premultiplied-alpha contract;
- presentation buffer format/count/resource flags;
- click-through/no-activation/topmost semantics.

Do not add a topmost watchdog, repeated `SetWindowPos` recovery loop, periodic presentation recreation, DWM polling, alternate swap-chain backend, or window-wide workaround.

The warm-up must use the **same `HudPresentation` implementation and the same production contract twice**.

No opacity behavior is part of this PR.

---

# Part A — remove the remaining production IGCL Graphics API path

## 4. Product decision

Graphics API (`DX11` / `DX12` / `Vulkan`) detection through IGCL is intentionally retired from the production HUD because it is not reliable enough to identify the active API consistently.

The HUD already omits this value from `FormatHud()`. Production must now stop doing the probe as well.

This is separate from:

- PresentMon API2 system/process telemetry;
- Intel VRR Range Fix;
- Intel panel detection used by the VRR tweak;
- archived diagnostic/research source under `archive/`.

Do not remove or alter those unrelated Intel paths.

## 5. Required production deletion

### 5.1 Delete the production probe implementation

Delete:

```text
src/ClawHUD/IntelGraphicsApiProbe.h
src/ClawHUD/IntelGraphicsApiProbe.cpp
```

Delete its dedicated test source if it exists on the current branch:

```text
tests/IntelGraphicsApiProbeTests.cpp
```

Remove the corresponding test target from `cmake/ClawHUDTests.cmake`.

Remove `src/ClawHUD/IntelGraphicsApiProbe.cpp` from the `ClawHUD` executable in `CMakeLists.txt`.

Do **not** delete archived IGCL research solely because production stops using it.

### 5.2 Simplify `ProductionTelemetryController`

In `ProductionTelemetryController.h` remove:

```cpp
#include "IntelGraphicsApiProbe.h"
```

Remove:

```cpp
kGraphicsApiRetryTimerId
```

Remove the entire graphics-API public surface:

```cpp
StartGraphicsApiProbe(...)
EnsureGraphicsApiProbe(...)
StopGraphicsApiProbe()
StopGraphicsApiProbeIfTarget(...)
ReconcileGraphicsApiTargetLiveness()
TryGraphicsApiProbe()
GraphicsApiProcessId()
```

Remove state:

```cpp
IntelGraphicsApiProbe graphicsApiProbe_;
std::optional<std::wstring> latestGraphicsApi_;
DWORD graphicsApiProcessId_{};
unsigned graphicsApiAttempts_{};
```

Update class comments from:

```text
EC / system / battery / FPS / graphics-API
```

to the actual remaining production ownership.

In `ProductionTelemetryController.cpp`:

- remove graphics-API retry constants;
- remove `snapshot.graphicsApi = latestGraphicsApi_` from `FillSnapshot()`;
- remove `ReconcileGraphicsApiTargetLiveness()` from `SampleSystemEc()`;
- delete all graphics-API probe/retry/liveness methods;
- remove any associated log strings.

### 5.3 Remove GameSession cross-domain probe hooks

From `GameSessionHooks` delete:

```cpp
std::function<void(DWORD)> startGraphicsApiProbe;
std::function<void(DWORD)> stopGraphicsApiProbeIfTarget;
```

From `GameSessionController.cpp` remove all calls to those hooks, including the current target set/clear/reset paths.

The target-set sequence after removal should remain conceptually:

```text
currentForegroundGameProcess_ = current.process
-> setInGameForegroundProcess(pid)
-> startProductionSampling()
-> foreground.target-set log
-> reconcileHudVisibility()
```

Do not disturb the established FPS/game-target ownership semantics while removing IGCL.

### 5.4 Remove App-level wiring

From `App::MakeGameSessionHooks()` remove graphics-probe hook assignments.

Remove every App call such as:

```cpp
productionTelemetry_.StartGraphicsApiProbe(...)
productionTelemetry_.StopGraphicsApiProbe()
productionTelemetry_.StopGraphicsApiProbeIfTarget(...)
productionTelemetry_.EnsureGraphicsApiProbe(...)
productionTelemetry_.TryGraphicsApiProbe()
```

Known current sites include, but are not limited to:

- runtime shutdown;
- HUD stop/disable;
- resume recovery;
- timer dispatch for `kGraphicsApiRetryTimerId`;
- game-session hook construction.

Search the complete tree after implementation; no production reference may remain.

### 5.5 Remove the unused HUD snapshot field

From `HudTelemetrySnapshot` remove:

```cpp
std::optional<std::wstring> graphicsApi;
```

Update sample factories and tests that still assign it, including `MakeGameDcSample()` / no-game sample setup and old formatter test fixtures.

Do **not** remove the Graphics HUD segment itself: FPS still uses `HudSegmentKind::Graphics`.

### 5.6 Repository-wide IGCL cleanup rule

After the change:

- production `src/ClawHUD` must contain no Graphics API IGCL probe code;
- no `ControlLib.dll` load may occur for this retired probe;
- no `IGCL Graphics API ...` runtime log may remain;
- no graphics-API retry timer may remain;
- archived diagnostic/research material may remain;
- attribution in `THIRD-PARTY-NOTICES.md` must only be removed if it is genuinely no longer required by any source retained in the repository. Do not remove attribution that is still required by archived source.

---

# Part B — repair foreground authority using existing event-driven sources

## 6. Design rule

Do not add polling.

Do not create a second foreground tracker.

Do not create a second foreground PID cache in `ProductionTelemetryController` or `GameSessionController`.

Use the existing source split:

```text
EVENT_SYSTEM_FOREGROUND
        |
        v
ForegroundTracker::Reconcile()
        |
        +-> App hook -> ProductionTelemetryController::OnForegroundProcessChanged()
        |
        +-> GameSessionController::HandleProductionForegroundChanged()

Production object events
        |
        v
ProductionGameWindowSource
        |
        v
GameSessionController::HandleProductionWindowEvent()
```

The fix is to let production object events also **wake the existing foreground reconciliation authority**.

`ForegroundTracker::Reconcile()` is already cheap and stateful: it re-reads `GetForegroundWindow()` / PID and emits its callback only when `(HWND, PID)` differs from its cached last foreground.

## 7. Add `EVENT_OBJECT_NAMECHANGE` to production window observation

Current `ProductionGameWindowSource` observes five event types:

```text
CREATE
SHOW
HIDE
LOCATIONCHANGE
DESTROY
```

Add:

```text
EVENT_OBJECT_NAMECHANGE
```

Required changes:

- add `ProductionWindowEventType::NameChange`;
- map `EVENT_OBJECT_NAMECHANGE` in `MapProductionWindowEvent()`;
- increase the observed-event array and hook storage from 5 to 6;
- keep the existing `OBJID_WINDOW` + `CHILDID_SELF` filter;
- keep the existing `GA_ROOT` top-level filter;
- keep the bounded queue and worker design unchanged.

`NAMECHANGE` is **not game evidence by itself**. It is only another lifecycle/reconciliation trigger.

The existing foreground-first detector still decides whether the current foreground is excluded, hidden, needs renderer verification, or eligible.

## 8. Reconcile canonical foreground on production window events

In `GameSessionController::HandleProductionWindowEvent()` preserve the existing Microsoft evidence handling, then invoke the existing foreground tracker reconciliation before/alongside the current same-screen evaluation path:

```cpp
foregroundTracker_.Reconcile();
```

The key property is that the current `ForegroundTracker` callback already does both required downstream actions:

```text
hooks_.onForegroundChanged(window, processId)
    -> ProductionTelemetryController::OnForegroundProcessChanged(pid)
    -> ReconcileHudVisibility()
    -> debug foreground observation

if HUD enabled:
    HandleProductionForegroundChanged(window, processId)
    -> EvaluateCurrentForeground("foreground")
```

Therefore this one reconciliation call closes the ATS split without introducing new ownership.

### Expected ATS recovery

```text
old tracked foreground = Ghost pid 1988
real game SHOW/HIDE lifecycle event arrives
-> foregroundTracker_.Reconcile()
-> GetForegroundWindow() == real game pid 15172
-> callback fires because HWND/PID changed
-> Always FPS target immediately becomes 15172
-> game detection evaluates the same actual foreground
```

### Expected Mafia recovery

If the `SHOW` event occurs before the game becomes authoritative foreground, it may legitimately do nothing.

Later:

```text
NAMECHANGE on Mafia top-level window
-> foregroundTracker_.Reconcile()
-> if actual foreground has changed, callback repairs canonical PID
-> WindowEventAffectsCurrentForeground(NameChange) is also allowed to
   re-evaluate the current foreground/current detector window
-> full-screen/title transition becomes visible to foreground-first admission
```

The important point is that `NAMECHANGE` does not directly set a target. Every target still comes from a fresh `GetForegroundWindow()` observation and normal detector evaluation.

## 9. Preserve current screen-affect policy

`WindowEventAffectsCurrentScreen()` currently rejects `Create` and accepts other events only when the event belongs to either:

- the live foreground `(HWND, PID)`; or
- the current detector `(HWND, PID)`.

Keep that principle.

Adding `NameChange` must not allow a background window to steal foreground authority.

The PR #230 optimization for redundant `ExcludedExecutable` `LOCATIONCHANGE` must stay exactly scoped to `LOCATIONCHANGE`. Do not broaden it to `NameChange`.

## 10. No new polling or timers

Explicitly prohibited for this fix:

- periodic `GetForegroundWindow()` polling;
- process scanning;
- WMI process lifecycle resurrection;
- ETW solely for this issue;
- retry state machines;
- scheduler-dependent recovery loops.

The existing production window events are frequent enough to provide the missing wake-up path in the demonstrated real-world sequences.

---

# Part C — process-lifetime first-visible presentation warm-up/recreate

## 11. Required behavior

Warm up the HUD presentation **once per ClawHUD process**, at the first time that a production HUD instance is truly visible and has successfully submitted a real frame.

The process-lifetime rule is intentional:

- do not repeat per game;
- do not repeat after a game exits and another game starts;
- do not repeat because the same presentation is hidden/shown;
- do not repeat after font/size/background-mode recreations;
- do not repeat after HUD Disable -> Enable later in the same process;
- process restart naturally resets the one-shot state.

### Always mode

Expected flow:

```text
process starts
-> HUD Ensure
-> normal first Show
-> normal first Render/Present succeeds
-> one-shot warm-up is consumed
-> recreate presentation once
-> restore/render/show normal HUD
-> never warm up again in this process
```

### In-Game Only mode

Do **not** make a hidden In-Game Only HUD visible at startup merely to warm it up.

Expected flow:

```text
process starts
-> presentation may exist but HUD remains hidden
-> no warm-up yet
-> first eligible game makes HUD visible
-> first visible Render/Present succeeds
-> one-shot warm-up is consumed
-> recreate presentation once
-> restore/render/show normal HUD
-> later games reuse the stabilized presentation with no extra warm-up
```

If F8/manual override causes the first real HUD visibility before a game, that real visible Present may consume the same process-lifetime one-shot. Do not create a separate F8 warm-up path.

## 12. Trigger on a real successful Present, not on `Initialize()` or `Show()` alone

Do **not** warm up immediately after `HudPresentation::Initialize()`.

Do **not** warm up merely because `Show()` succeeded.

The first instance must first complete the normal visible presentation path.

`HudPresentation::Render()` returns the result of `presentationManager_->Present()` after buffer acquisition/drawing. It can also return `S_FALSE` when no buffer is available.

Therefore the warm-up trigger must require all of:

```text
presentation exists
presentation is logically Visible()
first-visible warm-up has not already been attempted
HudPresentation::Render(...) returned S_OK
```

`S_FALSE` does not count as the first successful Present.

A failed Render does not consume the one-shot before any frame has been successfully submitted.

## 13. Keep one-shot ownership in `HudController`

`HudController` already owns:

- the concrete `HudPresentation` object;
- every Initialize/Render/Show/Hide/Shutdown call site;
- presentation recreate behavior;
- presentation failure log latches.

The one-shot state belongs here, not in `App`, game detection, telemetry, or `HudPresentationContract`.

Add process-lifetime state such as:

```cpp
bool firstVisiblePresentationWarmupAttempted_{};
```

Do not reset this member from:

- `Recreate()`;
- `ShutdownPresentation()`;
- setting changes;
- game target clear;
- `MarkDisabled()`;
- F8 hide/show.

It resets only because a new `HudController` is constructed for a new process.

## 14. Reuse the existing recreate path

Prefer the existing `HudController::Recreate(bool restoreVisible)` path instead of duplicating `HudPresentation` initialization internals.

A minimal intended sequence inside/after the first successful visible `HudController::Render()` is:

```text
first presentation is already Show() == visible
-> Render(snapshot) returns S_OK (real Present submitted)
-> mark firstVisiblePresentationWarmupAttempted_ = true BEFORE any recursive render path
-> Recreate(restoreVisible = true)
     -> old HudPresentation::Shutdown()
     -> same HudPresentation::Initialize() production path
     -> existing requestRender(true) supplies a fresh/current snapshot while hidden
     -> Show() restores visibility
-> warm-up complete
```

Mark the one-shot **before** calling `Recreate()` because `Recreate()` invokes the existing `requestRender_(true)` callback. That callback re-enters `HudController::Render()`; the guard must already prevent recursive warm-up.

Do not add a second presentation backend or a special warm-up renderer.

Do not add `Sleep()`.

Do not wait on a blocking presentation fence.

For this first field workaround, a successful visible `Present()` return is the boundary. If hardware testing later proves that the recreation must be deferred to a later message turn, handle that in a follow-up with evidence rather than adding arbitrary delay here.

## 15. Warm-up failure behavior

The workaround must never enter a recreate loop.

Rules:

1. mark the one-shot before attempting recreation;
2. if warm-up recreation fails, log once with a distinct warm-up reason;
3. do not automatically warm up again on the next render/game;
4. preserve the existing `Recreate()` failure semantics rather than introducing a new retry state machine;
5. no process termination solely because this workaround failed.

Suggested logs:

```text
[HudWarmup] first-visible present complete; recreating presentation
[HudWarmup] presentation recreation complete
```

Failure:

```text
[HudWarmup] presentation recreation failed hr=...
```

Keep logs concise; do not log every frame.

## 16. Do not change normal game-to-game lifecycle

Example after warm-up has completed:

```text
Game A detected
-> HUD shown
-> warm-up already consumed

Game A exits
-> HUD hidden (In-Game Only) / remains as normal (Always)

Game B detected
-> existing normal presentation show/use
-> NO warm-up recreation
```

Only a new `ClawHUD.exe` process receives a new one-shot allowance.

---

# 17. Expected files

The exact diff may vary slightly, but expect the PR to touch roughly these areas.

### Part A

```text
CMakeLists.txt
cmake/ClawHUDTests.cmake
src/ClawHUD/App.cpp
src/ClawHUD/ProductionTelemetryController.h
src/ClawHUD/ProductionTelemetryController.cpp
src/ClawHUD/GameDetection/GameSessionController.h
src/ClawHUD/GameDetection/GameSessionController.cpp
src/ClawHUD/HudModel.h
src/ClawHUD/HudModel.cpp
tests/HudModelTests.cpp
src/ClawHUD/IntelGraphicsApiProbe.h                  DELETE
src/ClawHUD/IntelGraphicsApiProbe.cpp                DELETE
tests/IntelGraphicsApiProbeTests.cpp                 DELETE (if still present)
```

### Part B

```text
src/ClawHUD/GameDetection/ProductionGameWindowSource.h
src/ClawHUD/GameDetection/ProductionGameWindowSource.cpp
src/ClawHUD/GameDetection/GameSessionController.cpp
tests/ProductionGameWindowSourceTests.cpp
tests/GameSessionCutoverPolicyTests.cpp               as needed
```

### Part C

```text
src/ClawHUD/HudController.h
src/ClawHUD/HudController.cpp
src/ClawHUD/HudPresentationLifecycle.h/.cpp            only if a pure helper is useful
tests/HudPresentationLifecycleTests.cpp                or existing HUD lifecycle/model tests
```

Do not modify `HudPresentationContract.*`.

Avoid modifying `HudPresentation.cpp` unless implementation cannot be completed through the existing controller lifecycle. There should be no need to change its production contract or backend.

---

# 18. Required tests

## 18.1 IGCL removal

Repository search / compile requirements:

- no production include of `IntelGraphicsApiProbe.h`;
- no production source entry for `IntelGraphicsApiProbe.cpp`;
- no `kGraphicsApiRetryTimerId`;
- no graphics API probe methods/state;
- no `IGCL Graphics API` runtime log strings;
- no `HudTelemetrySnapshot::graphicsApi`;
- archived diagnostics are not counted as production references.

`FormatHud()` FPS behavior must remain unchanged.

## 18.2 Production window event mapping

Extend `ProductionGameWindowSourceTests`:

```text
EVENT_OBJECT_NAMECHANGE -> ProductionWindowEventType::NameChange
```

Keep unsupported events rejected.

Keep queue ordering/capacity behavior unchanged.

## 18.3 Foreground cutover policy

Add/retain coverage showing:

- `Create` still does not directly affect the current screen;
- `NameChange` on the live foreground/current detector window is eligible to trigger reevaluation;
- `NameChange` on an unrelated background `(HWND, PID)` does not become current-screen authority;
- PR #230 excluded-foreground `LOCATIONCHANGE` suppression still applies only to `LOCATIONCHANGE`.

Do not add a large test-only dependency injection framework merely to fake `GetForegroundWindow()`.

## 18.4 Foreground authority field scenarios

Hardware/debug-log validation is required for the exact real-world gap:

### ATS-like Ghost -> real game

Verify:

```text
real GetForegroundWindow pid changes on SHOW/HIDE lifecycle
-> ForegroundTracker reconcile updates immediately
-> [PresentMonFPS] mode=Always foregroundPid=<real game pid>
-> no prolonged stale Ghost PID sampling
```

### Mafia-like delayed full-screen identity

Verify:

```text
SHOW/NAMECHANGE lifecycle occurs
-> game is evaluated without waiting ~80 s for a later task switch
-> renderer first-frame completes
-> target-set occurs
-> PresentMon FPS is obtained
```

## 18.5 HUD warm-up one-shot policy

Add a small pure policy helper/test if useful rather than trying to fake D3D/DComp.

Minimum cases:

```text
attempted=false, visible=true, Render=S_OK    -> warm-up yes
attempted=false, visible=true, Render=S_FALSE -> no
attempted=false, visible=false, Render=S_OK   -> no
attempted=true,  visible=true, Render=S_OK    -> no
failed Render                                  -> no
```

Also ensure the implementation sets the attempted flag before `Recreate()` can invoke `requestRender_(true)`.

## 18.6 Always mode hardware smoke

Fresh process:

1. launch with HUD enabled + Always;
2. observe normal first HUD appearance;
3. confirm exactly one `[HudWarmup]` recreation sequence;
4. maximize/restore Edge repeatedly;
5. maximize/restore Steam repeatedly;
6. confirm HUD remains visually present;
7. confirm no second warm-up occurs;
8. launch/exit multiple games and confirm no extra warm-up.

## 18.7 In-Game Only hardware smoke

Fresh process:

1. launch with HUD enabled + In-Game Only;
2. confirm no forced HUD appearance at app startup;
3. launch first game;
4. first real HUD visible Present triggers exactly one warm-up/recreate;
5. exit first game;
6. launch second game;
7. confirm no second warm-up;
8. confirm FPS targeting switches correctly.

## 18.8 HUD/VRR regression suite

All existing assertions/tests for the following must remain green and unchanged in meaning:

- click-through behavior;
- no activation;
- topmost behavior;
- transparent hit testing;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- independent flip;
- premultiplied alpha;
- production presentation contract.

No test should be weakened to accommodate this PR.

---

# 19. Build / validation

Run the normal repository validation from a clean tree.

At minimum:

```text
Native CMake Debug build
Native CMake Release build
ctest -E DiagWinEventTests   (Debug)
ctest -E DiagWinEventTests   (Release)
WPF Settings Release build/tests if shared files/project references are touched
```

The exact CTest total may decrease because the dedicated `IntelGraphicsApiProbeTests` target is deleted. Document the old -> new count in the PR.

No warning regressions.

---

# 20. Completion criteria

The PR is complete only when all of the following are true:

### IGCL

- production Graphics API IGCL probe is gone;
- no production Graphics API timer/retry/state remains;
- no unused `graphicsApi` snapshot field remains;
- PresentMon API2 telemetry/FPS remains functional;
- Intel VRR Range Fix remains untouched.

### Foreground authority

- production listens to top-level `NAMECHANGE`;
- production window events wake the existing `ForegroundTracker::Reconcile()`;
- Always FPS and game detection observe the same fresh foreground transition;
- background window events cannot steal authority;
- no polling was added.

### HUD presentation warm-up

- warm-up happens only after a visible `Render()` returns `S_OK`;
- exactly one warm-up recreation is attempted per process;
- Always naturally triggers it on first actual HUD display;
- In-Game Only naturally triggers it on the first actual in-game HUD display;
- a second game does not trigger another warm-up;
- existing `HudPresentation` production contract is byte/semantic unchanged;
- no watchdog, delay loop, alternate presentation backend, or z-order workaround is added.

### Field validation

- Edge/Steam maximize reproduction is re-tested after the warm-up change;
- Mafia/ATS-like foreground transitions are re-tested with debug logging;
- if Edge/Steam still reproduces, report that result rather than expanding the PR into speculative presentation changes.

---

# 21. PR structure / suggested title

One PR is preferred.

Suggested title:

```text
Remove retired IGCL probe and harden foreground/HUD startup recovery
```

Suggested commit organization is optional, but if multiple commits are used before squash they should follow the three logical parts:

```text
1. Remove retired production IGCL graphics API probe
2. Reconcile foreground authority from production window events
3. Warm up first visible HUD presentation once per process
```

Final merge should be squashable as one coherent post-refactor production-fix PR.
