# Work Order — Game Detection R5: Current Foreground Game Target Semantics

Status: implementation work order  
Repository: `onehoon/ClawHUD`  
Implementation baseline: latest `main` after PR #203 (`R4: cut GameSessionController over to foreground-first detection`)  
Baseline commit at authoring time: `1c6253d958479a581ae772d948609a6ed1863fb2`  
Parent design: `docs/GAME_DETECTION_REDESIGN_PR_PLAN_2026-08-31.md`  
Field evidence: `docs/GAME_DETECTION_FIELD_ANALYSIS_2026-08-31.md`  
R3 work order: `docs/work-orders/GAME_DETECTION_R3_FOREGROUND_FIRST_DETECTOR_CORE.md`  
R4 work order: `docs/work-orders/GAME_DETECTION_R4_GAME_SESSION_CONTROLLER_CUTOVER.md`

---

## 1. Goal

Complete the runtime semantic migration started by R4 by removing the misleading downstream **Committed Process** contract from HUD visibility, FPS targeting, graphics-API targeting, mode switching, and suspend/resume integration.

R4 already made `ForegroundGameDetector` the production game-screen authority. However, R4 intentionally retained a compatibility bridge so the first production cutover could remain isolated:

```text
current eligible foreground game
    -> ForegroundTracker.SetTrackedProcessId(...)
    -> GameSessionHooks.setCommittedProcess(...)
    -> ProductionTelemetryController::SetCommittedProcess(...)
    -> committedProcessId_
```

Those names describe the old architecture, not the runtime behavior after R4.

R5 must make the downstream contract explicit:

> **In-Game Only always follows the currently eligible foreground game process. It never follows a long-lived committed/background process.**

After this PR, the active runtime path must no longer contain any downstream API or field whose name implies that a game remains authoritative because it was previously committed.

The intended split is:

```text
Always mode
    -> current foreground PID
    -> AlwaysModeFpsTarget

In-Game Only
    -> current eligible foreground game PID
    -> ForegroundGameDetector / GameSessionController authority
```

The FPS provider must remain PID-only and unaware of game identity.

R6 will remove the dormant legacy coordinator/state-machine implementation.  
R7 will remove remaining tracker/lifetime compatibility plumbing that is no longer needed.

Do not mix those cleanup PRs into R5 unless a very small compile-only adaptation is required.

---

# 2. Why this PR exists

The 2026-08-31 field capture showed that the old global `Committed` target could allow an ordinary renderer such as `WindowsTerminal.exe` to remain authoritative and block later real games.

R1–R4 changed the production authority model:

```text
R1  hard foreground screen admission
R2  process-generation-aware known-game evidence
R3  foreground-first decision core
R4  GameSessionController production cutover
```

After R4, the actual authority is already:

```text
current foreground HWND/PID
-> GameScreenAdmission
-> exact GameProcessInstance
-> KnownGameProcessCache
-> ForegroundGameDetector
-> Eligible / NeedsRendererVerification / Hidden
```

But several downstream names still encode the old model:

```cpp
GameSessionHooks::setCommittedProcess
GameSessionHooks::clearCommittedProcess
ProductionTelemetryController::SetCommittedProcess
ProductionTelemetryController::ClearCommittedProcess
ProductionTelemetryController::committedProcessId_
GameSessionController::ReleaseCommittedIfForegroundGone
GameSessionController::CommittedProcessAliveOrNone
App::ReconcileHudVisibility() -> ForegroundIsTrackedProcess()
```

That creates two problems:

1. the code no longer communicates the actual runtime invariant;
2. future maintenance can accidentally reintroduce sticky/background target behavior because the API still suggests a committed game is a durable authority.

R5 removes that ambiguity.

---

# 3. Non-negotiable HUD / VRR safety contract

This PR is game-target / telemetry / visibility orchestration only.

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
- existing Presentation API / DirectComposition production path;
- premultiplied-alpha presentation contract.

Do not change opacity behavior in this PR.

Do not use this migration as a reason to recreate the HUD window, alter hit testing, alter activation, alter swap/presentation semantics, or add a different overlay path.

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

# 4. Current-main inventory after R4

Review latest `main` before editing. The following describes the baseline at commit `1c6253d...`.

## 4.1 `ForegroundGameDetector` is already production authority

`GameSessionController::EvaluateCurrentForeground()` now performs:

```text
GetForegroundWindow
-> GetWindowThreadProcessId
-> ObserveGameScreen
-> ForegroundGameDetector::Evaluate
-> ApplyForegroundEvaluation
```

This is correct and remains the authority.

Do not reintroduce legacy `GameDetectionCoordinator` decisions into the live path.

## 4.2 R4 compatibility bridge

`GameSessionController::ApplyForegroundEvaluation()` currently translates the foreground-first result into old downstream names.

Conceptually:

```cpp
if (current == Eligible)
{
    bridgedEligibleProcess_ = current.process;
    foregroundTracker_.SetTrackedProcessId(pid);
    hooks_.setCommittedProcess(pid);
    hooks_.startGraphicsApiProbe(pid);
}

if (current becomes Hidden / NeedsRendererVerification)
{
    bridgedEligibleProcess_.reset();
    foregroundTracker_.SetTrackedProcessId(0);
    hooks_.clearCommittedProcess();
}
```

The behavior is already foreground-first; the naming is not.

## 4.3 `ProductionTelemetryController` still caches a committed PID

Current API/field:

```cpp
void SetCommittedProcess(DWORD processId) noexcept;
void ClearCommittedProcess() noexcept;
DWORD committedProcessId_{};
```

`SampleFps()` currently selects:

```cpp
ResolveProductionFpsTargetPid(
    visibilityMode_,
    alwaysFpsTarget_.TargetProcessId(),
    committedProcessId_)
```

The target split is structurally correct. R5 must rename and harden the semantic contract.

## 4.4 `App::ReconcileHudVisibility()` still asks tracker-style questions

Current shape:

```cpp
gameSession_.ReleaseCommittedIfForegroundGone();
productionTelemetry_.ReconcileGraphicsApiTargetLiveness();
const bool foregroundGameActive = gameSession_.ForegroundIsTrackedProcess();
const auto effects = hudController_.ReconcileVisibility(foregroundGameActive);
```

This should use explicit current-game APIs rather than inferring through `ForegroundTracker` compatibility state.

## 4.5 Visibility mode switching is incomplete for the new semantic model

`App::SetHudVisibilityMode()` currently:

- immediately adopts current foreground PID when switching to `Always`;
- updates `ProductionTelemetryController` mode;
- does not explicitly re-evaluate the current game screen when switching into `InGameOnly`.

R5 must make `Always -> InGameOnly` immediately evaluate the current foreground rather than waiting for the next WinEvent.

## 4.6 Resume recovery still uses compatibility names

`App::TryResumeRecovery()` currently reads:

```text
TrackedProcessId()
ForegroundIsTrackedProcess()
VerifierProcessId()
VerifierRunning()
```

and related compatibility/liveness semantics.

Resume recovery must reason from the current foreground-game state after a fresh foreground evaluation, not from a previously selected process that happened to survive suspend.

---

# 5. Target runtime contract

After R5, the active runtime model should read clearly as:

```text
ForegroundGameDetector
        |
        v
GameSessionController
    CurrentForegroundGameActive()
    CurrentForegroundGameProcessId()
        |
        +------> HUD visibility
        |
        +------> InGameOnly FPS target
        |
        +------> Graphics API probe target
```

The current game target must have these invariants:

```text
eligible Game A foreground
-> current game PID = A

Game A -> Explorer
-> current game PID = 0 immediately

Explorer -> known Game A
-> current game PID = A immediately after foreground admission

Game A -> Game B while A remains alive
-> current game PID = B when B becomes eligible
-> never fall back to A

Game A window loses fullscreen admission
-> current game PID = 0

PID reused with different creation time
-> old generation never remains current authority
```

---

# Part A — Rename the downstream game target contract

## 6. Rename `GameSessionHooks` target callbacks

Replace the old compatibility names:

```cpp
std::function<void(DWORD)> setCommittedProcess;
std::function<void()> clearCommittedProcess;
```

with explicit current-target names.

Recommended naming:

```cpp
std::function<void(DWORD)> setInGameForegroundProcess;
std::function<void()> clearInGameForegroundProcess;
```

or:

```cpp
setCurrentForegroundGameProcess
clearCurrentForegroundGameProcess
```

Pick one convention and use it consistently.

The important rule is:

> No active downstream hook should still contain the word `Committed` after this PR.

Update comments accordingly.

The hook must mean:

```text
set(pid)
= this PID is the current eligible foreground game target now

clear()
= there is no current eligible foreground game target now
```

It does **not** mean process lifetime ownership.

---

## 7. Rename `ProductionTelemetryController` state

Replace:

```cpp
DWORD committedProcessId_{};
void SetCommittedProcess(DWORD processId) noexcept;
void ClearCommittedProcess() noexcept;
```

with explicit current In-Game Only target state.

Recommended shape:

```cpp
DWORD inGameForegroundProcessId_{};

void SetInGameForegroundProcess(DWORD processId);
void ClearInGameForegroundProcess();
```

or an equivalent consistent name.

Prefer non-inline implementation if target transitions need FPS invalidation logic.

Do not leave compatibility aliases such as:

```cpp
SetCommittedProcess(...) { SetInGameForegroundProcess(...); }
```

unless a temporary compile bridge is absolutely required within the same commit. The final R5 diff should remove active downstream committed terminology.

---

## 8. Rename R4 bridge storage in `GameSessionController`

Current R4 member:

```cpp
std::optional<GameProcessInstance> bridgedEligibleProcess_;
```

R5 should stop treating this as a compatibility bridge and name it according to its real purpose.

Recommended:

```cpp
std::optional<GameProcessInstance> currentForegroundGameProcess_;
```

or:

```cpp
currentEligibleForegroundProcess_
```

This exact-process-generation value is useful because it prevents PID-reuse ambiguity.

Keep the full `GameProcessInstance`, not numeric PID only.

Do not replace it with a long-lived process handle.

---

# Part B — Explicit `GameSessionController` current-game API

## 9. Add current foreground game queries

Replace downstream use of:

```cpp
TrackedProcessId()
ForegroundIsTrackedProcess()
```

with APIs that state the actual game authority.

Recommended public API:

```cpp
bool CurrentForegroundGameActive() const noexcept;
DWORD CurrentForegroundGameProcessId() const noexcept;
```

Optional exact identity query if useful for resume/tests:

```cpp
std::optional<GameProcessInstance>
CurrentForegroundGameProcess() const noexcept;
```

Required semantics:

```text
CurrentForegroundGameActive()
-> true only when the latest foreground-first evaluation is Eligible
   and the stored current process matches that eligible exact process generation

CurrentForegroundGameProcessId()
-> eligible PID, otherwise 0
```

Do not derive these values from:

```text
HUD presentation visibility
FPS provider state
graphics API probe state
Steam RunningAppID
manual override
legacy GameDetectionCoordinator
```

The source of truth remains the R3 detector/current R4 target.

---

## 10. Keep `ForegroundTracker` compatibility internal for now

R7 is responsible for removing/simplifying the remaining tracker compatibility layer.

R5 may continue calling:

```cpp
foregroundTracker_.SetTrackedProcessId(...)
```

if existing resume/event infrastructure still requires it.

However:

- `App` should no longer use `ForegroundIsTrackedProcess()` as game authority;
- telemetry should not use tracker state;
- public current-game semantics should come from the foreground-first target.

Do not expand `ForegroundTracker` responsibilities in R5.

---

## 11. Rename/remove `ReleaseCommittedIfForegroundGone`

Current method:

```cpp
void ReleaseCommittedIfForegroundGone();
```

no longer performs a real committed-target release after R4. It validates whether the current exact process generation still exists and may trigger a fresh foreground evaluation.

Rename it to a semantic name such as:

```cpp
void RevalidateCurrentForegroundGame();
```

or:

```cpp
void ReconcileCurrentForegroundGameLiveness();
```

Do not keep `Committed` terminology.

If the method is no longer necessary once `App::ReconcileHudVisibility()` uses the explicit current-game state and all foreground/window events already drive evaluation, it may be reduced or removed **only if existing process-exit/liveness behavior remains covered**.

Do not add polling.

---

## 12. Rename `ClearCandidateIfNotCommitted`

Current HUD-disable path calls:

```cpp
gameSession_.ClearCandidateIfNotCommitted(L"hud-disabled");
```

After R4 this method already clears verifier/current bridge state rather than operating on a meaningful legacy candidate/commit distinction.

Rename it to the actual lifecycle action, for example:

```cpp
ClearForegroundGameRuntimeState(reason)
```

or:

```cpp
ResetForegroundGameSession(reason)
```

Required behavior remains:

```text
stop active renderer verification
clear current eligible foreground-game target
clear graphics target through hooks
clear compatibility tracker state as needed
reconcile HUD visibility
```

Do not reset `KnownGameProcessCache` merely because HUD is disabled unless there is an explicit existing reason. Positive game identity is process-generation knowledge, not presentation state.

---

# Part C — In-Game Only FPS target semantics

## 13. Preserve the high-level target split

The final FPS target mapping remains:

```text
HudVisibilityMode::Always
    -> AlwaysModeFpsTarget::TargetProcessId()

HudVisibilityMode::InGameOnly
    -> current eligible foreground game PID
```

Update `ResolveProductionFpsTargetPid` parameter names/tests if they still say committed.

Recommended signature semantics:

```cpp
DWORD ResolveProductionFpsTargetPid(
    HudVisibilityMode mode,
    DWORD alwaysForegroundProcessId,
    DWORD inGameForegroundProcessId) noexcept;
```

No fallback behavior is allowed.

Specifically forbidden:

```text
InGameOnly current target == 0
+ prior game still alive
-> use prior game
```

or:

```text
Always foreground target == 0
-> fall back to current game target
```

Each mode has exactly one authority.

---

## 14. Invalidate stale FPS immediately on In-Game target change

Current `FpsStaleHold` intentionally retains short same-PID misses for up to two seconds.

Keep that behavior for the **same current PID**.

But when the In-Game target changes:

```text
A -> 0
A -> B
0 -> B
```

old A FPS must never be displayed as B/Explorer FPS.

When `SetInGameForegroundProcess(newPid)` changes the target from a different PID:

```cpp
latestProcessFps_.reset();
fpsStaleHold_.Reset();
```

When target clears:

```cpp
latestProcessFps_.reset();
fpsStaleHold_.Reset();
(void)provider_.ReadProcess(0);
```

The exact provider reset placement may vary, but the externally visible invariant is mandatory:

> No old-game FPS survives a current-game PID change or target clear.

Do not clear valid same-PID stale hold merely because a redundant set for the same PID arrives.

Recommended transition helper:

```cpp
void ProductionTelemetryController::SetInGameForegroundProcess(DWORD processId)
{
    if (inGameForegroundProcessId_ == processId)
        return;

    inGameForegroundProcessId_ = processId;
    latestProcessFps_.reset();
    fpsStaleHold_.Reset();

    if (!processId)
        (void)provider_.ReadProcess(0);
}
```

Adapt as required by current provider/query ownership.

---

## 15. `SampleFps()` must remain target-only

Keep `ProductionTelemetryController::SampleFps()` ignorant of game detection internals.

It should only receive cached target state:

```cpp
const DWORD processId = ResolveProductionFpsTargetPid(
    visibilityMode_,
    alwaysFpsTarget_.TargetProcessId(),
    inGameForegroundProcessId_);
```

Do not add:

```text
GetForegroundWindow
game cache lookup
Steam lookup
Microsoft identity probing
process scanning
fallback PID search
```

to the FPS provider/controller sampling path.

---

## 16. Required FPS transition scenarios

Cover and preserve these exact behaviors:

### 16.1 Game A -> Explorer

```text
A Eligible
-> InGame target A
-> FPS A

Explorer foreground
-> detector Hidden
-> InGame target 0 immediately
-> latest FPS cleared
-> no stale A displayed
```

### 16.2 Explorer -> known Game A

```text
A already renderer/Microsoft known
-> foreground admission passes
-> Eligible immediately
-> InGame target A
-> FPS query resumes for A
```

No generic renderer re-verification should be required solely because of Alt+Tab.

### 16.3 Game A -> Game B while A remains alive

```text
A current target
B becomes admitted foreground
B unknown -> target clears while verification is pending
B verified -> target B
A remains background -> never used
```

Do not keep A FPS visible while B is being verified.

### 16.4 Same PID transient sample miss

```text
current eligible target stays A
API2 sample temporarily missing
-> existing same-PID FpsStaleHold behavior remains
```

---

# Part D — HUD visibility semantics

## 17. Update `App::ReconcileHudVisibility()`

Replace tracker-based authority:

```cpp
const bool foregroundGameActive = gameSession_.ForegroundIsTrackedProcess();
```

with explicit current-game authority:

```cpp
const bool foregroundGameActive =
    gameSession_.CurrentForegroundGameActive();
```

`HudController` should continue receiving only the boolean.

Do not pass process IDs or game evidence into HUD presentation.

The HUD visibility policy remains conceptually:

```text
Always
-> visible while HUD enabled unless lifecycle/manual override says otherwise

InGameOnly
-> visible only when CurrentForegroundGameActive()
   unless manual override explicitly overrides presentation visibility
```

Game identity is not inferred from telemetry availability.

---

## 18. Current target clear must reconcile visibility immediately

When current eligible game transitions to:

```text
Hidden
or
NeedsRendererVerification
```

R4 already clears the compatibility target and reconciles visibility.

Preserve that immediate behavior under the new names.

Examples:

```text
Game -> Explorer
-> InGame HUD hides immediately

Game fullscreen -> windowed/work-area-sized
-> admission fails
-> InGame HUD hides

Game A -> unknown Game B
-> A target clears
-> HUD remains hidden while B verifies
-> HUD shows only after B is eligible
```

Do not keep the HUD visible merely because production telemetry sampling is still active for a moment.

---

# Part E — Graphics API probe target

## 19. Graphics API target follows the same current game target

The graphics API probe must follow the current eligible foreground game, not process lifetime.

Required behavior:

```text
Eligible A
-> graphics probe target A

A -> Explorer
-> stop/clear A graphics probe target

Explorer -> known A
-> start/ensure A target again

A -> B
-> stop A target
-> start B only when B becomes Eligible
```

Do not probe B while B is merely `NeedsRendererVerification` unless current R4 behavior already intentionally does so; the authoritative graphics target should be the current eligible game.

Do not retain A because it remains alive.

The existing `StartGraphicsApiProbe`, `EnsureGraphicsApiProbe`, `StopGraphicsApiProbeIfTarget` APIs may keep their generic names because they describe the probe operation correctly.

Only game-target authority names need semantic migration.

---

## 20. Prevent stale graphics API display across game changes

`StopGraphicsApiProbe()` already clears:

```text
graphicsApiProcessId_
graphicsApiAttempts_
latestGraphicsApi_
```

Preserve this.

When switching A -> B, do not allow A's resolved API label to remain displayed for B while B's probe is pending.

Add/retain deterministic coverage if an existing graphics-target policy helper supports it.

---

# Part F — Visibility mode switching

## 21. Switching to `Always`

Preserve current behavior:

```text
mode becomes Always
-> resolve current foreground PID immediately
-> AlwaysModeFpsTarget adopts that PID
-> do not wait for next foreground event
```

Always mode remains fully independent of game detection for FPS targeting.

Do not make Always mode require:

```text
fullscreen admission
renderer verification
Microsoft identity
Steam context
```

---

## 22. Switching to `InGameOnly`

This needs an explicit fresh evaluation.

Current code changes mode and later calls `ReconcileHudVisibility()`, but R5 should not depend on an already cached/tracker result when entering In-Game Only.

Required flow when the mode actually changes to `InGameOnly`:

```text
HudController mode = InGameOnly
-> ProductionTelemetryController mode = InGameOnly
-> GameSessionController.ReevaluateForeground()
-> foreground-first detector evaluates current screen now
-> current target / verifier / HUD visibility reconcile from that result
```

Do not wait for:

```text
next EVENT_SYSTEM_FOREGROUND
next SHOW
next LOCATIONCHANGE
```

If the current foreground is an already-known admitted game, the HUD should become eligible immediately.

If it is an unknown admitted game, generic renderer verification should begin through the normal R4 path.

If it is Explorer/non-game, target remains 0.

---

## 23. Mode switching must clear inappropriate stale FPS

When changing visibility mode:

```text
Always -> InGameOnly
InGameOnly -> Always
```

continue clearing FPS/stale state so samples from the previous authority do not leak into the new mode.

Current `ProductionTelemetryController::SetVisibilityMode()` already resets:

```cpp
latestProcessFps_.reset();
fpsStaleHold_.Reset();
```

Preserve that behavior.

Mode switching must not clear positive known-game identity cache.

---

# Part G — Suspend / resume semantics

## 24. Resume must re-evaluate the actual current foreground

Current resume recovery begins with tracker reconciliation and then reads `TrackedProcessId()`.

R5 should make the new authority explicit.

At the beginning of a resume recovery attempt, after lifecycle gates allow it:

```text
reconcile foreground source if needed
-> GameSessionController.ReevaluateForeground()
-> read CurrentForegroundGameActive()
-> read CurrentForegroundGameProcessId()
-> reason from that current result
```

Do not restore authority to a pre-suspend game merely because:

```text
it is still alive
it was previously tracked
it was previously the graphics target
it was previously producing FPS
```

Example:

```text
before suspend: Game A eligible
while suspended/resume: Explorer becomes foreground
resume recovery
-> current foreground evaluation = Explorer Hidden
-> current game PID = 0
-> do not restart A graphics/FPS target
```

---

## 25. Rename resume-facing committed/liveness query

Current API:

```cpp
CommittedProcessAliveOrNone()
```

must not survive R5 under that name.

If still required by the recovery policy, rename/reframe it around the current exact foreground game process, for example:

```cpp
CurrentForegroundGameProcessAliveOrNone()
```

However prefer to simplify the recovery path so fresh foreground evaluation is the authority and liveness is only a guard, not a target selector.

Do not create a new sticky process restoration path under a new name.

---

## 26. Renderer verifier resume behavior

Preserve R4's renderer request model:

```text
active RendererVerificationRequest
requestId + exact GameProcessInstance
```

Resume may retain/restart verification only when it still corresponds to the current admitted unknown foreground process.

A verifier for a background process must not make that process the current target.

Do not weaken the R3/R4 stale completion safety:

```text
late completion may cache exact generation evidence
but cannot switch current foreground target
and cannot overwrite a newer PID generation
```

---

# Part H — Manual HUD override

## 27. Manual override remains presentation-only

Manual HUD override may force presentation visible/hidden according to the existing `HudController` policy.

It must not:

- mark a process as a game;
- change `KnownGameProcessCache`;
- set current foreground game PID;
- select an FPS PID;
- create Steam association;
- bypass hard `GameScreenAdmission` for game identity;
- change renderer verification evidence.

In other words:

```text
manual override
= presentation visibility override
!= game detection override
```

Preserve existing behavior and add a pure regression assertion if a suitable policy seam exists.

---

# Part I — F8 / HUD enable-disable lifecycle

## 28. F8 enable path should use current-game API

Current F8 enable code uses:

```cpp
if (const DWORD processId = gameSession_.TrackedProcessId())
    productionTelemetry_.StartGraphicsApiProbe(processId);
```

Replace this with the explicit current-game query:

```cpp
if (const DWORD processId =
        gameSession_.CurrentForegroundGameProcessId())
{
    productionTelemetry_.StartGraphicsApiProbe(processId);
}
```

or equivalent.

Do not use tracker semantics outside the game-session controller.

---

## 29. HUD disable path should clear runtime target, not known-game identity

On HUD disable:

```text
stop sampling
stop renderer verification
clear current foreground game target bridge/runtime target
stop graphics probe
hide/shutdown presentation as existing lifecycle requires
```

But do not globally erase the process-generation known-game cache solely because presentation was disabled.

If the game remains alive and the HUD is re-enabled later:

```text
fresh foreground evaluation
-> already-known admitted game may become Eligible immediately
```

This is consistent with the R2 cache purpose.

---

# Part J — Logging terminology

## 30. Remove misleading runtime `committed` terminology from the new active path

R6 will delete dormant legacy coordinator logs/files.

R5 should update logs/comments attached to the active downstream target path so they describe current semantics.

Preferred terms:

```text
foreground-game-target
in-game-foreground
target-set
target-clear
```

Examples:

```text
[GameDetection] foreground.target-set pid=1234
[GameDetection] foreground.target-clear oldPid=1234 reason=foreground
[PresentMonFPS] mode=InGameOnly targetPid=1234
[PresentMonFPS] mode=InGameOnly target-cleared
```

Do not spend this PR renaming every dormant legacy `Committed` log inside the old coordinator implementation. R6 owns that deletion.

The requirement is that the **active R4/R5 path** no longer communicates sticky commit semantics.

---

# Part K — Tests

## 31. Preserve all existing R1–R4 regression coverage

At minimum retain passing coverage for:

```text
GameScreenAdmission
KnownGameProcessCache
ForegroundGameDetector
GameSessionCutoverPolicy
MicrosoftGameTrigger
ProductionTargetPolicy
AlwaysModeFpsTarget
FPS stale hold
HUD presentation contract
suspend/resume policy
```

Do not weaken or remove a test merely because names change.

---

## 32. FPS target policy tests

Update existing `ResolveProductionFpsTargetPid` tests to use the new parameter terminology.

Required cases:

```text
Always + foreground A + in-game B
-> A

Always + foreground 0 + in-game B
-> 0

InGameOnly + foreground A + in-game B
-> B

InGameOnly + foreground A + in-game 0
-> 0
```

This explicitly proves there is no cross-mode fallback.

---

## 33. Current-game target transition tests

Add deterministic tests around the game-session policy seam for:

### A -> Explorer

```text
current eligible A
next Hidden
-> clear current game target
```

### A -> unknown B

```text
current eligible A
next NeedsRendererVerification(B)
-> clear A immediately
-> do not keep A while B verifies
```

### A -> eligible B

```text
current eligible A
next Eligible(B)
-> retarget B
```

### redundant A -> A

```text
current eligible A
next Eligible(same exact generation A)
-> do not perform destructive target reset
```

### PID reuse

```text
current PID 5000 generation A
next PID 5000 generation B
-> exact process instance change is treated as target change
```

---

## 34. FPS stale-state tests

Cover:

```text
same target PID + brief missing sample
-> stale hold allowed

target A -> B
-> A stale value invalid immediately

target A -> 0
-> A stale value invalid immediately

mode Always -> InGameOnly
-> old Always FPS does not survive authority change

mode InGameOnly -> Always
-> old in-game FPS does not survive authority change
```

Prefer pure helper/state tests rather than requiring live PresentMon in CI.

---

## 35. Visibility tests

Cover semantic use of `CurrentForegroundGameActive()`:

```text
InGameOnly + current active game
-> normal visibility policy may show

InGameOnly + no current game
-> normal visibility policy hides

Always + no current game
-> Always policy remains visible
```

Manual override behavior should remain unchanged.

Do not test presentation by altering production window styles.

---

## 36. Mode-switch tests

Add coverage proving:

```text
switch to InGameOnly
-> current foreground is re-evaluated immediately
```

At a pure orchestration seam, verify the call/order rather than relying on an actual WinEvent.

Also verify:

```text
switch to Always
-> current foreground PID is adopted for Always FPS immediately
```

---

## 37. Resume tests

Update suspend/resume tests so the expected authority is the fresh current foreground game, not old tracked/committed state.

Required regression sequence:

```text
pre-suspend Game A current
resume with Explorer foreground
-> no A target restoration
```

and:

```text
resume with same admitted/known Game A foreground
-> A can be restored through fresh foreground evaluation
```

and:

```text
resume with unknown admitted Game B
-> B requires normal renderer verification
-> A is not used as fallback
```

Do not add synchronization complexity for pathological interleavings; test normal lifecycle behavior.

---

## 38. Graphics target tests

Cover:

```text
Eligible A -> probe A
A clear -> probe cleared
Eligible B -> probe B
A still alive in background -> irrelevant
```

Ensure old graphics API label does not survive target clear/retarget.

---

# Part L — Real-device validation checkpoint

## 39. Repeat the R4 field scenarios after the semantic migration

R5 changes downstream FPS/HUD/graphics authority naming and invalidation behavior, so repeat a focused real-device test before proceeding to R6.

Use at least:

```text
Windows Terminal / ordinary desktop foreground
Diablo IV
Minecraft for Windows
Dave the Diver
Mafia: The Old Country
Explorer Alt+Tab
Steam Alt+Tab
Game Bar / QAM appearance
Game A -> Game B while A remains alive
```

Pay special attention to:

```text
HUD visibility
FPS target PID logs
graphics API label target
Alt+Tab return latency
A -> Explorer -> B stale FPS leakage
Minecraft LOCATIONCHANGE admission transition
no old/background game fallback
```

Expected results:

```text
WindowsTerminal never becomes current game target
Explorer/Steam foreground clears InGameOnly target immediately
known game return is immediate
unknown Game B clears Game A while B verifies
Game B becomes target only when eligible
Game A remains background and is never used as fallback
```

---

# 40. Explicit non-goals

Do **not** do the following in R5:

- delete `GameDetectionCoordinator`;
- delete legacy `GameDetectionState` / `CandidateDisposition` machinery;
- delete `GenericForegroundTrigger` solely because it is dormant;
- delete `SteamRunningAppTrigger` legacy adapter solely because it is dormant;
- perform the full PR6 state-machine cleanup;
- perform the full PR7 `ForegroundTracker` / `ProductionProcessLifetimeWatcher` cleanup;
- add polling or periodic process scanning;
- make Steam AppID map directly to a game PID;
- move game detection into `ProductionTelemetryController`;
- make PresentMon FPS sampling discover/select games;
- change `GameRenderVerifier` evidence meaning;
- change R1 fullscreen tolerance or admission rules without separate evidence;
- change HUD layout/style/opacity;
- change production HUD presentation contracts;
- change independent-flip / DirectComposition / premultiplied-alpha behavior.

Keep this PR focused on replacing the downstream target semantic contract and completing runtime target correctness.

---

# 41. Suggested implementation order

A practical order is:

```text
1. Rename ProductionTelemetryController committed target field/APIs.
2. Add immediate FPS invalidation on target change/clear.
3. Rename GameSessionHooks target callbacks.
4. Rename bridgedEligibleProcess_ to current foreground-game semantics.
5. Add CurrentForegroundGameActive / CurrentForegroundGameProcessId APIs.
6. Rename lifecycle methods that still expose Committed semantics.
7. Update App::MakeGameSessionHooks.
8. Update App::ReconcileHudVisibility to current-game API.
9. Update F8 enable path to current-game PID API.
10. Update mode-switch path with immediate InGameOnly re-evaluation.
11. Update suspend/resume recovery to fresh current-game evaluation.
12. Update logging/comments.
13. Update/add pure regression tests.
14. Run full build/test and HUD presentation contract tests.
15. Perform focused real-device R5 validation before R6.
```

Do not start by deleting legacy coordinator files. That is deliberately deferred.

---

# 42. Acceptance criteria

R5 is complete only when all of the following are true.

## Naming / architecture

- No active downstream game-target API or field uses `CommittedProcess` terminology.
- `ProductionTelemetryController` caches an explicitly named current In-Game foreground PID.
- `GameSessionController` exposes explicit current foreground-game active/PID queries.
- `App` no longer uses `ForegroundIsTrackedProcess()` as HUD game authority.
- The FPS provider remains PID-only and game-detection agnostic.

## Runtime behavior

- In-Game Only target is exactly the current eligible foreground game PID.
- Game -> Explorer clears target immediately.
- Game A -> unknown Game B clears A while B verifies.
- Game A -> eligible Game B retargets B.
- Background Game A is never a fallback target.
- Known game Alt+Tab return can become target immediately after admission.
- PID reuse cannot preserve old-generation authority.

## FPS

- Same-PID short misses still use the existing stale hold.
- PID change clears stale FPS immediately.
- Target clear clears stale FPS immediately.
- Mode changes cannot leak FPS from the previous authority.
- Always mode still follows current foreground PID regardless of game identity.

## Graphics API

- Probe target follows current eligible game PID.
- Clearing current game clears graphics target/label.
- Retargeting A -> B cannot display A graphics API as B.

## Visibility / lifecycle

- `App::ReconcileHudVisibility()` consumes explicit current-game boolean state.
- Switching to InGameOnly re-evaluates current foreground immediately.
- Resume recovery re-evaluates current foreground and never resurrects a stale pre-suspend game authority.
- Manual HUD override remains presentation-only.
- HUD disable does not destroy valid known-game process evidence solely due to presentation state.

## Safety / regression

- No production HUD presentation contract is changed.
- Click-through tests remain passing.
- No-activation tests remain passing.
- Topmost tests remain passing.
- Transparent hit-test tests remain passing.
- Independent-flip assertions remain passing.
- Premultiplied-alpha assertions remain passing.
- Production presentation contract assertions remain passing.
- R1/R2/R3/R4 game-detection regression tests remain passing.

---

# 43. Final architecture after R5

The intended active runtime path after this PR is:

```text
EVENT_SYSTEM_FOREGROUND
ProductionGameWindowSource
MicrosoftGameTrigger
SteamRunningAppIdSource
        |
        v
GameSessionController
        |
        v
ForegroundGameDetector
        |
        +--> Hidden
        |
        +--> NeedsRendererVerification
        |        |
        |        v
        |   GameRenderVerifier
        |        |
        |        v
        |   KnownGameProcessCache
        |        |
        |        +--> fresh foreground re-evaluation
        |
        +--> Eligible
                 |
                 v
      Current Foreground Game Target
                 |
        +--------+---------+
        |                  |
        v                  v
In-Game Only FPS      HUD visibility
        |
        +------------------+
        |
        v
Graphics API probe
```

Alongside it:

```text
Always mode FPS
-> AlwaysModeFpsTarget
-> raw current foreground PID
```

There is no sticky committed-game authority in the active runtime design.

After R5 is validated on device, proceed to R6 to delete the dormant legacy candidate/Ready/Committed state machine.
