# Post-refactor production fixes — IGCL removal, foreground reconciliation, first-visible HUD presentation warm-up

**Status:** implementation work order  
**Date:** 2026-09-04  
**Target repository:** `onehoon/ClawHUD`  
**Code baseline:** `main` @ `841a073a7046aec5ba738fdd5fbc505682ddbc96` (PR #231; later work-order-only commits do not change the code baseline)  
**Delivery:** **one PR** containing all three parts below

---

## 1. Goal

Implement three production fixes discovered during post-refactor field validation:

1. **Remove the remaining production IGCL Graphics API probe path.**
   Graphics API identification through IGCL is not reliable enough for product use and the HUD no longer displays the API label. The current runtime still loads `ControlLib.dll`, probes foreground games, retries, logs failures, and carries `graphicsApi` through the HUD snapshot for no user-visible benefit.

2. **Repair missed foreground authority updates without polling.**
   The current split between `ForegroundTracker` and `ProductionGameWindowSource` can leave Always-mode FPS authority stale even while `GameSessionController` has already re-read a newer `GetForegroundWindow()`. Production object events must also wake the existing canonical foreground reconciliation path.

3. **Add a process-lifetime, one-shot HUD presentation warm-up after the first real non-empty visible Present.**
   Field behavior indicates the first Presentation API / DirectComposition HUD instance after process launch can enter a state where Edge/Steam visually cover the HUD even though the HUD HWND remains visible and topmost. The workaround is deliberately bounded: let the first production presentation become visible and submit one real HUD frame, then recreate that presentation exactly once on the next message-pump turn through the same production contract/path.

This is intentionally one PR. Part A is mainly deletion, while Parts B/C are small post-refactor production fixes that should be validated together on hardware.

---

# 2. Verified field evidence

## 2.1 IGCL is still executing although Graphics API is no longer rendered

Current code still contains:

- `src/ClawHUD/IntelGraphicsApiProbe.{h,cpp}`;
- `ProductionTelemetryController` graphics-API target/retry/timer state;
- `kGraphicsApiRetryTimerId = 4`;
- `GameSessionHooks::startGraphicsApiProbe` / `stopGraphicsApiProbeIfTarget`;
- App hook/timer/resume/shutdown wiring;
- `HudTelemetrySnapshot::graphicsApi`;
- CMake/test entries for `IntelGraphicsApiProbe`.

`HudModel::FormatHud()` now renders FPS for the Graphics segment and does not use `snapshot.graphicsApi`.

Observed runtime work therefore includes:

```text
IGCL Graphics API live state ...
IGCL Graphics API unresolved after bounded retries
Graphics API resolved api=DX12
```

with no product output. This is dead production behavior and must be removed, not only silenced.

## 2.2 Mafia: the missing wake-up lasted ~79.7 s and ended only after a task switch

Observed `MafiaTheOldCountry.exe`, PID `14312`:

```text
19:01:39.412 CREATE UnrealWindow visible=0
19:01:39.432 SHOW   UnrealWindow visible=1
19:01:44.874 NAMECHANGE title="마피아: 올드 컨트리" rect=0,0,1920,1200
19:01:44.997 NAMECHANGE same top-level window
19:01:45.004 NAMECHANGE same top-level window
```

Production did not switch foreground/FPS authority to PID `14312` at those transitions. Always-mode FPS remained on Explorer PID `8856`.

The eventual correction was **not a fixed ~80 s fallback**. Immediately before the correction the log shows the Windows Task Switch UI (`title="작업 전환"`, `XamlExplorerHostIslandWindow`) and then fresh foreground events:

```text
19:02:58.876 Task Switch SHOW
19:02:59.059 foreground -> pid 0
19:02:59.073 Always foregroundPid=14312
19:02:59.087 foreground.evaluate pid=14312
```

Once authority reached the game, renderer verification and PresentMon API2 worked normally and FPS was ~90.

Conclusion: the ~79.7 s interval was time spent without a usable canonical foreground wake-up; the user's later task switch finally produced one. There is no evidence of a fixed 80-second fallback timer.

## 2.3 ATS: same authority gap, but only ~21.7 s — not ~80 s

Observed ATS sequence:

```text
18:59:50.536 Always foregroundPid=1988     // Windows Ghost
18:59:51.090 Ghost HIDE
18:59:51.092 real game SHOW pid=15172
18:59:52.118 HudWindowState foregroundPid=15172
```

At `18:59:52.118`, direct Win32 observation already reported the real game as foreground, yet Always-mode FPS continued sampling Ghost PID `1988` through `19:00:10.388`.

The canonical foreground tracker did not switch to PID `15172` until:

```text
19:00:13.772 Task Switch HIDE
19:00:13.800 Always foregroundPid=15172
```

That is roughly **21.7 s** after the log already showed `GetForegroundWindow()` on the real ATS window, not ~80 s.

This proves the architectural split:

- `GameSessionController` can re-read current Win32 foreground from production window events;
- `ProductionTelemetryController::OnForegroundProcessChanged()` is updated only through `ForegroundTracker`;
- `ForegroundTracker` currently wakes only from `EVENT_SYSTEM_FOREGROUND`;
- therefore game detection and Always FPS can temporarily disagree about the same actual screen.

## 2.4 Edge/Steam disappearance is not normal HWND hide/topmost loss

At the reported disappearance the HUD debug state remained:

```text
logicalVisible=1
isWindowVisible=1
isIconic=0
exTopmost=1
```

There was no `hide-applied` transition.

The field observation driving Part C is:

> after completely exiting ClawHUD from the tray and starting a new process, the same Edge maximize behavior no longer reproduced.

Part C is therefore an empirical one-shot workaround. It must not claim a proven DWM/Presentation API root cause.

---

# 3. Non-negotiable HUD / VRR safety boundary

Part C MUST NOT modify, replace, weaken, bypass, or compensate around any existing production HUD presentation invariant.

Do **not** change:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- existing `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()` / `kHudPresentationContract`;
- independent-flip requirement;
- Presentation API production path;
- DirectComposition production path;
- premultiplied-alpha contract;
- presentation buffer format/count/resource flags;
- click-through/no-activation/topmost semantics.

Do not add:

- a topmost watchdog;
- repeated `SetWindowPos` recovery;
- periodic presentation recreation;
- DWM polling;
- an alternate swap-chain backend;
- sleeps/delays used as a timing workaround;
- window-wide opacity changes.

The warm-up must use the **same `HudPresentation` implementation and the same production contract twice**.

---

# Part A — remove the remaining production IGCL Graphics API path

## 4. Product decision

Graphics API (`DX11` / `DX12` / `Vulkan`) detection through IGCL is retired from the production HUD because the result is not consistently reliable.

This decision does **not** remove or alter:

- PresentMon API2 system/process telemetry;
- PresentMon FPS/render verification;
- Intel VRR Range Fix;
- Intel panel detection used by the VRR tweak;
- archived IGCL research under `archive/diagnostics/igcl/`.

## 5. Required deletion

### 5.1 Delete the production probe implementation

Delete:

```text
src/ClawHUD/IntelGraphicsApiProbe.h
src/ClawHUD/IntelGraphicsApiProbe.cpp
tests/IntelGraphicsApiProbeTests.cpp
```

Remove:

- `src/ClawHUD/IntelGraphicsApiProbe.cpp` from `CMakeLists.txt`;
- the `ClawHUD.IntelGraphicsApiProbeTests` target from `cmake/ClawHUDTests.cmake`.

Do **not** delete `archive/diagnostics/igcl/`.

### 5.2 Simplify `ProductionTelemetryController`

Remove from `ProductionTelemetryController.h/.cpp`:

```text
#include "IntelGraphicsApiProbe.h"
kGraphicsApiRetryTimerId
StartGraphicsApiProbe
EnsureGraphicsApiProbe
StopGraphicsApiProbe
StopGraphicsApiProbeIfTarget
ReconcileGraphicsApiTargetLiveness
TryGraphicsApiProbe
GraphicsApiProcessId
graphicsApiProbe_
latestGraphicsApi_
graphicsApiProcessId_
graphicsApiAttempts_
all graphics-API retry constants/logs
```

Also remove:

```cpp
snapshot.graphicsApi = latestGraphicsApi_;
```

from `FillSnapshot()` and the current graphics-API liveness reconciliation call from `SampleSystemEc()`.

Update ownership comments so the controller describes its actual remaining EC/system/battery/FPS responsibilities.

### 5.3 Remove GameSession cross-domain probe hooks

Delete from `GameSessionHooks`:

```cpp
std::function<void(DWORD)> startGraphicsApiProbe;
std::function<void(DWORD)> stopGraphicsApiProbeIfTarget;
```

Remove all calls in `GameSessionController.cpp` target set/clear/reset paths.

The eligible target path must remain conceptually:

```text
currentForegroundGameProcess_ = current.process
-> setInGameForegroundProcess(pid)
-> startProductionSampling()
-> foreground.target-set log
-> reconcileHudVisibility()
```

Do not alter FPS/game-target semantics while removing IGCL.

### 5.4 Remove App wiring

Remove every production call to the graphics probe, including current sites in:

- `StopRuntimeSources()`;
- `StopHud()`;
- suspend/resume recovery;
- `MakeGameSessionHooks()`;
- `HandleTimer()` (`kGraphicsApiRetryTimerId`).

After implementation, tree-search all production source for stale probe calls/timer constants.

### 5.5 Remove `HudTelemetrySnapshot::graphicsApi` and all test fixtures

Delete:

```cpp
std::optional<std::wstring> graphicsApi;
```

Update all current users, explicitly including:

- `src/ClawHUD/HudModel.cpp` sample factories;
- `tests/HudModelTests.cpp`;
- `tests/HudTelemetryAggregatorTests.cpp` (`FillSnapshotLeavesUnownedFieldsAlone` currently seeds/asserts `graphicsApi`).

Do **not** remove `HudSegmentKind::Graphics`; FPS still uses it.

### 5.6 Third-party notice / archive rule

The repository still contains archived IGCL-derived ABI declarations under `archive/diagnostics/igcl/`. Therefore:

- keep the archived research files;
- keep their required license material;
- **do not remove the IGCL section from `THIRD-PARTY-NOTICES.md` merely because production no longer loads `ControlLib.dll`**;
- remove only production build/source references to the retired probe.

### 5.7 Part A completion search

Production must contain none of:

```text
IntelGraphicsApiProbe
kGraphicsApiRetryTimerId
IGCL Graphics API
StartGraphicsApiProbe
TryGraphicsApiProbe
HudTelemetrySnapshot::graphicsApi
```

Archived/reference documentation may still mention historical IGCL behavior.

---

# Part B — repair foreground authority using existing event-driven sources

## 6. Current production path and exact insertion point

Current `GameSessionController::HandleProductionWindowEvent()` already does:

```text
production window event
-> MicrosoftGameTrigger::InspectWindowEvent(...)
-> if WindowEventAffectsCurrentForeground(event)
     EvaluateCurrentForeground("window-event")
```

Do not describe or replace that existing path as though it does not exist.

The required fix is:

```text
production window event
-> existing Microsoft evidence handling
-> FIRST: foregroundTracker_.Reconcile()
-> THEN: existing WindowEventAffectsCurrentForeground(event)
         -> EvaluateCurrentForeground("window-event")
```

`ForegroundTracker::Reconcile()` re-reads `GetForegroundWindow()` / PID and invokes its callback only when `(HWND, PID)` differs from its cached pair. That existing callback already updates both:

```text
ProductionTelemetryController::OnForegroundProcessChanged(pid)
GameSessionController::HandleProductionForegroundChanged(...)
```

This is the intended single canonical repair path. Do not invent a second foreground cache.

## 7. Add `EVENT_OBJECT_NAMECHANGE` to the production source

Current `ProductionGameWindowSource` observes:

```text
CREATE
SHOW
HIDE
LOCATIONCHANGE
DESTROY
```

Add:

```text
NAMECHANGE
```

Required changes:

- add `ProductionWindowEventType::NameChange`;
- map `EVENT_OBJECT_NAMECHANGE`;
- increase observed-event/hook arrays from 5 to 6;
- preserve bounded queue/worker behavior.

### Mandatory callback filters

The existing filters are not optional and must remain before any event is queued:

```text
objectId == OBJID_WINDOW
childId == CHILDID_SELF
hwnd != nullptr
GetAncestor(hwnd, GA_ROOT) == hwnd
```

`NAMECHANGE` is only a wake-up/re-evaluation trigger. It is **never game evidence by itself** and must never directly set FPS/game target authority.

## 8. Why `WindowLifecycleSource` must not be reused for production

`WindowLifecycleSource` already subscribes to `EVENT_OBJECT_NAMECHANGE`, but it is a **debug observation source** owned by `DebugObservationController` and is started only when developer debug logging is enabled.

Production game detection must not become dependent on a diagnostic source that is absent in normal operation.

Therefore the correct ownership is:

```text
WindowLifecycleSource       -> debug/evidence logging only
ProductionGameWindowSource -> production authority triggers
```

Adding NAMECHANGE to `ProductionGameWindowSource` is intentional duplication of the OS event subscription to preserve the existing debug/production boundary. Do not merge these sources in this PR.

## 9. NAMECHANGE noise control is required

Global `WINEVENT_OUTOFCONTEXT` NAMECHANGE traffic can be noisy for top-level browser/app windows. The existing object/top-level filters reduce noise but do not eliminate repeated title changes on the live foreground window.

Implement **bounded event-driven suppression without a new timer**.

Required behavior:

1. `foregroundTracker_.Reconcile()` must still run first on each accepted production window event. Its own `(HWND, PID)` cache makes the callback cheap and ensures an actual foreground change is never hidden by NAMECHANGE throttling.
2. Direct `EvaluateCurrentForeground("window-event")` caused specifically by repeated NAMECHANGE for the same `(HWND, PID)` must be debounced/rate-limited.
3. Use event time/received tick state; do not add `SetTimer`, polling, sleeps, worker retries, or a new scheduler.
4. A first NAMECHANGE for a current foreground/current detector window must be evaluated immediately.
5. Repeated NAMECHANGE for the same `(HWND, PID)` inside a small fixed window (recommended ~250 ms) may be coalesced/skipped for direct detector evaluation.
6. The first event after the debounce window must be eligible again.
7. Background-window NAMECHANGE must not evaluate the current screen simply because its title changed.

A small pure helper in `GameSessionCutoverPolicy.h` is preferred if it keeps the behavior unit-testable.

Optional additional optimization, if implemented, must remain semantically narrow: a same-window foreground process already classified `Hidden / ExcludedExecutable` can suppress title-only reevaluation because a title change cannot turn that executable into an eligible game. **Do not** weaken reevaluation for `NotFullscreenLike` or renderer-verification candidates.

## 10. Preserve current screen-affect semantics

`WindowEventAffectsCurrentScreen()` currently rejects `Create` and otherwise requires the event to belong to either:

- the live foreground `(HWND, PID)`; or
- the current detector `(HWND, PID)`.

Keep that rule.

The PR #230 optimization for redundant `ExcludedExecutable` `LOCATIONCHANGE` must stay correctly scoped; do not accidentally suppress fullscreen/window-state transitions that can make a real game eligible.

## 11. Expected recovery sequences

### ATS-like Ghost -> real game

```text
tracked foreground = Ghost pid 1988
real game SHOW/HIDE event arrives
-> foregroundTracker_.Reconcile()
-> GetForegroundWindow() == pid 15172
-> existing foreground callback fires
-> Always FPS invalidates pid 1988 and targets pid 15172
-> game detection sees the same fresh foreground
```

No later task switch should be required.

### Mafia-like delayed title/fullscreen transition

```text
SHOW may occur while authority is still elsewhere
later top-level NAMECHANGE arrives
-> foregroundTracker_.Reconcile()
-> if real foreground changed, canonical callback repairs it immediately
-> if foreground was already the same HWND/PID, first non-debounced NAMECHANGE
   may still drive the existing window-event evaluation
-> normal admission / renderer verification decides eligibility
```

No target may be set from the NAMECHANGE event alone.

## 12. Explicitly prohibited Part B changes

Do not add:

- periodic `GetForegroundWindow()` polling;
- process scanning;
- WMI process lifecycle resurrection;
- ETW solely for this issue;
- a second `ForegroundTracker`;
- a second foreground PID cache in telemetry/game detection;
- retry state machines.

---

# Part C — process-lifetime first-visible presentation warm-up/recreate

## 13. Required process-lifetime behavior

Warm up exactly once per `ClawHUD.exe` process.

Do not repeat:

- per game;
- after Game A exits and Game B starts;
- after normal Hide/Show;
- after font/size/background-mode recreation;
- after HUD Disable -> Enable later in the same process;
- after F8 toggles.

A new process naturally gets a new one-shot allowance.

### Always mode

```text
process starts
-> normal HUD Ensure/Show
-> first visible non-empty HUD frame successfully Present()s
-> consume one-shot
-> post deferred warm-up request
-> next message-pump turn recreates presentation once
-> fresh frame + visibility restored normally
-> never warm up again in this process
```

### In-Game Only mode

Do **not** show HUD at startup only to warm it.

```text
process starts hidden
-> no warm-up
-> first eligible game makes HUD visible
-> first visible non-empty HUD frame successfully Present()s
-> consume one-shot
-> post deferred warm-up request
-> next message-pump turn recreates once
-> later games do not warm up again
```

If F8/manual override causes the first real non-empty visible HUD frame before a game, that frame may consume the same process-wide one-shot. Do not create a separate F8 path.

## 14. Exact trigger: visible + non-empty + `Present()` success

Current `HudPresentation::Render()` ultimately returns `presentationManager_->Present()` after buffer acquisition/drawing. It may return `S_FALSE` when no presentation buffer is available.

A successful `S_OK` alone is **not enough**, because an empty `HudTelemetrySnapshot` can still reach `Present()` successfully.

The warm-up schedule condition must require all of:

```text
presentation exists
presentation->Visible() == true
firstVisiblePresentationWarmupAttempted_ == false
FormatHud(snapshot) is non-empty (or an equivalent pure "has renderable HUD content" check)
HudPresentation::Render(snapshot, ...) returned exactly S_OK
```

Do not consume on:

```text
Initialize success only
Show success only
hidden Render
empty HUD frame
S_FALSE
FAILED(hr)
resume recovery's explicit empty RenderRecoveryFrame()
```

Prefer a small pure helper such as:

```cpp
ShouldScheduleFirstVisibleHudWarmup(
    bool attempted,
    bool visible,
    bool hasRenderableContent,
    HRESULT renderResult)
```

so this policy can be tested without D3D/DComp mocks.

## 15. One-shot ownership remains in `HudController`

`HudController` owns the concrete presentation lifecycle, so keep process-lifetime state there:

```cpp
bool firstVisiblePresentationWarmupAttempted_{};
```

Do not reset it from:

- `Recreate()`;
- `ShutdownPresentation()`;
- `MarkDisabled()`;
- setting mutations;
- game target changes;
- F8.

It resets only when a new `HudController` is constructed for a new process.

## 16. Do NOT call `Recreate()` from inside the Present/Render call stack

This is a required correction to the earlier draft.

After the first qualifying frame returns `S_OK`:

1. set `firstVisiblePresentationWarmupAttempted_ = true` **before scheduling anything**;
2. request a one-shot deferred action through the existing App/runtime message pump;
3. return normally from the current `HudController::Render()` call;
4. only on the **next message-pump turn** perform the presentation recreation.

Do not call `Recreate()` directly from the same `Render()` stack.

The preferred shape is one small App-private WM_APP message (choose an unused value after auditing current message IDs), for example conceptually:

```text
HudController::Render
-> qualifying first frame
-> mark attempted=true
-> return effect / invoke narrow callback that PostMessage()s kHudPresentationWarmup

App::ProcessMessages (later turn)
-> kHudPresentationWarmup
-> hudController_.RunFirstVisiblePresentationWarmup()
```

Either a narrow callback or a small render-effect return value is acceptable. Do not introduce a thread, async task, timer, `Sleep()`, or blocking wait.

## 17. Deferred warm-up execution

At deferred execution time:

```text
capture current visibility (not the old schedule-time assumption)
-> use existing HudController::Recreate(restoreVisible=currentVisible)
-> Recreate performs existing Shutdown -> Initialize -> requestRender(true) -> Show restore path
```

This preserves state if visibility changed between scheduling and the next message turn.

The one-shot was already consumed when scheduled. Therefore:

- if recreation succeeds: log completion;
- if recreation fails: log once and do not re-arm;
- if the presentation was destroyed/disabled before the deferred handler runs: safely no-op and do not re-arm;
- never enter a recreation loop.

`Recreate()` may invoke `requestRender_(true)`, but recursion cannot schedule another warm-up because the attempted flag was set before the deferred message was posted.

## 18. Keep `HudPresentation` itself unchanged if possible

No new presentation backend/API is required.

Current `HudPresentation::Render()` already propagates the `Present()` result, so the warm-up policy can be implemented above it.

Avoid modifying `HudPresentation.cpp` unless a concrete implementation blocker is found. Any such change must preserve the production presentation contract exactly.

---

# 19. Expected files

The exact diff may vary, but expected areas are:

## Part A

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
tests/HudTelemetryAggregatorTests.cpp
src/ClawHUD/IntelGraphicsApiProbe.h          DELETE
src/ClawHUD/IntelGraphicsApiProbe.cpp        DELETE
tests/IntelGraphicsApiProbeTests.cpp         DELETE
```

`THIRD-PARTY-NOTICES.md` should normally remain unchanged because archived IGCL-derived source remains in the repository.

## Part B

```text
src/ClawHUD/GameDetection/ProductionGameWindowSource.h
src/ClawHUD/GameDetection/ProductionGameWindowSource.cpp
src/ClawHUD/GameDetection/GameSessionController.h/.cpp
src/ClawHUD/GameDetection/GameSessionCutoverPolicy.h   if pure debounce/suppression helper used
tests/ProductionGameWindowSourceTests.cpp
tests/GameSessionCutoverPolicyTests.cpp
```

Do not route production through `WindowLifecycleSource`.

## Part C

```text
src/ClawHUD/App.cpp
src/ClawHUD/App.h                             if new private message handler declaration is needed
src/ClawHUD/HudController.h
src/ClawHUD/HudController.cpp
src/ClawHUD/HudPresentationLifecycle.h/.cpp   only if a pure policy helper naturally belongs there
tests/HudPresentationLifecycleTests.cpp       and/or existing HUD policy tests
```

Do not modify `HudPresentationContract.*`.

---

# 20. Required automated tests

## 20.1 IGCL removal

Verify by build/tree search:

- no production include/source for `IntelGraphicsApiProbe`;
- no graphics API timer/retry/state/methods;
- no `IGCL Graphics API` runtime log strings;
- no `HudTelemetrySnapshot::graphicsApi`;
- `HudTelemetryAggregatorTests` updated to verify its remaining unowned fields without `graphicsApi`;
- archived IGCL diagnostics/license remain intact.

`FormatHud()` FPS behavior must remain unchanged.

## 20.2 Production NAMECHANGE mapping

Extend `ProductionGameWindowSourceTests`:

```text
EVENT_OBJECT_NAMECHANGE -> ProductionWindowEventType::NameChange
```

Also preserve:

- OBJID_WINDOW / CHILDID_SELF filter expectations;
- top-level-only behavior;
- unsupported-event rejection;
- bounded queue ordering/capacity.

## 20.3 Foreground/window-event policy

Cover at minimum:

- `Create` still does not directly affect current screen;
- first `NameChange` on live foreground/current detector can evaluate;
- background `NameChange` cannot steal authority;
- repeated same `(HWND, PID)` NAMECHANGE inside debounce window does not repeatedly trigger direct `EvaluateCurrentForeground`;
- first event after debounce window can evaluate again;
- foreground reconciliation wake-up is not suppressed by the NAMECHANGE direct-evaluation debounce;
- PR #230 excluded `LOCATIONCHANGE` behavior remains correct;
- `NotFullscreenLike` transitions remain reevaluable.

Do not create a large DI framework only to fake Win32 foreground APIs. Keep new policy helpers pure where practical.

## 20.4 HUD warm-up policy

Pure-policy minimum matrix:

```text
attempted=false, visible=true,  content=true,  Render=S_OK    -> schedule
attempted=false, visible=true,  content=false, Render=S_OK    -> no
attempted=false, visible=true,  content=true,  Render=S_FALSE -> no
attempted=false, visible=false, content=true,  Render=S_OK    -> no
attempted=true,  visible=true,  content=true,  Render=S_OK    -> no
FAILED(Render)                                               -> no
```

Also verify:

- attempted flag is set before deferred request is posted;
- deferred handler does not re-arm on failure;
- later game/HUD Show does not schedule again;
- normal setting-driven `Recreate()` does not reset the one-shot;
- empty resume recovery frame cannot consume the warm-up.

## 20.5 Existing HUD/VRR tests

All existing assertions for the following must remain green and unchanged in meaning:

- click-through;
- no activation;
- topmost;
- transparent hit testing;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- independent flip;
- premultiplied alpha;
- production presentation contract.

No test may be weakened for this PR.

---

# 21. Mandatory hardware smoke before merge

Parts B and C are field-driven and cannot be fully proven by CTest alone. The PR description must contain a hardware smoke checklist/result summary before merge.

## 21.1 ATS-like foreground handoff

Verify with debug logging:

```text
Ghost PID becomes foreground
-> real ATS window becomes GetForegroundWindow()
-> production SHOW/HIDE event wakes ForegroundTracker immediately
-> Always foregroundPid changes to real game without task switching
-> no prolonged Ghost FPS sampling
```

Record relevant timestamps/PIDs in the PR description.

## 21.2 Mafia-like NAMECHANGE path

Verify:

```text
SHOW/NAMECHANGE occurs
-> foreground/game evaluation happens without later Alt-Tab/Task Switch
-> renderer first-frame completes
-> target-set occurs
-> PresentMon FPS is obtained
```

Record the NAMECHANGE and target-set timestamps in the PR description.

## 21.3 Always HUD warm-up

Fresh process:

1. start with HUD enabled + Always;
2. normal HUD appears;
3. exactly one first-visible non-empty Present schedules warm-up;
4. next message-pump turn recreates presentation once;
5. repeatedly maximize/restore Edge;
6. repeatedly maximize/restore Steam;
7. confirm HUD remains visually present;
8. launch/exit multiple games;
9. confirm no second warm-up.

## 21.4 In-Game Only HUD warm-up

Fresh process:

1. start with HUD enabled + In-Game Only;
2. confirm no HUD is forced visible at startup;
3. launch first game;
4. first actual non-empty in-game HUD Present schedules exactly one warm-up;
5. recreation occurs on next message-pump turn;
6. exit first game;
7. launch second game;
8. confirm no second warm-up;
9. confirm FPS target switches correctly.

## 21.5 Failure reporting

If Edge/Steam still reproduces after the one-shot recreation, report that result. Do not expand this PR into speculative presentation/window-contract changes.

---

# 22. Build / validation

Run from a clean tree:

```text
Native CMake Debug build
Native CMake Release build
ctest -E DiagWinEventTests   (Debug)
ctest -E DiagWinEventTests   (Release)
WPF Settings Release build/tests if shared/project files are touched
```

The CTest total may decrease because `ClawHUD.IntelGraphicsApiProbeTests` is deleted. Document old -> new totals in the PR.

No warning regressions.

---

# 23. Completion criteria

The PR is complete only when all are true.

## IGCL

- production Graphics API IGCL probe/timer/retry/state is gone;
- `HudTelemetrySnapshot::graphicsApi` and all fixtures are gone;
- PresentMon API2 telemetry/FPS remains functional;
- Intel VRR Range Fix remains untouched;
- archived IGCL research/license remains intact.

## Foreground authority

- production listens to filtered top-level `NAMECHANGE`;
- every accepted production window event first wakes existing `ForegroundTracker::Reconcile()`;
- current existing `window-event` evaluation path remains and runs after reconciliation;
- repeated NAMECHANGE direct evaluation is bounded without polling/timers;
- Always FPS and game detection converge on the same fresh foreground;
- background title changes cannot steal authority;
- no fixed-fallback-timer behavior is introduced.

## HUD presentation warm-up

- only a visible, non-empty HUD frame whose `Render()` returned `S_OK` can schedule warm-up;
- warm-up recreation is deferred to a later App message-pump turn;
- exactly one attempt per process;
- Always triggers naturally on first actual HUD display;
- In-Game Only triggers naturally on first actual in-game HUD display;
- later games/setting recreations/HUD toggles do not re-arm it;
- failed/no-op deferred recreation does not retry;
- production presentation contract/backend remains unchanged.

## Merge gate

- automated builds/tests green;
- hardware smoke results for ATS/Mafia and Always/In-Game Only included in PR description;
- Edge/Steam maximize behavior re-tested;
- no HUD/VRR invariant changed.

---

# 24. Suggested PR title / commit organization

Suggested title:

```text
Remove retired IGCL probe and harden foreground/HUD startup recovery
```

Optional pre-squash commit organization:

```text
1. Remove retired production IGCL graphics API probe
2. Reconcile foreground authority from production window events
3. Warm up first visible HUD presentation once per process
```

Final merge should be squashable as one coherent post-refactor production-fix PR.
