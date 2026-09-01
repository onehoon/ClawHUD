# Work Order — Game Detection R6: Remove Legacy Sticky State Machine

Status: implementation work order  
Repository: `onehoon/ClawHUD`  
Implementation baseline: latest `main` after PR #205 diagnostic-only HUD logging  
Baseline commit at authoring time: `7a181df2383b3eb7995741f0e4e8dc132d726a4e`  
R5 squash merge: `6af9e36a69c21c79ccef2f3c724d94fef68a68be`  
Parent design: `docs/GAME_DETECTION_REDESIGN_PR_PLAN_2026-08-31.md`  
Field evidence: `docs/GAME_DETECTION_FIELD_ANALYSIS_2026-08-31.md`  
R4 work order: `docs/work-orders/GAME_DETECTION_R4_GAME_SESSION_CONTROLLER_CUTOVER.md`  
R5 work order: `docs/work-orders/GAME_DETECTION_R5_CURRENT_FOREGROUND_TARGET_SEMANTICS.md`

---

## 1. Goal

Remove the obsolete global candidate/commit game-detection architecture now that production authority has fully moved to the foreground-first path.

R4 made `ForegroundGameDetector` the production game-screen decision authority. R5 then completed the downstream semantic cutover so HUD visibility, In-Game-Only FPS targeting, graphics-API targeting, mode switching, and resume recovery follow the **current eligible foreground game** rather than a durable committed process.

The old coordinator implementation is therefore no longer allowed to remain as a second conceptual architecture in production code.

R6 must delete the dormant sticky machinery and coordinator adapters without changing current runtime behavior.

The target after this PR is:

```text
current foreground HWND/PID
    -> GameScreenAdmission
    -> exact GameProcessInstance
    -> KnownGameProcessCache
    -> ForegroundGameDetector
    -> GameSessionController current foreground game target
        -> HUD visibility
        -> In-Game-Only FPS target
        -> Graphics API target
```

There must be no alternate path based on:

```text
Idle / Armed / Verifying / Ready / Committed
candidateProcessId
global committed process
CandidateDisposition
ready-candidate commit
committed-target retention
```

This PR is primarily **deletion and dependency cleanup**, not a redesign of the foreground-first implementation.

---

## 2. Preconditions

### 2.1 R5 is already merged

The R5 foreground-target semantic cutover is present in `main`.

Current production code already exposes:

```cpp
bool CurrentForegroundGameActive() const noexcept;
DWORD CurrentForegroundGameProcessId() const noexcept;
```

and stores the exact current game generation as:

```cpp
std::optional<GameProcessInstance> currentForegroundGameProcess_;
```

`ProductionTelemetryController` already receives explicit In-Game-Only foreground target updates through:

```cpp
setInGameForegroundProcess
clearInGameForegroundProcess
```

Do not reintroduce any committed-process naming or fallback behavior while removing the legacy implementation.

### 2.2 R5 real-device checkpoint

The redesign plan intentionally places a hardware validation checkpoint after R5 and before destructive legacy cleanup.

Before finalizing R6, confirm that the R5 behavior has either already been validated or is explicitly being validated with the existing field matrix:

```text
Windows Terminal -> real game
Explorer <-> known game Alt+Tab
Steam <-> game Alt+Tab
Game A -> Game B while Game A remains alive
Microsoft/Xbox game
unknown generic game requiring renderer verification
window loses fullscreen-like admission
HUD Always <-> In-Game-Only switching
suspend/resume on Explorer
suspend/resume on the same admitted game
Game Bar / QAM interaction
```

R6 must not be used to hide an unresolved R5 behavioral regression.

### 2.3 PR #205 is diagnostic-only and out of scope

Current `main` also contains HUD window/Z-order diagnostic logging from PR #205.

That work is unrelated to R6. Preserve it unchanged.

Do not modify `HudPresentation`, its window styles, Z-order behavior, logging probes, or presentation lifecycle while performing this cleanup.

---

# 3. Non-negotiable HUD / VRR safety contract

R6 is game-detection architecture cleanup only.

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

Do not add a different overlay path, window recreation behavior, activation behavior, hit testing, swap/presentation mode, or Z-order workaround.

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

# 4. Current-main inventory relevant to R6

At baseline `7a181df...`, the active foreground-first path and the obsolete coordinator path coexist inside the same codebase.

## 4.1 Active production authority — keep

The live runtime path is already:

```text
GameSessionController::EvaluateCurrentForeground()
    -> GetForegroundWindow()
    -> ObserveGameScreen(...)
    -> ForegroundGameDetector::Evaluate(...)
    -> ApplyForegroundEvaluation(...)
```

`ApplyForegroundEvaluation()` currently performs the real target transitions:

```text
Eligible exact process generation
    -> currentForegroundGameProcess_
    -> foregroundTracker_.SetTrackedProcessId(pid)   // compatibility until R7
    -> setInGameForegroundProcess(pid)
    -> startGraphicsApiProbe(pid)
    -> startProductionSampling()

Hidden / NeedsRendererVerification after prior eligible target
    -> clear currentForegroundGameProcess_
    -> clear compatibility tracked PID
    -> clearInGameForegroundProcess()
    -> reconcile HUD
```

This behavior is authoritative and must remain.

## 4.2 Obsolete coordinator still owned by `GameSessionController`

Current header still includes and owns:

```cpp
#include "GameDetectionCoordinator.h"
#include "GenericForegroundTrigger.h"
#include "SteamRunningAppTrigger.h"

GameDetectionCoordinator gameDetectionCoordinator_;
SteamRunningAppTrigger steamRunningAppTrigger_{gameDetectionCoordinator_};
GenericForegroundTrigger genericForegroundTrigger_;
```

These are no longer required by the live foreground-first path.

## 4.3 Obsolete controller methods still compiled

The current `GameSessionController` still declares/implements legacy methods such as:

```text
ApplyProductionEvidence
HandleGameDetectionTransition
StartCandidateRenderVerification
ArmProductionProcessLifetime
TryCommitReadyCandidateFromForeground
ReleaseProductionGameCandidate
ClearProductionCandidate
ReleaseCommittedProductionTarget
```

`StartCandidateRenderVerification()` is now only a compatibility wrapper around the active `EnsureRenderVerification()` path and should not survive merely to preserve the old naming.

The remaining methods operate on `gameDetectionCoordinator_.Context()` and old candidate/commit policy.

## 4.4 `ProductionTargetPolicy` still mixes live helpers with sticky policy

Current `ProductionTargetPolicy.h/.cpp` still contains old coordinator-dependent policy:

```text
CandidateDisposition
CommittedTargetReleasePlan
CommittedTargetReleaseOps
GlobalTelemetryAction
DecideCandidateDisposition
ShouldCommitReadyCandidate
ShouldRetainCommittedProductionTarget
PlanCommittedTargetRelease
ApplyCommittedTargetReleasePlan
```

The same file also contains still-useful non-sticky functionality, including process image inspection/exclusion used by `GameScreenAdmission`, and the live resume helper:

```text
InspectProductionTargetProcessDetailed
IsRejectedProductionTargetImage
IsEligibleProductionTargetImage
ShouldReevaluateForegroundAfterResume
```

Do not delete useful low-level functionality just because it currently shares a file with obsolete policy.

## 4.5 Trigger/verifier classes still expose coordinator adapters

The following current APIs still depend on `GameDetectionCoordinator` even though the active path does not need those adapters:

```cpp
GenericForegroundTrigger::ApplyEvidence(...)
MicrosoftGameTrigger::ApplyEvidence(...)
GameRenderVerifier::ApplyRendererEvidence(...)
```

`SteamRunningAppTrigger` is itself a coordinator adapter.

Keep the useful behavior:

```text
MicrosoftGameTrigger::InspectWindowEvent
Microsoft identity probing
KnownGameProcessCache updates
GameRenderVerifier Start/Stop/event delivery
SteamRunningAppIdSource
ForegroundGameDetector::UpdateSteamSession
```

Remove only the coordinator-facing adapter surface.

## 4.6 Lifetime/tracked-PID compatibility is intentionally R7

`ProductionProcessLifetimeWatcher` and the selected-PID responsibilities of `ForegroundTracker` are scheduled for R7.

R6 must remove coordinator-specific lifetime policy that prevents deleting `GameDetectionCoordinator`, but must **not** broaden into the final R7 architecture cleanup.

In particular, do not use R6 as an excuse to redesign `ForegroundTracker`, process-lifetime handling, or App/controller event ownership.

---

# Part A — Delete the legacy coordinator architecture

## 5. Remove `GameDetectionCoordinator`

Delete the obsolete implementation and header if no non-historical production code depends on them after the adapter cleanup:

```text
src/ClawHUD/GameDetection/GameDetectionCoordinator.h
src/ClawHUD/GameDetection/GameDetectionCoordinator.cpp
```

This removes the old concepts:

```text
GameDetectionState
    Idle
    Armed
    Verifying
    Ready
    Committed

GameDetectionContext
GameDetectionEvidence
GameDetectionWake
GameDetectionTransition
GameDetectionTransitionResult
global candidateProcessId
global candidate generation
rendererObserved as coordinator state
commit/reset/candidate merge state transitions
```

There must be no replacement coordinator, renamed equivalent state machine, or compatibility shim that recreates the same sticky global authority under different names.

The foreground-first detector is already the replacement.

---

## 6. Remove legacy trace helpers

`GameDetectionTrace.h/.cpp` currently depend on coordinator enums and exist to stringify old coordinator state/trigger/transition concepts.

After deleting the legacy transition path, remove the trace implementation if it has no remaining live purpose:

```text
src/ClawHUD/GameDetection/GameDetectionTrace.h
src/ClawHUD/GameDetection/GameDetectionTrace.cpp
```

Also remove associated tests whose only purpose is formatting obsolete state-machine enums.

Do **not** remove current foreground-first runtime logs such as:

```text
[GameDetection] foreground.evaluate
[GameDetection] foreground.target-set
[GameDetection] foreground.target-clear
[GameDetection] foreground.hidden
[GameDetection] verifier.start-failed
[GameDetection] renderer.first-frame
[GameDetection] microsoft.evidence
[GameDetection] steam.session
```

Those logs describe the current architecture and remain useful.

---

# Part B — Clean `GameSessionController`

## 7. Remove coordinator ownership

Remove from `GameSessionController.h`:

```cpp
#include "GameDetectionCoordinator.h"
#include "GenericForegroundTrigger.h"
#include "SteamRunningAppTrigger.h"
```

and remove members that only serve the old architecture:

```cpp
GameDetectionCoordinator gameDetectionCoordinator_;
SteamRunningAppTrigger steamRunningAppTrigger_{gameDetectionCoordinator_};
GenericForegroundTrigger genericForegroundTrigger_;
```

Do not replace these members.

The controller should continue to own the current foreground-first components:

```text
ForegroundTracker                       // compatibility remains until R7
KnownGameProcessCache
ForegroundGameDetector
MicrosoftGameTrigger
ProductionGameWindowSource
ProductionProcessLifetimeWatcher        // final removal deferred to R7
GameRenderVerifier
SteamRunningAppIdSource
current Steam RunningAppID context
active RendererVerificationRequest
currentForegroundGameProcess_
```

## 8. Delete legacy methods

Remove obsolete declarations and implementations:

```text
ApplyProductionEvidence
HandleGameDetectionTransition
StartCandidateRenderVerification
ArmProductionProcessLifetime
TryCommitReadyCandidateFromForeground
ReleaseProductionGameCandidate
ClearProductionCandidate
ReleaseCommittedProductionTarget
```

Also remove any private helper, message branch, local struct, include, or log that exists only to support those functions.

Do not keep forwarding aliases such as:

```cpp
void StartCandidateRenderVerification()
{
    EnsureRenderVerification();
}
```

The final code should use the foreground-first names directly.

## 9. Preserve active controller behavior

Do not change the semantics of:

```text
EvaluateCurrentForeground
ApplyForegroundEvaluation
WindowEventAffectsCurrentForeground
HandleProductionForegroundChanged
HandleProductionWindowEvent
HandleMicrosoftGameEvidence
HandleGameRenderVerifierUpdate
HandleSteamRunningAppIdChanged
ReevaluateForeground
EnsureRenderVerification
StopRenderVerification
RevalidateCurrentForegroundGame
ResetForegroundGameSession
CurrentForegroundGameActive
CurrentForegroundGameProcessId
```

Small compile adaptations are acceptable after removing legacy types, but they must not alter target authority.

In particular:

```text
old background game remains alive
+ new foreground game appears
-> old game must never retain authority
```

and:

```text
unknown B replaces eligible A in foreground
-> A clears immediately
-> B verifies normally
-> no fallback to A
```

must remain true.

---

# Part C — Remove trigger/verifier coordinator adapters

## 10. Remove `SteamRunningAppTrigger`

The current foreground-first implementation already stores Steam context directly:

```cpp
steamRunningAppId_ = steamRunningAppIdSource_.GetRunningAppId();
foregroundGameDetector_.UpdateSteamSession(steamRunningAppId_);
```

and on change:

```text
read RunningAppID
-> update steamRunningAppId_
-> ForegroundGameDetector::UpdateSteamSession(...)
-> EvaluateCurrentForeground("steam-session")
```

Therefore `SteamRunningAppTrigger` is no longer required.

Delete if no live non-historical caller remains:

```text
src/ClawHUD/GameDetection/SteamRunningAppTrigger.h
src/ClawHUD/GameDetection/SteamRunningAppTrigger.cpp
```

Do not remove `SteamRunningAppIdSource`.

Steam RunningAppID remains context only; R6 must not turn it into direct game eligibility.

## 11. Remove `GenericForegroundTrigger`

The current foreground-first path no longer needs a generic coordinator candidate adapter.

`EvaluateCurrentForeground()` already obtains and evaluates the actual current foreground window through `ObserveGameScreen()` and `ForegroundGameDetector`.

Delete `GenericForegroundTrigger` if no current production caller remains:

```text
src/ClawHUD/GameDetection/GenericForegroundTrigger.h
src/ClawHUD/GameDetection/GenericForegroundTrigger.cpp
```

Do not recreate generic-candidate state elsewhere.

The process inspection/exclusion logic currently reachable through `GenericForegroundTrigger::Inspect()` is already used directly by `GameScreenAdmission` through `InspectProductionTargetProcessDetailed()` and must be preserved independently.

## 12. Remove `MicrosoftGameTrigger` coordinator adapter only

Keep:

```cpp
std::optional<MicrosoftGameTriggerEvidence>
InspectWindowEvent(const ProductionWindowEvent& event) noexcept;
```

Keep its exact process-generation-aware positive identity behavior and `KnownGameProcessCache` integration.

Remove only:

```cpp
static GameDetectionTransitionResult ApplyEvidence(
    GameDetectionCoordinator& coordinator,
    const MicrosoftGameTriggerEvidence& evidence) noexcept;
```

After this change, `MicrosoftGameTrigger.h` must no longer include `GameDetectionCoordinator.h`.

Microsoft evidence must continue to cause a fresh foreground evaluation rather than directly asserting target authority.

## 13. Remove `GameRenderVerifier` coordinator adapter only

Keep the production PresentMon API2 verifier lifecycle:

```text
Start
Stop
Running
ProcessId
Generation
FirstDisplayedFrame event delivery
```

Keep `ForegroundGameDetector::CompleteRendererVerification(...)` as the place where verified evidence enters the new architecture.

Remove only the obsolete adapter:

```cpp
static bool ApplyRendererEvidence(
    GameDetectionCoordinator& coordinator,
    const GameRenderVerifierEvent& event) noexcept;
```

After removal, `GameRenderVerifier.h` must no longer include `GameDetectionCoordinator.h`.

Do not redesign the verifier worker or PresentMon API2 integration in R6.

---

# Part D — Strip sticky policy from `ProductionTargetPolicy`

## 14. Remove coordinator-dependent sticky policy

Delete legacy policy types/functions that only support the old candidate/commit path:

```text
CandidateDisposition
GlobalTelemetryAction                    // if only used by committed-release plan
CommittedTargetReleasePlan
CommittedTargetReleaseOps
PlanCommittedTargetRelease
ApplyCommittedTargetReleasePlan
DecideCandidateDisposition
ShouldCommitReadyCandidate
ShouldRetainCommittedProductionTarget
```

Also remove tests that assert behavior such as:

```text
Committed / Ready candidates ignore later foreground candidates
live committed PID is retained
ready candidate may commit if foreground/alive
committed release plan drives old cleanup sequence
```

Those invariants are intentionally obsolete and must not survive as requirements.

## 15. Preserve live low-level inspection/exclusion behavior

`GameScreenAdmission` currently depends on:

```cpp
InspectProductionTargetProcessDetailed(processId)
```

which provides:

```text
Unavailable
Excluded
Eligible
```

and the centralized executable exclusion list includes the demonstrated false-positive set such as:

```text
windowsterminal.exe
runtimebroker.exe
dllhost.exe
backgroundtaskhost.exe
werfault.exe
crashreportclient.exe
```

Preserve this behavior exactly unless a separate bug is discovered.

Acceptable implementation options:

1. leave the still-live process inspection helpers in a stripped `ProductionTargetPolicy.h/.cpp` with the coordinator dependency removed; or
2. move them to a small neutral process-inspection component if that produces a materially cleaner dependency graph.

Do **not** duplicate the exclusion list.

Do **not** move it into title-based/fuzzy game detection.

Do **not** change the exclusion policy as part of cleanup unless required for compile correctness.

## 16. Preserve live resume policy

`App.cpp` still uses:

```cpp
ShouldReevaluateForegroundAfterResume(hudEnabled, recovered)
```

Keep that behavior or move the helper to a more appropriate existing policy location.

Do not accidentally delete it when stripping `ProductionTargetPolicy`.

The R5 invariant remains:

```text
resume recovery completes
-> fresh foreground evaluation
-> do not resurrect a pre-suspend game solely because it is alive
```

## 17. Remove other tests-only legacy helpers when safe

At current baseline, helpers such as the following appear to be retained primarily by old policy tests rather than the active production path:

```text
ShouldRestartGraphicsApiProbe
ShouldConsiderForegroundProductionTarget
```

Re-check current call sites while implementing.

If a helper has no production caller and only protects obsolete architecture, remove it and its test rather than preserving dead API surface.

If a helper still has a real current caller, keep it and rename parameters/comments away from `committedProcessId` terminology where necessary.

Do not delete live behavior merely to maximize line removal.

---

# Part E — Lifetime compatibility boundary with R7

## 18. Do not perform the full R7 cleanup

R7 is responsible for removing the remaining tracked-PID / single-lifetime compatibility layer.

R6 must **not** redesign or remove the `ForegroundTracker` implementation merely because old coordinator state is gone.

The R5 compatibility calls may remain for now:

```cpp
foregroundTracker_.SetTrackedProcessId(processId);
foregroundTracker_.SetTrackedProcessId(0);
foregroundTracker_.Reconcile();
```

`App` must continue using `CurrentForegroundGameActive()` as the game authority, not tracker match state.

## 19. Decouple `ProductionProcessLifetimeWatcher` from coordinator types

`ProductionProcessLifetime.h` currently includes `GameDetectionCoordinator.h` because it also declares old policy:

```text
ProductionProcessExitAction
DecideProductionProcessExit(GameDetectionContext, ...)
```

Those coordinator-dependent policy pieces belong to the obsolete candidate/committed architecture and should be removed in R6 if required to eliminate the coordinator dependency.

However, do not turn R6 into the final watcher redesign/removal.

Preferred transitional state:

```text
ProductionProcessLifetimeWatcher class
    -> may remain as a standalone lifetime utility until R7
    -> no dependency on GameDetectionCoordinator types

old candidate/committed exit policy
    -> deleted
```

If removing the old controller candidate/commit path makes some `kProductionProcessExit` plumbing provably unreachable, remove only the minimal coordinator-specific plumbing required for a clean build. Avoid broader tracker/lifetime architecture work; leave final simplification to R7.

---

# Part F — Build-system and test cleanup

## 20. Update production source lists

Remove deleted legacy sources from `CMakeLists.txt`, including as applicable:

```text
GameDetectionCoordinator.cpp
GameDetectionTrace.cpp
SteamRunningAppTrigger.cpp
GenericForegroundTrigger.cpp
```

Do not accidentally remove current R1–R5 sources:

```text
GameScreenAdmission.cpp
GameProcessInstance.cpp
KnownGameProcessCache.cpp
ForegroundGameDetector.cpp
GameSessionCutoverPolicy.cpp
MicrosoftGameTrigger.cpp
ProductionGameWindowSource.cpp
GameRenderVerifier.cpp
SteamRunningAppIdSource.cpp
```

`ProductionProcessLifetime.cpp` remains until R7 unless the implementation proves it has already become completely unused and removing it is necessary for compile correctness. Prefer preserving the planned R6/R7 boundary.

## 21. Delete obsolete test executables/files

Delete tests whose only purpose is validating the removed sticky state machine or coordinator adapters.

Expected candidates include all or part of:

```text
tests/GameDetectionCoordinatorTests.cpp
tests/GameDetectionTraceTests.cpp
tests/SteamRunningAppTriggerTests.cpp
tests/GenericForegroundTriggerTests.cpp
```

Also remove obsolete coordinator-adapter assertions from:

```text
tests/MicrosoftGameTriggerTests.cpp
tests/GameRenderVerifierTests.cpp
tests/ProductionTargetPolicyTests.cpp
```

Do not blindly delete entire mixed-purpose test files if they still cover live behavior.

## 22. Preserve foreground-first behavioral coverage

Keep and, where needed, strengthen tests around the current architecture:

```text
GameScreenAdmissionTests
GameProcessInstance / KnownGameProcessCache coverage
ForegroundGameDetectorTests
GameSessionCutoverPolicyTests
MicrosoftGameTrigger live identity/cache tests
GameRenderVerifier live event/lifecycle tests
AlwaysModeFpsTargetTests
PresentMonTelemetryProviderTests
ProductionTelemetryController-related target transition coverage
HUD presentation contract/lifecycle tests
```

The removal of old tests must not reduce coverage for actual user-visible behavior.

Where an obsolete coordinator scenario test expressed a still-valid user behavior, migrate that expectation to a foreground-first test rather than deleting the behavioral coverage.

Examples:

```text
WindowsTerminal rejected, later real game accepted
A -> Explorer clears A
A -> unknown B clears A while B verifies
A -> eligible B changes target even while A remains alive
known game Alt+Tab return is immediate
PID reuse with different creation time does not inherit prior authority
stale renderer completion cannot directly show HUD
Steam context alone cannot make a process eligible
```

---

# 23. Required code-search cleanup

Before considering R6 complete, search the active source/build/test tree for obsolete symbols.

The final active production code must not reference:

```text
GameDetectionCoordinator
GameDetectionState
GameDetectionContext
GameDetectionTransition
GameDetectionWake
CandidateDisposition
DecideCandidateDisposition
ShouldCommitReadyCandidate
ShouldRetainCommittedProductionTarget
CommittedTargetReleasePlan
ApplyCommittedTargetReleasePlan
TryCommitReadyCandidateFromForeground
ReleaseCommittedProductionTarget
ReleaseProductionGameCandidate
ClearProductionCandidate
ApplyProductionEvidence
HandleGameDetectionTransition
```

Likewise, there must be no current runtime log whose semantics assert the obsolete architecture, such as:

```text
transition old=Ready new=Committed
committed pid=
candidate.start
candidate.replace
candidate.merge
steam.armed
```

Historical design/work-order documents may still mention these terms to explain the migration history. Do not rewrite historical evidence merely to make repository-wide text search return zero.

The requirement applies to **active source, build configuration, and active tests**.

---

# 24. Behavior that must remain unchanged

R6 must not alter these runtime contracts.

## 24.1 Current foreground remains sole final authority

```text
eligible A foreground
-> A is current game target

A -> Explorer
-> target 0 immediately

Explorer -> known A
-> A becomes current immediately after admission

A -> B while A remains alive
-> B is evaluated independently
-> A never blocks B

A -> unknown B
-> A clears immediately
-> B renderer verification runs
-> A does not remain visible/targeted while B verifies
```

## 24.2 Known-game cache remains process-generation aware

```text
same PID + same creation time
-> remembered evidence may be reused

same numeric PID + new creation time
-> old evidence does not authorize new process generation
```

Do not convert cache identity back to numeric PID only.

## 24.3 Renderer completion remains supporting evidence only

An old asynchronous verifier completion may update known evidence for its exact process generation when valid, but must never directly show HUD or select the FPS target.

The completion path remains:

```text
renderer evidence
-> cache exact process generation
-> fresh current foreground evaluation
-> full admission again
-> Eligible only if current foreground now qualifies
```

## 24.4 Steam remains context only

`RunningAppID != 0` is not proof that the current foreground process is a game.

It must not bypass screen admission or renderer/Microsoft evidence requirements.

## 24.5 Microsoft evidence remains positive process evidence

Exact Microsoft/Xbox identity remains strong positive evidence for the exact process generation.

It does not bypass current foreground/admission authority.

## 24.6 Always mode remains independent from game detection

R6 must not change:

```text
HudVisibilityMode::Always
-> raw current foreground FPS target via AlwaysModeFpsTarget
```

Do not route Always mode through `ForegroundGameDetector`.

## 24.7 In-Game-Only FPS stale-sample safety remains

Target transitions must continue to clear old target samples/query state as implemented by R5.

Do not reintroduce cross-mode fallback or same-numeric-PID query reuse bugs.

---

# 25. Explicit non-goals

Do not implement any of the following in R6:

- new game-detection heuristics;
- new executable exclusions unless required by a separately demonstrated bug;
- fuzzy title matching;
- polling/periodic foreground scans;
- broad `EnumWindows` scanning;
- timer-based game detection;
- PresentMon verifier redesign;
- FPS-provider game-awareness;
- HUD renderer/presentation changes;
- opacity changes;
- HUD Z-order fixes from PR #205 diagnostics;
- `ForegroundTracker` final simplification;
- final removal/redesign of `ProductionProcessLifetimeWatcher`;
- Diag app refactoring;
- R7 cleanup beyond compile-only decoupling required to delete coordinator types.

If a cleanup requires changing foreground-first behavior, stop and isolate the issue instead of silently folding a behavioral redesign into R6.

---

# 26. Required validation

## 26.1 Static dependency validation

Confirm:

```text
GameSessionController.h no longer includes GameDetectionCoordinator.h
MicrosoftGameTrigger.h no longer includes GameDetectionCoordinator.h
GameRenderVerifier.h no longer includes GameDetectionCoordinator.h
ProductionTargetPolicy.h no longer requires GameDetectionCoordinator.h
ProductionProcessLifetime.h no longer requires GameDetectionCoordinator.h
```

assuming the corresponding legacy policy has been removed as specified.

Confirm the deleted coordinator source is absent from production and test CMake targets.

## 26.2 Build

Run the normal Release x64 MSVC build.

No new warnings should be introduced.

## 26.3 Tests

Run the full normal CTest suite.

The existing live-desktop `ClawHUD.DiagWinEventTests` exception remains unrelated to R6; do not modify that diagnostic test merely to make R6 green.

All other relevant tests must pass.

At minimum confirm current coverage for:

```text
GameScreenAdmission
KnownGameProcessCache
ForegroundGameDetector
GameSessionCutoverPolicy
MicrosoftGameTrigger live behavior
GameRenderVerifier live behavior
AlwaysModeFpsTarget
PresentMonTelemetryProvider target release/rebind behavior
HUD presentation contract
HUD presentation lifecycle
```

## 26.4 Focused behavior smoke validation

Because R6 is intended to be behavior-preserving deletion, a short real-device smoke is enough if the full R5 checkpoint was already completed:

```text
1. In-Game-Only + Explorer -> HUD hidden
2. launch/foreground a known game -> HUD appears
3. Alt+Tab to Explorer -> HUD hides immediately
4. return to known game -> HUD restores immediately
5. foreground Windows Terminal -> never becomes sticky authority
6. foreground another game while first game remains alive -> new game may become target
7. switch Always <-> In-Game-Only -> no stale old-game FPS
```

Any regression here is a blocker because R6 should not change behavior.

---

# 27. Acceptance criteria

R6 is complete only when all of the following are true:

- [ ] `GameDetectionCoordinator` production implementation is removed.
- [ ] `Idle / Armed / Verifying / Ready / Committed` are no longer active production game-detection states.
- [ ] `GameSessionController` no longer owns a coordinator or coordinator trigger adapters.
- [ ] legacy controller candidate/commit methods are deleted.
- [ ] `SteamRunningAppTrigger` coordinator adapter is removed.
- [ ] `GenericForegroundTrigger` coordinator adapter is removed.
- [ ] `MicrosoftGameTrigger` no longer exposes a coordinator `ApplyEvidence` adapter.
- [ ] `GameRenderVerifier` no longer exposes a coordinator `ApplyRendererEvidence` adapter.
- [ ] sticky `ProductionTargetPolicy` APIs are removed.
- [ ] reusable process inspection/exclusion behavior remains intact.
- [ ] live resume re-evaluation policy remains intact.
- [ ] `ProductionProcessLifetimeWatcher` no longer depends on coordinator types, while final watcher cleanup remains R7 scope.
- [ ] obsolete coordinator-only tests and CMake targets are removed.
- [ ] equivalent foreground-first user-behavior coverage remains.
- [ ] current foreground HWND/PID remains the sole final game authority.
- [ ] Steam remains context only.
- [ ] Microsoft/renderer evidence remains process-generation scoped.
- [ ] no old game PID can block evaluation of a later foreground game.
- [ ] no new polling, timers, broad scans, or per-frame game-detection work is added.
- [ ] Always mode remains independent from game detection.
- [ ] In-Game-Only target/FPS/graphics semantics from R5 remain unchanged.
- [ ] PR #205 HUD diagnostic instrumentation remains untouched.
- [ ] HUD/VRR presentation contract is unchanged.
- [ ] Release x64 build succeeds.
- [ ] full applicable CTest suite passes.

---

# 28. Expected end-state before R7

After R6 the runtime architecture should read approximately:

```text
ProductionGameWindowSource --------+
ForegroundTracker WinEvent --------+----> GameSessionController
SteamRunningAppIdSource -----------+            |
MicrosoftGameTrigger --------------+            v
                                           EvaluateCurrentForeground
                                                   |
                                            GameScreenAdmission
                                                   |
                                            GameProcessInstance
                                                   |
                              +--------------------+--------------------+
                              |                                         |
                    KnownGameProcessCache                     GameRenderVerifier
                              |                                         |
                              +--------------------+--------------------+
                                                   |
                                           ForegroundGameDetector
                                                   |
                                       currentForegroundGameProcess_
                                                   |
                              +--------------------+--------------------+
                              |                    |                    |
                              v                    v                    v
                       HUD visibility      In-Game-Only FPS      Graphics API target
```

The following compatibility pieces may still remain temporarily and are R7 work:

```text
ForegroundTracker selected/tracked PID semantics
ProductionProcessLifetimeWatcher final removal/simplification
associated compatibility plumbing
```

But there must be **no second sticky game-detection state machine** remaining beside the foreground-first detector.

---

# 29. Handoff note for R7

Do not begin R7 in the same PR.

Once R6 is merged and behavior remains stable, R7 can finish the architectural cleanup by removing the remaining tracked-PID / single-lifetime compatibility layer so `ForegroundTracker` becomes only the event-source responsibility actually needed by the final design.

R6 should leave that final step obvious and mechanically small rather than mixing it into this deletion PR.
