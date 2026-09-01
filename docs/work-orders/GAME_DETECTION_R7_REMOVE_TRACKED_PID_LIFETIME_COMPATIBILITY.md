# Work Order — Game Detection R7: Remove Tracked-PID / Single-Lifetime Compatibility Layer

Status: implementation work order  
Repository: `onehoon/ClawHUD`  
Implementation baseline: latest `main` after PR #206 (`R6: remove the legacy sticky game-detection state machine`)  
Baseline commit at authoring time: `d89ec25ad14ac855837e5e5ce40ada1521b5fcc8`  
Parent design: `docs/GAME_DETECTION_REDESIGN_PR_PLAN_2026-08-31.md`  
Field evidence: `docs/GAME_DETECTION_FIELD_ANALYSIS_2026-08-31.md`  
R5 work order: `docs/work-orders/GAME_DETECTION_R5_CURRENT_FOREGROUND_TARGET_SEMANTICS.md`  
R6 work order: `docs/work-orders/GAME_DETECTION_R6_REMOVE_LEGACY_STICKY_STATE_MACHINE.md`

---

## 1. Goal

Finish the seven-PR game-detection redesign by removing the final compatibility layer left over from the old single-selected-process architecture.

R4 moved production authority to `ForegroundGameDetector`. R5 made HUD visibility, In-Game-Only FPS, graphics-API targeting, mode switching, and resume recovery follow the **current eligible foreground game**. R6 deleted the dormant global candidate/commit state machine and its coordinator adapters.

After R6, two legacy concepts intentionally remain:

```text
ForegroundTracker
    -> foreground WinEvent source                  [still needed]
    -> selected/tracked PID + process HANDLE       [obsolete]
    -> foregroundMatches state                     [obsolete]

ProductionProcessLifetimeWatcher
    -> standalone class still compiled             [obsolete]
    -> no production Arm() path remains            [obsolete]
    -> only Disarm() is called during shutdown     [obsolete]
```

R7 must remove those compatibility responsibilities without changing the foreground-first behavior introduced by R1–R6.

The final authority remains:

```text
current foreground HWND/PID
    -> GameScreenAdmission
    -> exact GameProcessInstance
    -> KnownGameProcessCache
    -> ForegroundGameDetector
    -> currentForegroundGameProcess_
        -> HUD visibility
        -> In-Game-Only FPS target
        -> Graphics API target
```

`ForegroundTracker` should remain only as the lightweight event source/current-foreground reconciliation mechanism required by this path.

R7 is a **cleanup / responsibility-finalization PR**, not a new game-detection algorithm.

---

# 2. Non-negotiable HUD / VRR safety contract

R7 is game-detection plumbing cleanup only.

Do **not** modify, replace, weaken, or work around any production HUD presentation behavior:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- existing `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirement;
- existing Presentation API / DirectComposition production presentation path;
- premultiplied-alpha presentation contract.

Do not change opacity behavior.

Do not use R7 as an opportunity to fix or alter HUD Z-order, recreation, activation, hit testing, swap/presentation behavior, DPI behavior, or the diagnostic logging added by PR #205.

Existing regression coverage for:

```text
click-through
no activation
topmost
transparent hit testing
independent flip
premultiplied alpha
production presentation contract
```

must remain unchanged and passing.

---

# 3. Current-main inventory after R6

Review latest `main` before editing. The following describes baseline `d89ec25...`.

## 3.1 `ForegroundTracker` still performs two unrelated jobs

Current `ForegroundTracker` owns:

```cpp
using ChangedCallback = std::function<void(bool)>;
using ForegroundChangedCallback = std::function<void(HWND, DWORD)>;

void SetTrackedProcessId(DWORD processId);
DWORD TrackedProcessId() const noexcept;
bool ForegroundIsTrackedProcess() const noexcept;
static bool PidsMatch(DWORD foregroundProcessId, DWORD trackedProcessId) noexcept;
```

and state:

```cpp
DWORD trackedProcessId_{};
HANDLE trackedProcess_{};
HWND lastForegroundWindow_{};
DWORD lastForegroundProcessId_{};
bool foregroundMatches_{};
ChangedCallback changed_;
ForegroundChangedCallback foregroundChanged_;
```

`SetTrackedProcessId()` opens a process handle with:

```cpp
OpenProcess(
    SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
    FALSE,
    processId)
```

and `Reconcile()` currently does both:

```text
1. detect current foreground HWND/PID change and emit foregroundChanged_
2. compare current foreground PID against selected tracked PID
3. check selected process HANDLE liveness
4. maintain foregroundMatches_
5. emit changed_(matches)
```

Only item 1 is part of the final architecture.

## 3.2 `GameSessionController` still bridges current game target into `ForegroundTracker`

Current R5/R6 target transitions still contain:

```cpp
// Eligible
currentForegroundGameProcess_ = current.process;
foregroundTracker_.SetTrackedProcessId(processId);
hooks_.setInGameForegroundProcess(processId);

// Clear
currentForegroundGameProcess_.reset();
foregroundTracker_.SetTrackedProcessId(0);
hooks_.clearInGameForegroundProcess();
```

`ResetForegroundGameSession()` also calls:

```cpp
foregroundTracker_.SetTrackedProcessId(0);
```

These calls no longer contribute to game authority. `App::ReconcileHudVisibility()` already uses `GameSessionController::CurrentForegroundGameActive()` rather than tracker match state.

R7 must delete this bridge.

## 3.3 `StartForegroundTracking()` still registers a match callback

Current startup shape is approximately:

```cpp
foregroundTracker_.Start(
    messageWindow_,
    kForegroundChanged,
    [this](bool matches)
    {
        // log selected PID / clear
        hooks_.reconcileHudVisibility();
    },
    [this](HWND window, DWORD processId)
    {
        hooks_.onForegroundChanged(window, processId);
        if (Runtime().hudEnabled)
            HandleProductionForegroundChanged(window, processId);
    });
```

The first callback exists only because the tracker still maintains selected-PID match state.

The second callback is the useful foreground event path and must remain.

## 3.4 `ReconcileForeground()` is still useful

Current public API:

```cpp
void GameSessionController::ReconcileForeground()
{
    foregroundTracker_.Reconcile();
}
```

Resume recovery calls:

```text
GameSessionController::ReconcileForeground()
GameSessionController::ReevaluateForeground()
```

This remains useful after tracked-PID removal because `ReconcileForeground()` catches a missed foreground transition and updates the event-tail consumers (`ProductionTelemetryController` Always-mode target, debug observation, etc.) before the explicit current-game evaluation.

Do not remove this API merely because selected-PID matching is removed.

## 3.5 `ProductionProcessLifetimeWatcher` is now completely dormant

R6 intentionally left the class for R7.

Current `GameSessionController` still owns:

```cpp
ProductionProcessLifetimeWatcher productionProcessLifetimeWatcher_;
```

but there is no production `Arm()` call after R6.

The only remaining controller interaction is:

```cpp
productionProcessLifetimeWatcher_.Disarm();
```

inside `StopSources()`.

The class therefore no longer affects runtime behavior and should be deleted in R7.

## 3.6 Some `GameSessionHooks` fields are now dead compatibility surface

At the R6 baseline, the struct still declares:

```cpp
std::function<void(DWORD)> ensureGraphicsApiProbe;
std::function<void()> stopGraphicsApiProbe;
std::function<void(bool, const wchar_t*)> stopProductionSampling;
```

but `GameSessionController` no longer calls those hooks.

Before deleting them, confirm with a current-tree reference search. If still unused, remove them from `GameSessionHooks` and from `App::MakeGameSessionHooks()`.

Do **not** remove live hooks such as:

```text
runtimeState
onForegroundChanged
reconcileHudVisibility
startGraphicsApiProbe
stopGraphicsApiProbeIfTarget
setInGameForegroundProcess
clearInGameForegroundProcess
stopFpsSampling
startProductionSampling
```

unless the current implementation proves a specific one is also dead and its removal is behavior-neutral.

## 3.7 Verifier queries are not all equivalent

Current controller exposes:

```cpp
DWORD VerifierProcessId() const noexcept;
std::uint64_t VerifierGeneration() const noexcept;
bool VerifierRunning() const noexcept;
```

Resume recovery currently uses:

```text
VerifierProcessId()
VerifierRunning()
```

so those two are live and must remain unless the resume-recovery design is separately changed, which is **not** an R7 goal.

`VerifierGeneration()` currently has no known production caller. Confirm with a current-tree search; if unused, remove it as final API cleanup.

---

# Part A — Reduce `ForegroundTracker` to foreground observation only

## 4. Remove selected/tracked PID APIs

Delete:

```cpp
void SetTrackedProcessId(DWORD processId);
DWORD TrackedProcessId() const noexcept;
bool ForegroundIsTrackedProcess() const noexcept;
static bool PidsMatch(DWORD foregroundProcessId, DWORD trackedProcessId) noexcept;
```

Do not replace them with equivalent selected-PID APIs under a new name.

The final `ForegroundTracker` must not own game target selection.

## 5. Remove tracked-process state and process HANDLE ownership

Delete:

```cpp
DWORD trackedProcessId_{};
HANDLE trackedProcess_{};
bool foregroundMatches_{};
```

Delete the liveness helper:

```cpp
bool TrackedProcessIsAlive() const noexcept;
```

Remove all `OpenProcess`, `WaitForSingleObject`, and `CloseHandle` operations that exist only for selected game tracking.

After R7, `ForegroundTracker` must hold **no game-process HANDLE**.

This is important architecturally:

> Process identity/liveness for current-game authority is handled by exact `GameProcessInstance` validation and foreground re-evaluation, not by a selected PID handle hidden inside the event source.

## 6. Simplify callback model

Remove the match callback concept:

```cpp
using ChangedCallback = std::function<void(bool)>;
ChangedCallback changed_;
```

Prefer a simple `Start()` contract centered on the foreground observation callback, for example:

```cpp
using ForegroundChangedCallback = std::function<void(HWND, DWORD)>;

bool Start(
    HWND dispatchWindow,
    UINT reconcileMessage,
    ForegroundChangedCallback foregroundChanged);
```

The exact parameter naming may vary, but there should be no callback whose payload means "foreground matches the selected game PID".

## 7. Preserve event-source mechanics

Keep the event-driven design:

```text
SetWinEventHook(EVENT_SYSTEM_FOREGROUND)
    -> WinEventProc
    -> PostMessageW(dispatchWindow_, reconcileMessage_)
    -> GameSessionController::HandleMessage
    -> ForegroundTracker::Reconcile()
```

Do not add:

- polling;
- periodic timers;
- `EnumWindows` loops;
- background process scans;
- per-frame foreground queries;
- a new thread solely for foreground detection.

Keep `WINEVENT_OUTOFCONTEXT` behavior unless an unrelated existing requirement explicitly says otherwise.

## 8. Preserve last/current foreground deduplication

`ForegroundTracker` may continue storing:

```cpp
HWND lastForegroundWindow_{};
DWORD lastForegroundProcessId_{};
```

`Reconcile()` should continue to:

```text
GetForegroundWindow()
-> GetWindowThreadProcessId()
-> compare with last HWND/PID
-> update last HWND/PID
-> invoke foregroundChanged_(hwnd, pid) only when the observed foreground changed
```

This avoids duplicate event-tail work while preserving explicit `ReconcileForeground()` recovery behavior.

Do not make the tracker decide whether that foreground is a game.

## 9. Preserve startup and stop semantics

`Start()` should still:

```text
validate dispatch window / message
register EVENT_SYSTEM_FOREGROUND hook
store callback
perform an initial Reconcile()
```

`Stop()` should still:

```text
UnhookWinEvent
clear active singleton pointer when applicable
clear dispatch window/message
clear last foreground HWND/PID
clear callback
```

but must no longer close a selected process handle because none should exist.

---

# Part B — Remove tracker compatibility from `GameSessionController`

## 10. Simplify `StartForegroundTracking()`

Remove the obsolete match-state callback entirely.

Target shape:

```cpp
bool GameSessionController::StartForegroundTracking()
{
    return foregroundTracker_.Start(
        messageWindow_,
        kForegroundChanged,
        [this](HWND window, DWORD processId)
        {
            hooks_.onForegroundChanged(window, processId);
            if (Runtime().hudEnabled)
                HandleProductionForegroundChanged(window, processId);
        });
}
```

Equivalent organization is acceptable.

Remove logs whose only meaning is selected tracker match state, for example:

```text
Foreground target pid=...
Foreground target cleared
```

Do not remove the current foreground-first logs:

```text
[GameDetection] foreground.evaluate
[GameDetection] foreground.target-set
[GameDetection] foreground.target-clear
[GameDetection] foreground.hidden
[GameDetection] renderer.first-frame
[GameDetection] microsoft.evidence
[GameDetection] steam.session
```

## 11. Remove `SetTrackedProcessId()` calls from target transitions

From `ApplyForegroundEvaluation()` remove:

```cpp
foregroundTracker_.SetTrackedProcessId(processId);
foregroundTracker_.SetTrackedProcessId(0);
```

From `ResetForegroundGameSession()` remove:

```cpp
foregroundTracker_.SetTrackedProcessId(0);
```

No replacement is required.

Target state is already represented by:

```cpp
std::optional<GameProcessInstance> currentForegroundGameProcess_;
```

and downstream target hooks.

## 12. Keep target transition ordering otherwise unchanged

Do not use R7 to reorder or redesign the R5 target transition contract.

Eligible transition must still effectively perform:

```text
stop old graphics target if necessary
-> set exact currentForegroundGameProcess_
-> set In-Game foreground telemetry target
-> start graphics API probe
-> ensure production sampling
-> reconcile HUD visibility
```

Clear transition must still effectively perform:

```text
stop old graphics target if necessary
-> clear currentForegroundGameProcess_
-> clear In-Game foreground telemetry target
-> reconcile HUD visibility
```

Removing `ForegroundTracker.SetTrackedProcessId(...)` must be the only semantic deletion from these paths.

## 13. Keep `ReconcileForeground()`

Retain:

```cpp
void ReconcileForeground();
```

with event-source meaning only.

Required semantics after R7:

> Refresh foreground observation and emit the normal foreground-changed tail if HWND/PID changed; do not select, match, retain, or validate a game target.

Resume recovery may continue:

```text
ReconcileForeground()
ReevaluateForeground()
```

Do not collapse these into old tracked-PID semantics.

---

# Part C — Delete `ProductionProcessLifetimeWatcher`

## 14. Remove the dormant member

Delete from `GameSessionController`:

```cpp
#include "ProductionProcessLifetime.h"
ProductionProcessLifetimeWatcher productionProcessLifetimeWatcher_;
```

Remove from `StopSources()`:

```cpp
productionProcessLifetimeWatcher_.Disarm();
```

There is no replacement.

## 15. Delete source/header

Delete:

```text
src/ClawHUD/GameDetection/ProductionProcessLifetime.h
src/ClawHUD/GameDetection/ProductionProcessLifetime.cpp
```

Do not introduce a new single-current-game process watcher.

Current-game liveness safety remains:

```cpp
GameSessionController::RevalidateCurrentForegroundGame()
    -> QueryGameProcessInstance(current PID)
    -> compare exact PID + creation time
    -> EvaluateCurrentForeground(...) on mismatch
```

Known-game evidence remains process-generation aware in `KnownGameProcessCache`.

That is sufficient for the final architecture.

## 16. Do not add per-known-game watchers

Never replace the deleted watcher with:

```text
one HANDLE/thread/wait registration per cached game
```

The cache is knowledge, not process ownership.

A background known game may remain cached while alive, but it is never HUD/FPS authority unless it becomes the current admitted foreground again.

---

# Part D — Final `GameSessionHooks` / controller API cleanup

## 17. Remove dead hook fields

At implementation time, perform a reference search over every `GameSessionHooks` member.

At the R6 baseline, these appear unused by `GameSessionController`:

```cpp
ensureGraphicsApiProbe
stopGraphicsApiProbe
stopProductionSampling
```

If current `main` still has no controller caller, remove the fields and matching `App::MakeGameSessionHooks()` assignments.

Do not remove the corresponding `ProductionTelemetryController` public methods merely because the hook field is dead; `App` still directly uses graphics-probe lifecycle APIs during suspend/resume/shutdown.

The goal is only to remove dead **cross-controller hook plumbing**.

## 18. Preserve live hook responsibilities

Keep the narrow hook surface required by the final controller, expected to include:

```text
runtimeState
onForegroundChanged
reconcileHudVisibility
startGraphicsApiProbe
stopGraphicsApiProbeIfTarget
setInGameForegroundProcess
clearInGameForegroundProcess
stopFpsSampling
startProductionSampling
```

Exact final list should follow real call sites, not this document mechanically.

Do not create a generic event bus.

## 19. Remove unused verifier API only where proven dead

Keep:

```cpp
DWORD VerifierProcessId() const noexcept;
bool VerifierRunning() const noexcept;
```

because current resume recovery consumes them.

If current-tree search still shows no caller for:

```cpp
std::uint64_t VerifierGeneration() const noexcept;
```

remove it from `GameSessionController.h/.cpp`.

Do not redesign resume recovery in R7 solely to remove the remaining verifier queries.

---

# Part E — App / resume / HUD semantic preservation

## 20. `App::ReconcileHudVisibility()` remains current-game based

Do not reintroduce tracker match state.

In-Game-Only visibility must remain based on:

```cpp
gameSession_.CurrentForegroundGameActive()
```

Manual override remains presentation-only.

Always mode remains independent from game detection.

## 21. Preserve foreground event tail

The useful `onForegroundChanged` hook remains responsible for the existing App tail:

```text
ProductionTelemetryController::OnForegroundProcessChanged(pid)
-> ReconcileHudVisibility()
-> DebugObservationController::OnForegroundChanged(hwnd, pid) when enabled
```

This matters because Always-mode FPS follows raw current foreground PID independently of game eligibility.

R7 must not route Always mode through `ForegroundGameDetector`.

## 22. Preserve resume recovery authority

Current resume behavior must stay foreground-first:

```text
fresh foreground observation
-> fresh foreground game evaluation
-> current exact eligible game target
```

Never restore a pre-suspend target because:

```text
old PID is still alive
old process HANDLE is signaled/not signaled
old tracker matched before suspend
```

After R7 those concepts should no longer exist in the foreground event source.

Do not remove `VerifierProcessId()` / `VerifierRunning()` while the existing recovery flow uses them to decide whether an in-flight verifier can be retained.

## 23. Preserve current-game liveness behavior

`RevalidateCurrentForegroundGame()` must continue to detect:

```text
process exited
PID reused with a different creation time
```

and trigger a fresh current foreground evaluation.

Do not replace exact process-generation checking with numeric PID-only liveness.

---

# Part F — Build-system and test cleanup

## 24. Remove `ProductionProcessLifetime` from build files

Delete the production source from root `CMakeLists.txt`:

```text
src/ClawHUD/GameDetection/ProductionProcessLifetime.cpp
```

Delete the test target from `cmake/ClawHUDTests.cmake`:

```text
ClawHUD.ProductionProcessLifetimeTests
```

Delete:

```text
tests/ProductionProcessLifetimeTests.cpp
```

No equivalent test is required because the utility itself no longer exists.

## 25. Update or remove obsolete `ForegroundTrackerTests`

Current `tests/ForegroundTrackerTests.cpp` only validates:

```cpp
ForegroundTracker::PidsMatch(...)
```

That test is obsolete once tracked-PID matching is removed.

Preferred rule:

- if the simplified tracker naturally exposes meaningful deterministic foreground-observation logic, update the test to cover that logic;
- otherwise delete the obsolete `ForegroundTrackerTests` target/file rather than introducing production API solely to test trivial HWND/PID comparison.

Do not keep `PidsMatch()` as a test-only compatibility API.

Live desktop WinEvent validation can remain in the existing diagnostic/live-desktop coverage; do not add a polling substitute just to make the event hook easy to unit test.

## 26. Preserve foreground-first behavioral tests

Keep all relevant current tests for:

```text
GameScreenAdmission
KnownGameProcessCache
ForegroundGameDetector
GameSessionCutoverPolicy
MicrosoftGameTrigger
GameRenderVerifier
AlwaysModeFpsTarget
ProductionTelemetryController / PresentMon target release-rebind
resume recovery policy
HUD presentation contract/lifecycle
```

In particular retain equivalent assertions for:

```text
A -> unknown B clears A before B verification
A -> Explorer clears target
A -> eligible B retargets while A remains alive
same numeric PID + new creation time is a new target generation
late renderer completion cannot replace current foreground
known-game Alt+Tab return is immediate
Steam context alone does not make a game eligible
WindowsTerminal/excluded foreground stays hidden
```

Do not delete a foreground-first regression test merely because its historical counterpart originally came from the old coordinator suite.

---

# 27. Required static validation

Before finalizing, run a current-tree reference search.

There should be **no active source/build/test references** to:

```text
SetTrackedProcessId
TrackedProcessId
ForegroundIsTrackedProcess
PidsMatch
TrackedProcessIsAlive
trackedProcessId_
trackedProcess_
foregroundMatches_
ProductionProcessLifetimeWatcher
ProductionProcessLifetime.h
ProductionProcessLifetime.cpp
```

Historical design/work-order documentation may still mention those terms as migration history.

Also confirm there is no new equivalent concept such as:

```text
selectedGamePid
matchedForegroundPid
activeGameHandle
currentGameLifetimeWatcher
```

unless it has a separate, demonstrably required responsibility. R7 must not rename the compatibility layer instead of deleting it.

---

# 28. Required runtime invariants

R7 must preserve all R1–R6 user-visible semantics.

## 28.1 Explorer / desktop

```text
In-Game-Only + Explorer
-> current game = none
-> HUD hidden
-> In-Game FPS target = 0
```

## 28.2 Known game foreground

```text
Explorer -> known admitted Game A
-> Game A eligible immediately
-> target A
-> HUD shown in In-Game-Only
```

## 28.3 Alt+Tab away

```text
Game A -> Explorer/Steam
-> current game clears immediately
-> no selected/tracked PID remains anywhere
-> A may remain known in cache
```

## 28.4 Alt+Tab back

```text
Explorer -> known Game A
-> fresh foreground observation
-> admission
-> exact process-generation cache hit
-> eligible immediately
```

No process HANDLE in `ForegroundTracker` is needed.

## 28.5 Game A -> unknown Game B while A remains alive

```text
A eligible
-> B becomes current foreground
-> A clears immediately
-> B enters NeedsRendererVerification
-> no fallback to A
-> B becomes eligible only after verification + fresh current-foreground evaluation
```

## 28.6 Game A -> known Game B while A remains alive

```text
A eligible
-> B foreground/admitted/known
-> direct target A -> B
-> A liveness irrelevant
```

## 28.7 PID reuse

```text
PID X / generation A was known/current
-> exits
-> Windows reuses PID X / generation B
-> generation A target/evidence cannot remain authoritative
```

## 28.8 Always mode

```text
Always
-> raw current foreground PID continues through AlwaysModeFpsTarget
-> game detection may independently evaluate current screen
-> removing tracker match state must not affect Always FPS targeting
```

## 28.9 Suspend/resume

```text
resume on Explorer
-> do not resurrect pre-suspend game

resume on same admitted known game
-> fresh foreground observation/evaluation can restore game

resume on unknown Game B
-> normal verification path
-> no fallback to old Game A
```

---

# 29. Focused real-device smoke

Because R7 removes compatibility plumbing but should not change behavior, perform a short focused smoke after build/test success.

Recommended matrix:

```text
1. In-Game-Only + Explorer
   -> HUD hidden

2. Launch / foreground a previously verified known game
   -> HUD appears
   -> FPS + graphics API target correct

3. Alt+Tab game -> Explorer -> same game
   -> hide immediately on Explorer
   -> show immediately on known-game return

4. Game A -> Game B while A remains alive
   -> B becomes authority
   -> never fall back to A

5. Windows Terminal foreground
   -> never game eligible / never sticky

6. Always mode while switching Explorer / Steam / game
   -> HUD remains according to Always semantics
   -> FPS follows raw current foreground target behavior

7. Suspend/resume on Explorer and on an active game if convenient
   -> no stale pre-suspend target resurrection
```

Do not block R7 on exotic timing-only scenarios that have no realistic normal-use path. Follow the project PR review policy: only realistic, materially harmful regressions are blockers.

---

# 30. Non-goals

Do **not** include:

- new game-detection heuristics;
- new executable exclusions unless separately justified by a real field issue;
- Steam AppID behavior changes;
- Microsoft identity policy changes;
- PresentMon renderer-verifier redesign;
- FPS provider redesign;
- polling/timer-based game detection;
- HUD presentation changes;
- HUD Z-order fix work from PR #205 diagnostics;
- opacity or layout work;
- Diag app refactoring;
- broad App architecture refactoring unrelated to tracked-PID/lifetime cleanup;
- speculative synchronization/state-machine complexity.

If removal exposes a real dependency that requires changing foreground-first behavior, stop and isolate that conflict rather than silently redesigning it inside R7.

---

# 31. Build and validation

Required before PR completion:

```text
Configure Debug x64
Build Debug x64
Configure Release x64
Build Release x64
Run full applicable CTest suite
```

CI must pass the normal ClawHUD Build Test workflow.

`ClawHUD.DiagWinEventTests` may retain its existing live-interactive-desktop exception if the CI environment still cannot run it; do not weaken other tests to accommodate it.

There must be no new compiler warnings attributable to R7.

---

# 32. Acceptance checklist

- [ ] `ForegroundTracker` no longer owns a selected/tracked game PID.
- [ ] `ForegroundTracker` no longer owns a game-process HANDLE.
- [ ] `ForegroundTracker` no longer maintains `foregroundMatches` state.
- [ ] `SetTrackedProcessId`, `TrackedProcessId`, `ForegroundIsTrackedProcess`, and `PidsMatch` are gone.
- [ ] `ForegroundTracker` remains event-driven and still reports actual foreground HWND/PID changes.
- [ ] `GameSessionController` target set/clear/reset paths no longer write tracker PID state.
- [ ] `GameSessionController::ReconcileForeground()` remains event-source reconciliation only.
- [ ] `ProductionProcessLifetimeWatcher` source/header/member/test/build references are removed.
- [ ] no replacement single-game lifetime watcher is introduced.
- [ ] dead `GameSessionHooks` compatibility fields are removed where reference search proves them unused.
- [ ] `VerifierProcessId()` and `VerifierRunning()` remain while resume recovery uses them.
- [ ] unused `VerifierGeneration()` is removed if still unreferenced.
- [ ] current foreground HWND/PID remains the sole final game authority.
- [ ] exact `GameProcessInstance` generation remains the current-game/cache identity basis.
- [ ] Steam remains context only.
- [ ] Microsoft/renderer evidence remains process-generation scoped.
- [ ] A -> unknown B clears A immediately.
- [ ] A -> B while A remains alive never falls back to A.
- [ ] known-game Alt+Tab return remains immediate.
- [ ] Always mode remains independent from game detection.
- [ ] resume recovery never restores a game merely because it is still alive.
- [ ] no new polling, timers, broad scans, or per-frame game-detection work is added.
- [ ] PR #205 HUD diagnostics remain untouched.
- [ ] HUD click-through / no-activation / topmost / transparent-hit-test behavior is unchanged.
- [ ] independent flip and premultiplied-alpha presentation contracts remain unchanged.
- [ ] Debug x64 build passes.
- [ ] Release x64 build passes.
- [ ] full applicable CTest suite passes.

---

# 33. Expected final architecture

After R7, the seven-PR game-detection redesign is complete.

Expected structure:

```text
EVENT_SYSTEM_FOREGROUND
        |
        v
ForegroundTracker
  event source only
  last HWND/PID only
        |
        v
GameSessionController
        |
        +--> App foreground tail
        |      -> Always-mode foreground target
        |      -> debug observation
        |
        +--> ObserveGameScreen
                 |
                 v
          GameScreenAdmission
                 |
                 v
          ForegroundGameDetector
           /       |         \
          /        |          \
 KnownGameCache  Steam ctx   Renderer verifier
          \        |          /
           \       |         /
            current exact eligible
             foreground game
                    |
          +---------+---------+
          |         |         |
          v         v         v
      HUD state   FPS PID   Graphics API PID
```

There should be:

```text
no global committed candidate
no selected/tracked game PID in ForegroundTracker
no selected game process HANDLE in ForegroundTracker
no single-game lifetime watcher
no old-game liveness authority
```

The only live authority for In-Game-Only remains the currently admitted, positively identified/verified foreground process generation.

---

# 34. PR guidance

Keep this as one focused final cleanup PR.

Suggested title:

```text
R7: remove tracked-PID and lifetime compatibility layer
```

The PR description should explicitly state that it is behavior-preserving cleanup completing the foreground-first game-detection redesign.

Call out:

- tracked-PID/HANDLE removal from `ForegroundTracker`;
- deletion of `ProductionProcessLifetimeWatcher`;
- dead hook/API cleanup;
- preserved resume verifier queries;
- preserved foreground-first tests and HUD/VRR contract;
- build/test results;
- focused real-device smoke result if available.
