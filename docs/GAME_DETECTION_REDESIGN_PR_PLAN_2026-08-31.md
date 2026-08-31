# ClawHUD Game Detection Redesign — Detailed PR Migration Plan

Status: implementation planning reference  
Date: 2026-08-31  
Repository: `onehoon/ClawHUD`  
Baseline main: `f0834084a4d883df4977789cb70508b538be1827`  
Companion evidence: `docs/GAME_DETECTION_FIELD_ANALYSIS_2026-08-31.md`

---

## 1. Purpose

This document converts the 2026-08-31 field-analysis conclusions into an implementation sequence against the current production code.

The goal is not to incrementally patch the existing sticky `Committed` game PID model. The goal is to migrate production In-Game Only detection to a foreground-first design where:

1. the **current foreground HWND/PID** is the only final authority for HUD visibility and In-Game Only FPS targeting;
2. Steam `RunningAppID` remains session context only;
3. Microsoft/Xbox game identity remains strong positive per-process evidence;
4. API2 renderer verification remains supporting evidence for unknown generic processes;
5. known game evidence is cached per process generation so Alt+Tab return is immediate;
6. no old game PID can remain globally authoritative and block a later real game;
7. production game detection remains event-driven and does not introduce polling;
8. the HUD presentation / VRR safety contract is untouched.

The project is still pre-release, so compatibility with the current internal state-machine shape is not a product requirement. The implementation should favor a clean end-state over preserving unnecessary transitional abstractions.

---

## 2. Current production architecture at the planning baseline

### 2.1 Current game state is globally candidate/commit oriented

`GameDetectionCoordinator` currently owns one global context:

```cpp
GameDetectionState state;
DWORD candidateProcessId;
HWND candidateWindow;
std::uint32_t steamAppId;
bool microsoftGameIdentity;
bool rendererObserved;
GameDetectionEvidence evidence;
```

with states:

```text
Idle
Armed
Verifying
Ready
Committed
```

This model assumes one globally authoritative candidate/committed process at a time.

### 2.2 The sticky behavior is explicit policy

`ProductionTargetPolicy::DecideCandidateDisposition()` rejects later candidates when the current context is `Committed` or `Ready`.

Conceptually:

```cpp
if (context.state == GameDetectionState::Committed)
    return CandidateDisposition::Ignore;
if (context.state == GameDetectionState::Ready)
    return CandidateDisposition::Ignore;
```

`GameSessionController::HandleProductionForegroundChanged()` reinforces that model: when the committed PID is still alive, it keeps the existing target and returns before considering the new foreground process.

This is the structural failure demonstrated by the field run where `WindowsTerminal.exe` became committed and later real games were ignored.

### 2.3 HUD visibility is still tied to a tracked committed process

`App::ReconcileHudVisibility()` currently derives In-Game Only activity from:

```cpp
gameSession_.ForegroundIsTrackedProcess()
```

The `ForegroundTracker` therefore performs two unrelated jobs:

1. foreground event observation;
2. match tracking against one selected PID.

The redesign should retain the event-source role but remove the requirement that one previously committed PID remain the visibility authority.

### 2.4 In-Game Only FPS is still tied to `committedProcessId_`

`ProductionTelemetryController` currently keeps:

```cpp
DWORD committedProcessId_{};
```

and `SampleFps()` resolves:

```text
Always      -> AlwaysModeFpsTarget current foreground PID
InGameOnly  -> committedProcessId_
```

The Always path is already conceptually correct and should not be redesigned.

The In-Game Only input needs to become the current eligible foreground game PID.

### 2.5 Production API2 verifier is reusable

`GameRenderVerifier` should remain.

It uses the shared `PresentMonTelemetryProvider`, acquires a process lease with `BeginGameRenderVerification(pid)`, and waits for PID-filtered displayed-frame evidence through frame telemetry.

This is different from the standalone diagnostic multi-PID dynamic-query behavior. The production verifier remains useful as supporting proof that an unknown generic fullscreen foreground process is genuinely rendering.

The redesign should change who owns verification state and how completion is interpreted, not replace the PresentMon frame-verification path.

### 2.6 Microsoft identity infrastructure is already useful

`MicrosoftGameTrigger` already:

- probes relevant top-level window events;
- requires readable `MicrosoftGame.config` executable match;
- identifies process instances using PID + creation time;
- caches positive identity by process generation.

This should be retained and generalized into the new known-game cache rather than discarded.

### 2.7 Production is already Per-Monitor-V2 DPI aware

`src/ClawHUD/main.cpp` sets:

```cpp
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
```

before creating `App`.

Therefore the standalone Diag 150% DPI mismatch does not require a new production DPI initialization change. The redesign only needs to ensure geometry APIs are compared in one physical coordinate space.

---

## 3. Target architecture

The target design separates three responsibilities that are currently collapsed into one coordinator context.

```text
                   Platform / Session Context
                  +---------------------------+
                  | Steam RunningAppID        |
                  | Microsoft game evidence   |
                  +-------------+-------------+
                                |
                                | supporting evidence only
                                v

WinEvent sources ------------------------------+
  EVENT_SYSTEM_FOREGROUND                      |
  EVENT_OBJECT_SHOW                            |
  EVENT_OBJECT_LOCATIONCHANGE                  |
  EVENT_OBJECT_HIDE                            |
  EVENT_OBJECT_DESTROY                         |
                                                v
                                    +------------------------+
                                    | ForegroundGameDetector |
                                    +-----------+------------+
                                                |
                                     GameScreenAdmission
                                                |
                           +--------------------+-------------------+
                           |                                        |
                           v                                        v
                 Known game process                     Unknown generic process
                 Microsoft identity                     same-PID API2 verifier
                 or renderer-verified                             |
                           |                                      |
                           +--------------------+-----------------+
                                                |
                                      final foreground recheck
                                                |
                                                v
                                  CurrentInGameForeground
                                         HWND + PID
                                                |
                              +-----------------+----------------+
                              |                                  |
                              v                                  v
                         HUD visibility                       FPS target
```

The critical invariant is:

> A process may be remembered as a known game while it is in the background, but only the **currently eligible foreground game screen** may drive HUD visibility or the In-Game Only FPS target.

---

## 4. Proposed migration strategy

Use seven focused PRs.

The PR count is driven by reviewability and dependency order, not by a permanent architectural requirement.

Approximate review sizing is intentionally small, but future work orders should describe the implementation scope rather than impose an arbitrary LOC quota on the implementer.

Recommended sequence:

```text
PR1  Screen admission + WinEvent foundation
PR2  Process-generation-aware known-game cache
PR3  New foreground-first detector core
PR4  Production GameSessionController cutover
     -> real-device validation checkpoint
PR5  HUD / FPS / graphics target semantic cutover
     -> real-device validation checkpoint
PR6  Remove legacy sticky coordinator/state policy
PR7  Remove compatibility lifetime/tracked-PID layer
```

Do not combine the first production cutover with all legacy deletion. A transitional compatibility bridge makes failures easier to isolate.

---

# PR1 — Foreground Game Screen Admission Foundation

## 5. Goal

Introduce one authoritative, testable policy for answering:

> Does the current HWND/PID represent a usable fullscreen-like foreground screen candidate?

Also extend the production window event source with the events proven necessary by field data.

This PR should establish foundation without switching the current production detector to the new behavior yet.

## 6. New component: `GameScreenAdmission`

Suggested files:

```text
src/ClawHUD/GameDetection/GameScreenAdmission.h
src/ClawHUD/GameDetection/GameScreenAdmission.cpp
```

Suggested result model:

```cpp
enum class GameScreenRejectReason
{
    None,
    NoWindow,
    NotTopLevel,
    NotVisible,
    Minimized,
    Cloaked,
    ProcessUnavailable,
    ExcludedExecutable,
    NoMonitor,
    NotFullscreenLike,
};

struct GameScreenAdmissionResult
{
    bool admitted{};
    GameScreenRejectReason reason{};
    HWND window{};
    DWORD processId{};
    std::wstring imageName;
    RECT windowBounds{};
    RECT monitorBounds{};
};
```

The exact type names may change, but rejection reasons are worth keeping because production debug logs should explain why a foreground candidate was rejected.

## 7. Admission checks

Recommended order:

```text
HWND exists
-> current top-level/root window validity
-> IsWindowVisible
-> !IsIconic
-> !DWM cloaked
-> process ID resolves
-> process image resolves
-> high-confidence executable exclusion passes
-> monitor resolves
-> physical frame/window bounds resolve
-> window covers monitor within tolerance
```

Use `DWMWA_EXTENDED_FRAME_BOUNDS` where practical, with a safe fallback to `GetWindowRect`.

Use `MonitorFromWindow(..., MONITOR_DEFAULTTONEAREST)` and `GetMonitorInfo` for current monitor bounds.

## 8. Fullscreen-like tolerance

Start with:

```cpp
constexpr LONG kFullscreenTolerancePx = 8;
```

and compare each edge against the physical monitor rectangle.

Field cases that must pass/fail:

```text
monitor 0,0,1920,1200
window  0,0,1920,1200          -> PASS
window -3,-3,1923,1203         -> PASS
window  0,0,1920,1128          -> FAIL
```

Do not compare against `rcWork` for fullscreen admission.

## 9. Extend `ProductionGameWindowSource`

Current observed events:

```text
CREATE
SHOW
DESTROY
```

Add:

```text
HIDE
LOCATIONCHANGE
```

Update:

```cpp
enum class ProductionWindowEventType
{
    Create,
    Show,
    Hide,
    LocationChange,
    Destroy,
};
```

Keep the source event-driven.

Do not add polling, `EnumWindows` loops, timers, or broad process scans.

## 10. Exclusion changes

At minimum add the demonstrated false-positive:

```text
windowsterminal.exe
```

Also centralize clearly non-game helper/system executables observed in the field run if not already covered:

```text
runtimebroker.exe
dllhost.exe
backgroundtaskhost.exe
werfault.exe
crashreportclient.exe
```

Do not add fuzzy title-based matching.

## 11. PR1 tests

Add deterministic tests for:

- edge tolerance;
- physical fullscreen vs work-area maximized window;
- minimized rejection;
- cloaked rejection;
- unavailable process rejection;
- excluded executable rejection;
- `EVENT_OBJECT_HIDE` mapping;
- `EVENT_OBJECT_LOCATIONCHANGE` mapping;
- top-level filtering remains intact;
- queue behavior remains bounded.

## 12. PR1 non-goals

Do not yet:

- replace `GameDetectionCoordinator`;
- change HUD visibility;
- change FPS target semantics;
- change Steam behavior;
- change renderer verification;
- modify HUD presentation.

---

# PR2 — Process-Generation-Aware Known Game Cache

## 13. Goal

Introduce reusable storage for strong or verified game evidence without making any cached PID globally authoritative.

The cache exists to avoid repeated expensive identity/render verification and to make Alt+Tab return immediate.

## 14. Process instance identity

Generalize the process-generation concept already used by `MicrosoftGameTrigger`.

Suggested type:

```cpp
struct GameProcessInstance
{
    DWORD processId{};
    ULONGLONG creationTime{};

    friend bool operator==(const GameProcessInstance&, const GameProcessInstance&) = default;
};
```

Suggested query helper:

```cpp
std::optional<GameProcessInstance> QueryGameProcessInstance(DWORD pid) noexcept;
```

Use `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` + `GetProcessTimes`.

Never key durable game evidence by numeric PID alone.

## 15. Known-game evidence model

Suggested minimal form:

```cpp
struct KnownGameEvidence
{
    bool microsoftGameIdentity{};
    bool rendererVerified{};
    bool observedDuringSteamSession{};
};
```

Do not treat `observedDuringSteamSession` as proof of identity. It is context only.

Suggested cache responsibilities:

```cpp
class KnownGameProcessCache
{
public:
    void MarkMicrosoftGame(const GameProcessInstance& process);
    void MarkRendererVerified(const GameProcessInstance& process);
    void MarkObservedInSteamSession(const GameProcessInstance& process);

    std::optional<KnownGameEvidence> Lookup(const GameProcessInstance& process) const;
    bool IsKnownGame(const GameProcessInstance& process) const;
    void Remove(const GameProcessInstance& process);
    void RemovePidIfGenerationChanged(DWORD pid, ULONGLONG currentCreationTime);
    void Clear();
};
```

Implementation can be map/unordered_map based.

## 16. Microsoft trigger integration

`MicrosoftGameTrigger` already keeps a positive process-generation cache.

Prefer converging on one shared process-instance abstraction so the codebase does not maintain two incompatible creation-time helpers.

After positive Microsoft evidence:

```text
MicrosoftGame.config exact executable match
-> KnownGameProcessCache.MarkMicrosoftGame(instance)
```

The trigger should continue emitting its event; production cutover occurs later.

## 17. Cache lifetime policy

A stale entry must never survive PID reuse.

Acceptable strategies:

1. creation-time revalidation on lookup; or
2. process exit observer removes exact generation; plus creation-time check on lookup as defense.

For the first implementation, creation-time validation on lookup is sufficient and keeps the cache simple.

Do not build one watcher thread/handle per known game solely to preserve cache hygiene.

## 18. PR2 tests

Cover:

- same PID + same creation time -> cache hit;
- same PID + different creation time -> miss;
- multiple known games may coexist;
- marking Microsoft + renderer merges evidence;
- removing one process does not remove another;
- Steam context does not make `IsKnownGame()` true by itself unless the intended API explicitly defines that behavior otherwise.

## 19. PR2 non-goals

No production target cutover yet.

---

# PR3 — Foreground-First Detector Core

## 20. Goal

Build the new decision core beside the existing coordinator.

It should have no global `Committed` state and no rule that a live older game blocks evaluation of a new foreground PID.

Suggested component:

```text
ForegroundGameDetector.h/.cpp
```

## 21. Detector responsibilities

Inputs:

- current foreground HWND/PID;
- `GameScreenAdmission` result;
- process-generation identity;
- `KnownGameProcessCache`;
- current Steam session context;
- asynchronous renderer verification completion.

Outputs:

```cpp
enum class ForegroundGameDecision
{
    Hidden,
    NeedsRendererVerification,
    Eligible,
};
```

plus current target information.

Suggested snapshot:

```cpp
struct CurrentForegroundGame
{
    ForegroundGameDecision decision{ForegroundGameDecision::Hidden};
    HWND window{};
    DWORD processId{};
    std::optional<GameProcessInstance> process;
};
```

## 22. Decision flow

```text
Evaluate current foreground
-> screen admission FAIL
   -> Hidden

screen admission PASS
-> resolve GameProcessInstance
-> Microsoft-known?
   -> Eligible

-> renderer-verified known game?
   -> Eligible

-> otherwise
   -> NeedsRendererVerification
```

Steam context does not bypass screen admission and does not directly make a PID eligible.

## 23. Verification token

Renderer completion is asynchronous. Do not reuse the legacy global coordinator generation semantics.

Use a verification request identity tied to process generation and current request generation, for example:

```cpp
struct RendererVerificationRequest
{
    std::uint64_t requestId{};
    GameProcessInstance process{};
};
```

Completion for an old request may still mark that exact process generation renderer-verified, but it must not directly show the HUD.

After completion:

```text
cache rendererVerified
-> read current foreground again
-> run full current screen admission again
-> only then report Eligible if the same process is currently eligible
```

## 24. Steam session context

Replace the conceptual meaning of legacy `Armed` with a simple context object.

Suggested:

```cpp
struct SteamSessionContext
{
    std::uint32_t appId{};
    std::uint64_t generation{};

    bool Active() const noexcept { return appId != 0; }
};
```

Transitions:

```text
0 -> N    appId=N, generation++
N -> M    appId=M, generation++
N -> 0    appId=0, generation++
```

The detector may record that a process was first observed while a Steam session was active, but this is metadata/supporting context only.

## 25. Mandatory PR3 scenario tests

### False positive regression

```text
WindowsTerminal foreground
+ renderer verified
+ excluded image
-> Hidden
```

### Generic game

```text
unknown fullscreen foreground PID
-> NeedsRendererVerification
renderer completion
-> final foreground recheck still same PID
-> Eligible
```

### Async stale completion

```text
Game A requests verification
user switches to Explorer
Game A verification completes
-> cache may update
-> current result remains Hidden
```

### Microsoft game

```text
Microsoft-known process
+ admitted fullscreen foreground
-> Eligible without generic renderer wait
```

### Alt+Tab

```text
verified Game A foreground -> Eligible
Explorer foreground        -> Hidden
Game A foreground again    -> Eligible immediately
```

### Multiple games

```text
Game A verified and still alive
Game B becomes foreground
-> Game B must be independently evaluated
-> Game A must not block Game B
```

This test is the structural replacement for the legacy sticky `Committed` behavior.

## 26. PR3 non-goals

Keep production wiring unchanged until the core is fully unit tested.

---

# PR4 — Cut `GameSessionController` Over to Foreground-First Detection

## 27. Goal

Make the new detector the production game-screen authority while keeping temporary compatibility hooks so downstream HUD/FPS code can be migrated separately.

This is the first PR that intentionally changes live In-Game Only detection behavior.

## 28. Foreground event path

Current foreground tracking already uses `EVENT_SYSTEM_FOREGROUND`.

Change the production handler to conceptually do:

```cpp
void GameSessionController::HandleProductionForegroundChanged(HWND, DWORD)
{
    EvaluateCurrentForegroundGame();
}
```

Do not retain early returns based on an old committed process being alive.

Every foreground transition is eligible for a fresh current-screen evaluation.

## 29. Window event path

`ProductionGameWindowSource` will now provide:

```text
CREATE
SHOW
HIDE
LOCATIONCHANGE
DESTROY
```

Use them for two distinct purposes.

### 29.1 Microsoft identity discovery

CREATE/SHOW may continue feeding `MicrosoftGameTrigger`.

### 29.2 Current foreground re-evaluation

For SHOW/HIDE/LOCATIONCHANGE/DESTROY:

```text
receive event
-> read GetForegroundWindow()
-> resolve current foreground PID
-> if event can affect current foreground window/process state
   call EvaluateCurrentForegroundGame()
```

Do not classify the event PID directly as the current game merely because it generated an object event.

This catches:

- Minecraft foreground first, then later expands from work-area-sized to fullscreen;
- Mafia foreground event before its final visible/fullscreen state;
- current game window becoming hidden/destroyed without requiring polling.

## 30. Microsoft positive path

On Microsoft trigger evidence:

```text
resolve process generation
-> KnownGameProcessCache.MarkMicrosoftGame()
-> if that process is current foreground, re-evaluate foreground immediately
```

Do not directly show the HUD from the identity callback.

## 31. Renderer verifier ownership

Keep `GameRenderVerifier`, but drive it from the new detector's `NeedsRendererVerification` result.

Rules:

- only one active generic foreground verification is necessary at a time;
- switching to a new unknown foreground process may stop/replace the old active verifier;
- a known verified game does not need a new renderer verification on every Alt+Tab return;
- completion marks the exact process generation verified;
- completion then performs final current-foreground re-evaluation.

## 32. Temporary compatibility bridge

PR4 may temporarily keep these downstream concepts to avoid mixing the detector cutover with telemetry rename work:

```text
ForegroundTracker.SetTrackedProcessId(currentEligibleGamePid)
GameSessionHooks.setCommittedProcess(currentEligibleGamePid)
```

However their semantics change immediately:

```text
old: long-lived globally committed game PID
new: current eligible foreground game PID only
```

When the foreground becomes Explorer/Steam/Search/etc.:

```text
tracked/compat PID = 0
```

When another game becomes foreground:

```text
compat PID = new game PID
```

No background game remains authoritative.

This bridge exists only to keep PR4 reviewable and is removed/renamed in PR5-PR7.

## 33. Steam changes

Retain `SteamRunningAppIdSource` registry notification infrastructure.

Stop using `SteamRunningAppTrigger` to drive `GameDetectionCoordinator::Armed` as production authority.

Instead:

```text
RunningAppID change
-> update SteamSessionContext
-> log transition
```

Optional behavior:

- while Steam session is active, CREATE/SHOW events may be annotated as observed-under-Steam-context;
- no PID is selected from AppID alone;
- no launch timeout;
- no forced foreground behavior.

## 34. PR4 runtime logging

Add logs that describe decisions rather than old state-machine transitions.

Examples:

```text
[GameDetection] foreground.evaluate pid=... hwnd=... admission=PASS
[GameDetection] foreground.reject pid=... reason=ExcludedExecutable
[GameDetection] foreground.reject pid=... reason=NotFullscreenLike
[GameDetection] renderer.verify-start pid=... request=...
[GameDetection] renderer.verified pid=... request=...
[GameDetection] foreground.eligible pid=... source=RendererVerified
[GameDetection] foreground.eligible pid=... source=MicrosoftIdentity
[GameDetection] foreground.clear oldPid=... reason=ForegroundChanged
```

Keep logging sufficiently deterministic for field comparison.

## 35. PR4 real-device validation checkpoint

Before proceeding to downstream cleanup, repeat a focused capture with at least:

```text
Windows Terminal / ordinary desktop foreground
Diablo IV
Minecraft for Windows
Dave the Diver
Mafia: The Old Country
Explorer Alt+Tab
Steam Alt+Tab
Game Bar/QAM appearance
Game A -> Game B while A is still alive
```

Expected high-level behavior:

```text
WindowsTerminal never eligible
actual fullscreen game eligible
Explorer/Steam foreground hides In-Game Only
return to already verified game is immediate
Minecraft becomes eligible on LOCATIONCHANGE when fullscreen geometry becomes valid
one alive game never blocks another foreground game
```

Do not continue cleanup if this cutover still reproduces a sticky-authority failure.

---

# PR5 — Replace Committed PID Semantics in HUD / FPS / Graphics Targeting

## 36. Goal

Remove the misleading downstream `CommittedProcess` contract and make all In-Game Only consumers explicitly use the **current eligible foreground game PID**.

After this PR, the new runtime semantics are complete even if some unused legacy files still exist.

## 37. `ProductionTelemetryController` rename

Replace:

```cpp
DWORD committedProcessId_{};
SetCommittedProcess(...)
ClearCommittedProcess()
```

with a name that encodes the real authority, e.g.:

```cpp
DWORD inGameForegroundProcessId_{};
SetInGameForegroundProcess(...)
ClearInGameForegroundProcess()
```

The exact naming may vary, but do not keep `Committed` terminology after sticky commit semantics are gone.

## 38. FPS target semantics

Keep the existing high-level split:

```text
Always
-> AlwaysModeFpsTarget current foreground PID

InGameOnly
-> current eligible foreground game PID
```

Do not make the FPS provider discover games.

The provider remains PID-only.

Required transitions:

```text
Game A eligible -> target A
Game A -> Explorer -> target 0 immediately
Explorer -> Game A -> target A immediately if known/eligible
Game A -> Game B -> target B
Game A remains background -> never fallback to A
```

Keep the same-PID 2-second `FpsStaleHold`, but PID changes/target clear must still invalidate stale data immediately.

## 39. HUD visibility semantic API

Replace:

```cpp
gameSession_.ForegroundIsTrackedProcess()
```

with explicit current-game state, e.g.:

```cpp
gameSession_.CurrentForegroundGameActive()
gameSession_.CurrentForegroundGameProcessId()
```

`App::ReconcileHudVisibility()` should consume the boolean only.

It must not derive game eligibility from presentation state or telemetry availability.

## 40. Graphics API probe target

The graphics API probe should follow the same current eligible game PID.

When foreground game clears:

```text
stop/clear graphics probe target
```

When another game becomes eligible:

```text
start/retarget graphics probe to that PID
```

Do not retain the old background game merely because the process is alive.

## 41. Visibility mode changes

When switching into `InGameOnly`, immediately evaluate the current foreground screen rather than waiting for another WinEvent.

When switching to `Always`, keep current existing Always FPS behavior.

## 42. Suspend/resume migration

Current resume recovery reads tracked/committed process semantics.

Update it to reason about:

```text
currentForegroundGamePid
currentForegroundGameActive
active renderer verification request if any
```

On resume:

```text
reconcile foreground
-> evaluate current screen
-> restore graphics/FPS target only for current eligible foreground game
```

Do not resurrect a previously backgrounded game as authority solely because it was selected before suspend.

## 43. Manual HUD override

Manual override remains a presentation/visibility override.

Do not let manual override mutate game identity caches or select a game PID.

## 44. PR5 tests

Add/adjust tests for:

- In-Game Only target clears on non-game foreground;
- no fallback to background verified game;
- Game A -> Game B retarget;
- Always mode unaffected;
- stale FPS invalidated on PID change;
- current eligible game controls graphics API probe target;
- resume reevaluates current foreground rather than restoring stale authority;
- manual override does not modify game-detection state.

## 45. PR5 validation checkpoint

Repeat the same real-device scenarios after FPS/HUD semantic cutover.

Specifically inspect:

- HUD visibility transitions;
- FPS PID logs;
- Alt+Tab return latency;
- no stale old-game FPS during Game A -> Explorer -> Game B;
- Minecraft LOCATIONCHANGE transition;
- VRR/presentation behavior unchanged.

---

# PR6 — Remove Legacy Sticky Game Detection State Machine

## 46. Goal

Delete the no-longer-authoritative global candidate/commit machinery once production no longer depends on it.

Likely removals include all or most of:

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
GenericForegroundTrigger as a coordinator adapter
SteamRunningAppTrigger as a coordinator adapter
CommittedTargetReleasePlan
```

Do not remove shared low-level helpers merely because they previously lived in a legacy-named file. Move reusable process exclusion/inspection policy to a more appropriate component if still needed by `GameScreenAdmission`.

## 47. `GameSessionController` cleanup

Remove legacy methods such as:

```text
ApplyProductionEvidence
HandleGameDetectionTransition
TryCommitReadyCandidateFromForeground
ReleaseProductionGameCandidate
ClearProductionCandidate
ReleaseCommittedProductionTarget
```

if they are no longer part of the new design.

The controller should become an orchestrator around:

```text
foreground/window event sources
Steam session context
Microsoft identity trigger
ForegroundGameDetector
KnownGameProcessCache
GameRenderVerifier
current-game change notifications
```

## 48. Tests

Delete tests whose only purpose is validating the obsolete sticky coordinator semantics.

Keep equivalent behavioral coverage in new detector scenario tests.

Explicitly preserve a regression test equivalent to:

```text
non-game false candidate can never lock out later real games
```

---

# PR7 — Remove Tracked-PID / Single-Lifetime Compatibility Layer

## 49. Goal

Finish the cleanup so class responsibilities reflect the final architecture.

## 50. Simplify `ForegroundTracker`

Current `ForegroundTracker` owns:

```text
foreground WinEvent hook
tracked process ID
tracked process HANDLE
foregroundMatches
last foreground HWND/PID
```

The new architecture only needs foreground observation/current foreground reconciliation.

Reduce it toward:

```text
ForegroundTracker
-> event source
-> last/current foreground HWND/PID
-> changed callback
```

Remove:

```text
SetTrackedProcessId
TrackedProcessId
ForegroundIsTrackedProcess
trackedProcess_
trackedProcessId_
foregroundMatches_
```

unless another unrelated feature still requires them.

## 51. Remove single committed lifetime watcher semantics

`ProductionProcessLifetimeWatcher` currently exists to track the one selected candidate/committed process.

The new known-game cache is process-generation aware and does not require one global lifetime authority.

Options for final design:

1. remove the watcher entirely and validate creation time on cache lookup; or
2. retain a narrow watcher only for an active renderer-verification request if that simplifies prompt cancellation.

Do not create a permanent watcher thread/handle per cached game unless real evidence shows a need.

## 52. Final API cleanup

Remove transitional names and bridge hooks from:

```text
GameSessionHooks
App
ProductionTelemetryController
resume recovery
HUD visibility reconcile
```

The final public queries from `GameSessionController` should be small and explicit, e.g.:

```cpp
bool CurrentForegroundGameActive() const noexcept;
DWORD CurrentForegroundGameProcessId() const noexcept;
void ReevaluateForeground();
void ReconcileForeground();
```

Verifier query methods should remain only if suspend/resume genuinely needs them.

---

## 53. Final intended `GameSessionController` responsibility

After all seven PRs, `GameSessionController` should no longer mean "global game session with one committed process".

It should instead orchestrate the live evidence needed to answer current-screen eligibility.

Conceptual members:

```cpp
ForegroundTracker foregroundTracker_;
ProductionGameWindowSource productionGameWindowSource_;
SteamRunningAppIdSource steamRunningAppIdSource_;
SteamSessionContext steamSession_;
MicrosoftGameTrigger microsoftGameTrigger_;
KnownGameProcessCache knownGames_;
ForegroundGameDetector foregroundGameDetector_;
GameRenderVerifier gameRenderVerifier_;
```

No global committed candidate PID.

No `Committed` state.

No policy that a live old game blocks a new foreground process.

---

## 54. Final foreground evaluation pseudocode

```cpp
void GameSessionController::EvaluateCurrentForeground()
{
    const HWND hwnd = GetForegroundWindow();
    DWORD pid{};
    if (hwnd)
        GetWindowThreadProcessId(hwnd, &pid);

    const auto admission = gameScreenAdmission_.Inspect(hwnd, pid);
    if (!admission.admitted)
    {
        SetCurrentForegroundGame(nullptr, 0, admission.reason);
        StopVerifierIfNoLongerRelevant(pid);
        return;
    }

    const auto instance = QueryGameProcessInstance(pid);
    if (!instance)
    {
        SetCurrentForegroundGame(nullptr, 0,
            GameScreenRejectReason::ProcessUnavailable);
        return;
    }

    const auto known = knownGames_.Lookup(*instance);
    if (known && known->microsoftGameIdentity)
    {
        SetCurrentForegroundGame(hwnd, pid, L"MicrosoftIdentity");
        return;
    }

    if (known && known->rendererVerified)
    {
        SetCurrentForegroundGame(hwnd, pid, L"RendererVerifiedCache");
        return;
    }

    SetCurrentForegroundGame(nullptr, 0, L"WaitingRendererVerification");
    BeginRendererVerification(*instance);
}
```

Renderer completion:

```cpp
void GameSessionController::OnRendererVerified(
    const RendererVerificationRequest& request)
{
    knownGames_.MarkRendererVerified(request.process);

    // Never show based on the stale callback itself.
    EvaluateCurrentForeground();
}
```

---

## 55. Target behavior by scenario

### 55.1 Normal desktop application

```text
WindowsTerminal foreground
-> excluded
-> HUD HIDE
-> no game cache pollution
```

### 55.2 Steam game slow launch

```text
RunningAppID 0 -> N
-> Steam context active
-> Steam/launcher foreground stays non-game
-> HUD HIDE

actual game foreground appears
-> screen admission
-> renderer verification if unknown
-> HUD SHOW
```

No timeout.

No AppID-derived PID.

### 55.3 Game starts in background

```text
Steam session active
actual game HWND/render activity exists in background
foreground = Steam/Explorer
-> HUD HIDE

user/Windows later foregrounds the game
-> normal current foreground evaluation
-> HUD SHOW when eligible
```

ClawHUD should not steal focus.

### 55.4 Microsoft/Xbox game

```text
game top-level window appears
-> MicrosoftGame.config exact executable match
-> cache Microsoft identity

foreground may still be GamingServices/PickerHost
-> HUD HIDE

actual game becomes fullscreen foreground
-> identity cache hit + screen admission
-> HUD SHOW without generic renderer wait
```

### 55.5 Generic non-Steam Win32 game

```text
fullscreen foreground unknown executable
-> screen admission PASS
-> not known Microsoft / renderer verified
-> same-PID API2 verification
-> final foreground recheck
-> HUD SHOW
```

### 55.6 Alt+Tab

```text
Game A foreground eligible
-> HUD SHOW

Explorer foreground
-> HUD HIDE
-> Game A remains known cache only

Game A foreground again
-> cache hit
-> HUD SHOW immediately
```

### 55.7 Game A to Game B while A remains alive

```text
Game A background but alive
Game B becomes foreground
-> evaluate Game B normally
-> A cannot block B
```

This is a mandatory architectural invariant.

### 55.8 Minecraft geometry transition

```text
Minecraft foreground 1920x1128
-> NotFullscreenLike
-> HUD HIDE

same HWND EVENT_OBJECT_LOCATIONCHANGE
-> -3,-3,1923,1203
-> admission PASS
-> known Microsoft game
-> HUD SHOW
```

### 55.9 Ghost window

```text
Ghost/identity unavailable foreground
-> HUD may HIDE
-> no special old-game retention

real game foreground returns
-> normal cache/admission path
-> HUD SHOW
```

---

## 56. Logging contract for the redesign

Use production logs to make future field validation easy.

Recommended event categories:

```text
[GameDetection] steam.session
[GameDetection] microsoft.identity
[GameDetection] foreground.evaluate
[GameDetection] foreground.reject
[GameDetection] renderer.verify-start
[GameDetection] renderer.verify-stop
[GameDetection] renderer.verified
[GameDetection] known-game.cache-hit
[GameDetection] known-game.cache-store
[GameDetection] foreground.eligible
[GameDetection] foreground.clear
```

Useful common fields:

```text
pid
hwnd
exe
processCreationTime
source/reason
steamAppId
requestId
windowRect
monitorRect
```

Do not restore huge diagnostic-only metric dumps into the normal app log.

---

## 57. Production no-polling constraint

The final design must remain event-driven.

Allowed:

```text
SetWinEventHook
RegNotifyChangeKeyValue
one-shot process/window inspection
one-shot Microsoft game identity probing
PID-filtered API2 verification while an unknown candidate exists
process creation-time lookup
```

Do not add:

```text
repeated EnumWindows
foreground polling timer
process-list polling
PDH TopGPU polling
FindWindow loops
WMI WITHIN polling
periodic package enumeration
```

The standalone `ClawHUD.Diag` may continue using polling because it is a research recorder, but production should not copy that behavior.

---

## 58. PresentMon/API2 rules

Keep these distinctions explicit.

### Renderer verification

```text
same-PID first displayed-frame evidence
-> supporting game evidence for unknown generic foreground process
```

### FPS telemetry

```text
DISPLAYED_FPS numeric value
-> telemetry only
```

Never use numeric FPS magnitude as game identity.

Do not use PDH TopGPU in production.

Do not use swap-chain address availability as a required game verdict.

The FPS provider continues to accept a PID chosen by policy and remains unaware of Steam, Microsoft identity, fullscreen rules, or game detection.

---

## 59. HUD / VRR safety contract

This redesign is entirely upstream of presentation.

Do not modify:

```text
HUD windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
WS_EX_LAYERED behavior
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
ProductionHudPresentationContract()
independent-flip requirement
Presentation API / DirectComposition production path
premultiplied-alpha contract
```

Background opacity remains background-only.

Game detection bugs are not justification for changing presentation behavior.

All existing presentation regression assertions must remain intact.

---

## 60. Validation matrix after implementation

At minimum validate these categories on real hardware.

| Category | Scenario | Expected |
|---|---|---|
| Negative control | Explorer | hidden |
| Negative control | Windows Terminal | hidden |
| Negative control | Browser | hidden |
| Platform shell | Steam foreground while game alive | hidden |
| Platform shell | Game Bar/QAM foreground | hidden unless future explicit continuity policy exists |
| Steam game | Diablo IV | shown only when actual game screen eligible |
| Steam game | Dave the Diver | shown |
| Microsoft/Xbox | Minecraft | shown after valid current screen admission |
| Generic/non-Steam | Mafia or equivalent | shown after renderer verification |
| Transition | Game -> Explorer -> Game | hide then immediate restore |
| Transition | Game A -> Game B while A alive | B becomes authority |
| Geometry | work-area maximized | hidden |
| Geometry | borderless overscan ±3 px | shown |
| Lifecycle | game exits | target clears |
| Suspend/resume | resume to game | reevaluated current game |
| Suspend/resume | resume to desktop | hidden |
| Mode | Always | existing behavior unchanged |
| Mode | In-Game Only | current foreground-game authority |

---

## 61. Recommended PR review checkpoints

### After PR1

Verify pure admission logic and WinEvent mapping only.

### After PR2

Verify cache process-generation semantics only.

### After PR3

Verify all detector scenario tests before any production cutover.

### After PR4

Run real-device game-detection validation before downstream rename/cleanup.

### After PR5

Run full HUD/FPS/Alt+Tab/suspend-resume validation. This is the functional completion checkpoint.

### After PR6-PR7

Verify cleanup did not reintroduce hidden dependency on legacy committed semantics.

---

## 62. Why seven PRs instead of one large rewrite

A single rewrite would combine four independently risky areas:

1. screen admission/window lifecycle;
2. process identity/cache semantics;
3. game detector state/async renderer verification;
4. HUD/FPS/suspend-resume downstream targeting.

Separating them provides a clear failure boundary.

If the first production cutover fails on hardware, PR1-PR3 remain useful tested foundations and the failure can be isolated to wiring rather than the policy primitives themselves.

The pre-release status means we do not need long-lived backward-compatibility layers, but small migration steps still materially improve review quality.

---

## 63. Definition of the final successful architecture

The migration is complete when all of the following are true:

- there is no globally authoritative `Committed` game PID;
- an alive background game cannot block evaluation of another foreground process;
- current foreground HWND/PID is the final In-Game Only authority;
- fullscreen-like screen admission is evaluated in physical coordinates with a practical tolerance;
- SHOW/LOCATIONCHANGE/HIDE/DESTROY can re-trigger current foreground evaluation;
- Steam AppID is session context only;
- Microsoft exact executable identity is strong positive evidence, not a visibility override;
- unknown generic games require same-PID renderer verification;
- renderer completion always performs final current foreground recheck;
- verified game evidence is cached per PID + process creation time;
- Alt+Tab back to a verified game is immediate;
- In-Game Only FPS target equals the same current eligible foreground PID;
- Always mode behavior remains unchanged;
- no production game-discovery polling is introduced;
- HUD/VRR presentation contracts are unchanged;
- the WindowsTerminal false-commit failure is structurally impossible rather than merely patched with one executable exclusion.

That is the intended end-state for the next production game-detection implementation series.
