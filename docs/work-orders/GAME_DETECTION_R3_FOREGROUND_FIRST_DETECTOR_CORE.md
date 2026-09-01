# Work Order — Game Detection R3: Foreground-First Detector Core

Status: implementation work order  
Repository: `onehoon/ClawHUD`  
Implementation baseline: after PR #201 (`Add generation-aware known game cache`) is merged  
Validated PR #201 head at authoring time: `929b6416e490e59c2e31ce71858cf3a58f7e3b26`  
Parent design: `docs/GAME_DETECTION_REDESIGN_PR_PLAN_2026-08-31.md`  
Field evidence: `docs/GAME_DETECTION_FIELD_ANALYSIS_2026-08-31.md`  
R1 work order: `docs/work-orders/GAME_DETECTION_R1_FOREGROUND_SCREEN_ADMISSION.md`  
R2 work order: `docs/work-orders/GAME_DETECTION_R2_KNOWN_GAME_PROCESS_CACHE.md`

---

## 1. Goal

Implement the new **foreground-first game detection decision core** beside the existing production `GameDetectionCoordinator`.

This PR must create a reusable detector that can answer:

> Given the current foreground HWND/PID, the R1 screen-admission result, the R2 process-generation identity/cache, current Steam session context, and renderer-verification completion, is the current foreground screen hidden, waiting for renderer verification, or eligible as the current game screen?

The detector must have **no global `Committed` game PID** and no policy where an older live game can block evaluation of a newer foreground process.

The core invariant is:

> A process may remain remembered as a known game while backgrounded, but only the **currently admitted foreground process instance** may become the current eligible game screen.

This PR is still a foundation PR.

It must **not** cut `GameSessionController`, HUD visibility, FPS targeting, graphics-API targeting, Steam source behavior, or production renderer ownership over to the new detector yet.

PR4 performs the first production cutover.

---

## 2. Why this PR exists

The mixed 2026-08-31 field run demonstrated that the current production architecture has a structural failure mode rather than a single missing exclusion.

The old model is effectively:

```text
candidate
-> renderer verified
-> Ready
-> Committed
-> keep one PID authoritative while it remains alive
```

A normal desktop renderer such as `WindowsTerminal.exe` was able to become the committed target. Once committed, later real games were ignored because the old live PID remained protected by the state machine.

R1 addressed the first part of the problem by adding a hard screen-admission gate:

```text
valid top-level window
+ visible
+ not minimized
+ not cloaked
+ inspectable/non-excluded process
+ monitor resolved
+ fullscreen-like physical bounds
```

R2 addressed the second part by introducing positive game evidence that is safe across Alt+Tab and PID reuse:

```text
GameProcessInstance = PID + process creation time
KnownGameProcessCache
```

R3 now needs to combine those foundations into the new decision model without activating it in production yet.

---

## 3. Required architectural outcome

After this PR there should be two detector models in the repository temporarily:

```text
Legacy production path
    GameDetectionCoordinator
    Ready / Committed
    still active in GameSessionController

New R3 core
    ForegroundGameDetector
    Hidden / NeedsRendererVerification / Eligible
    unit-tested but not yet production-authoritative
```

This temporary duplication is intentional.

Do not mutate the legacy coordinator to imitate the new model. The migration plan explicitly builds the new core beside it and cuts production over in PR4.

---

# Part A — New detector domain model

## 4. Add `ForegroundGameDetector`

Create a narrow game-detection component, suggested files:

```text
src/ClawHUD/GameDetection/ForegroundGameDetector.h
src/ClawHUD/GameDetection/ForegroundGameDetector.cpp
```

The component should depend on the foundations already introduced by R1/R2:

```text
GameScreenAdmission
GameProcessInstance
KnownGameProcessCache
```

Do not depend on:

```text
App
HudController
HudPresentation
ProductionTelemetryController
SettingsWindow
EC telemetry
IGCL
PDH TopGPU
PresentMon dynamic FPS query
```

The detector is game-screen policy/state only.

---

## 5. Decision enum

Use a decision model equivalent to:

```cpp
enum class ForegroundGameDecision
{
    Hidden,
    NeedsRendererVerification,
    Eligible,
};
```

Semantics:

```text
Hidden
    Current foreground is not an admitted game screen.

NeedsRendererVerification
    Current foreground passes hard screen admission, but this exact process
    generation does not yet have strong/verified game evidence.

Eligible
    Current foreground passes hard screen admission and this exact process
    generation is already known through Microsoft identity or renderer evidence.
```

Do not add a `Committed` equivalent.

Do not add a global `Ready` state.

Do not add a scoring system.

---

## 6. Current foreground snapshot

Use a result/snapshot equivalent to:

```cpp
struct CurrentForegroundGame
{
    ForegroundGameDecision decision{ForegroundGameDecision::Hidden};
    HWND window{};
    DWORD processId{};
    std::optional<GameProcessInstance> process;
    GameScreenAdmissionReason admissionReason{
        GameScreenAdmissionReason::NoWindow};
};
```

The exact shape may vary, but retain enough structured information to support PR4 logging and downstream bridging without reconstructing the decision externally.

Recommended rule:

- preserve current HWND/PID in the snapshot when they were supplied;
- preserve the R1 admission reason when available;
- include the exact `GameProcessInstance` only when it was successfully resolved;
- never substitute a prior game PID when the current foreground cannot be resolved.

---

# Part B — Explicit evaluation input

## 7. Keep Win32 observation and policy separable

R1 already provides:

```cpp
GameScreenObservation ObserveGameScreen(HWND window, DWORD processId) noexcept;
GameScreenAdmissionResult EvaluateGameScreenAdmission(
    const GameScreenObservation& observation) noexcept;
```

R3 should not duplicate those Win32 checks.

Prefer a detector API that can be unit-tested with deterministic R1 observations while still being easy for PR4 to call from real foreground events.

One acceptable model is:

```cpp
struct ForegroundGameEvaluationInput
{
    GameScreenObservation screen;
};

ForegroundGameEvaluation Evaluate(
    const ForegroundGameEvaluationInput& input) noexcept;
```

with the detector internally calling:

```cpp
EvaluateGameScreenAdmission(input.screen)
```

and resolving the process instance through an injectable query function.

An alternative API may accept an already-computed admission result, but do not allow the caller to bypass the hard R1 admission policy accidentally.

There should be one obvious production path that always applies R1 admission before positive game evidence.

---

## 8. Process-instance query injection

The detector needs exact process generation identity.

Production should use:

```cpp
QueryGameProcessInstance(processId)
```

For deterministic tests, support dependency injection equivalent to:

```cpp
using ProcessInstanceQuery =
    std::function<std::optional<GameProcessInstance>(DWORD)>;
```

Suggested constructor shape:

```cpp
explicit ForegroundGameDetector(
    KnownGameProcessCache& knownGames) noexcept;

ForegroundGameDetector(
    KnownGameProcessCache& knownGames,
    ProcessInstanceQuery processQuery);
```

Exact naming/ordering may vary.

Do not make `GetProcessTimes()` success optional for persistent positive cache use.

If process-generation identity cannot be resolved, the new detector must fail closed for eligibility.

Required result:

```text
screen admission PASS
+ process instance query FAIL
-> Hidden
```

Use an explicit reason/state if useful, but do not silently treat numeric PID as durable identity.

---

# Part C — Core decision flow

## 9. Mandatory evaluation order

The detector must evaluate the current foreground in this order:

```text
1. Run R1 GameScreenAdmission
2. If admission fails -> Hidden
3. Resolve exact GameProcessInstance
4. If process instance cannot be resolved -> Hidden
5. Lookup exact process generation in KnownGameProcessCache
6. If microsoftGameIdentity == true -> Eligible
7. Else if rendererVerified == true -> Eligible
8. Else -> NeedsRendererVerification
```

Steam context must not bypass steps 1–4.

Steam context must not make step 6/7 true.

Renderer evidence for a different generation of the same PID must not make the current process eligible.

---

## 10. Hard admission always remains authoritative

Positive game evidence does **not** override current screen validity.

Examples:

```text
Microsoft-known process + minimized
-> Hidden

Renderer-known process + windowed 1920x1128 on a 1920x1200 monitor
-> Hidden

Renderer-known game + Explorer foreground
-> Explorer evaluated independently -> Hidden

Known background game remains alive
-> no effect on current foreground decision
```

This is the conceptual difference from the legacy sticky target model.

---

## 11. Cache semantics

R2 defines known-game evidence as:

```cpp
microsoftGameIdentity || rendererVerified
```

Preserve that meaning in R3.

Do not treat:

```cpp
observedDuringSteamSession
```

as a game verdict.

A Steam-only unknown admitted process still requires renderer verification.

---

# Part D — Renderer verification request model

## 12. Add a detector-owned verification request identity

Renderer verification is asynchronous. The old `GameDetectionCoordinator::generation` must not be reused as the new detector's identity mechanism.

Introduce a request model equivalent to:

```cpp
struct RendererVerificationRequest
{
    std::uint64_t requestId{};
    GameProcessInstance process{};
};
```

The detector should allocate monotonically increasing non-zero request IDs.

Request identity must include the full process generation.

Do not use:

```text
PID only
HWND only
Steam AppID
legacy coordinator generation
```

as the new verification identity.

---

## 13. Evaluation output should expose verification work, not start workers

R3 must not own or start the production `GameRenderVerifier` yet.

Return enough information for PR4 to perform that wiring later.

Suggested result:

```cpp
struct ForegroundGameEvaluation
{
    CurrentForegroundGame current;
    std::optional<RendererVerificationRequest> verificationRequest;
};
```

Semantics:

```text
Hidden
-> no new verification request

Eligible
-> no new verification request

NeedsRendererVerification
-> verificationRequest identifies the exact process generation that needs proof
```

Do not call `GameRenderVerifier::Start()` from the new core in R3.

---

## 14. Deduplicate repeated verification requests

This is mandatory for the event-driven design.

A game may generate several foreground/show/location-change events while it settles into fullscreen geometry. Re-evaluating the same unknown exact process generation must not create an endless sequence of distinct verification IDs.

Required behavior:

```text
Evaluate admitted unknown Game A generation X
-> request #10

Evaluate same admitted Game A generation X again
-> reuse request #10

LOCATIONCHANGE on same process generation
-> if still admitted and still unknown, reuse request #10
```

Generate a new request only when the process instance requiring verification changes or the previous request has been completed/invalidated according to the final implementation model.

At minimum:

```text
Game A generation X -> request #10
Game B generation Y -> request #11
PID reused: Game A numeric PID, generation Z -> request #12
```

Do not restart verification merely because HWND changed while the exact process generation is unchanged unless there is a concrete later production reason.

The game identity is process-generation based, not HWND-generation based.

---

## 15. No older live game may block a newer foreground process

This is a mandatory structural invariant and must be directly tested.

Example:

```text
Game A generation X
-> renderer verified
-> known cache contains A
-> A remains alive in background

Game B generation Y becomes foreground and passes screen admission
-> evaluate B independently
-> if unknown, return NeedsRendererVerification for B
```

There must be no branch equivalent to:

```cpp
if (oldGameStillAlive)
    return oldGame;
```

or:

```cpp
if (currentState == Committed)
    ignoreNewForeground();
```

---

# Part E — Renderer completion semantics

## 16. Add an explicit completion input

Use a completion model equivalent to:

```cpp
struct RendererVerificationCompletion
{
    RendererVerificationRequest request;
    bool verified{};
};
```

or, if the only completion event is positive proof in the current verifier, a narrower positive-only API is acceptable:

```cpp
void MarkRendererVerified(
    const RendererVerificationRequest& request) noexcept;
```

However the API must preserve both:

```text
requestId
exact GameProcessInstance
```

for stale-completion tests and later PR4 adaptation.

---

## 17. Positive completion updates cache, not HUD/current eligibility directly

On a trusted positive renderer completion for exact process generation A:

```text
KnownGameProcessCache.MarkRendererVerified(A)
```

This may happen even if A is no longer foreground.

That is desirable: if the user later Alt+Tabs back to the same process generation, it can become `Eligible` immediately after screen admission.

But renderer completion must **never directly set current target eligibility**.

Required conceptual flow:

```text
renderer completion for process A
-> mark A rendererVerified in cache
-> evaluate CURRENT foreground again
-> only current foreground admission/cache decides Eligible/Hidden
```

R3 does not need to call Win32 `GetForegroundWindow()` itself if the production adapter will supply a fresh evaluation input in PR4. But the API must make a stale completion incapable of directly changing the stored current foreground target to the completed process.

---

## 18. Stale completion example

This exact sequence must be covered:

```text
1. Game A fullscreen foreground
2. Evaluate -> NeedsRendererVerification(request A)
3. User switches to Explorer
4. Evaluate Explorer -> Hidden
5. Renderer completion for A arrives late
6. A may be marked rendererVerified in KnownGameProcessCache
7. Current result remains Hidden because Explorer is still foreground
```

Do not discard useful renderer evidence merely because foreground changed.

Do not display/activate Game A merely because its old request completed.

---

## 19. PID-reuse safety for completion

A late completion from an old process generation must not mark a newer process generation with the same numeric PID.

Example:

```text
request #20 = PID 5000, creation A
process exits
PID 5000 reused, creation B
request #20 completes late
```

Allowed:

```text
cache old generation A evidence if the cache implementation accepts that exact entry
```

but absolutely forbidden:

```text
mark PID 5000 generation B rendererVerified
```

The `RendererVerificationRequest.process` identity must be passed unchanged into R2 cache APIs.

---

# Part F — Current detector state

## 20. Keep state minimal

The detector may need small state for:

```text
next verification request ID
currently outstanding/reusable verification request
last evaluation snapshot, if useful for tests/debugging
Steam session context
```

It must not recreate the legacy state machine under new names.

Avoid state such as:

```text
Committed PID
Ready PID
sticky selected game
single authoritative game process lifetime
candidate protected while alive
```

The authoritative question is always the supplied/current foreground screen.

---

## 21. Optional current snapshot storage

It is acceptable for `ForegroundGameDetector` to expose the last current snapshot:

```cpp
const CurrentForegroundGame& Current() const noexcept;
```

if that simplifies PR4.

If stored, every evaluation must replace the current snapshot with the newly evaluated foreground result.

A previous `Eligible` process must not remain current after a new foreground evaluates as `Hidden`.

Example:

```text
Game A -> Eligible
Explorer -> Hidden
Current() must now be Explorer/Hidden or empty/Hidden
```

Never retain Game A as the current target merely because it is still present in `KnownGameProcessCache`.

---

# Part G — Steam session context

## 22. Add a simple Steam context model

R3 may introduce the target context model described by the parent design:

```cpp
struct SteamSessionContext
{
    std::uint32_t appId{};
    std::uint64_t generation{};

    bool Active() const noexcept { return appId != 0; }
};
```

The exact location may be in `ForegroundGameDetector.*` or a tiny dedicated header if clearly justified.

Do not create a broad Steam subsystem abstraction.

---

## 23. Steam context transition semantics

Provide an update method/helper with deterministic behavior:

```text
initial 0

0 -> N
    appId = N
    generation++

N -> M where M != N
    appId = M
    generation++

N -> 0
    appId = 0
    generation++

N -> N
    no semantic transition
    generation unchanged
```

Generation is context-change identity, not a game PID generation.

Do not connect this to the existing `SteamRunningAppIdSource` in R3 production wiring yet unless only a compile-time non-authoritative adapter is needed. PR4 owns the production cutover.

---

## 24. Steam may record context only

When an unknown admitted process is evaluated while Steam context is active, it is acceptable to record:

```cpp
knownGames.MarkObservedDuringSteamSession(process);
```

provided this occurs on the detector/controller owner thread and does not affect eligibility.

Required invariant:

```text
Steam active
+ screen admitted
+ Steam-observed only
-> NeedsRendererVerification
```

Do not implement:

```text
Steam AppID -> PID mapping
RunningAppID active -> current PID is game
RunningAppID active -> Eligible
window created during Steam session -> game
```

Steam is session context only.

---

# Part H — Relationship to existing `GameRenderVerifier`

## 25. Preserve the production verifier implementation

Current production `GameRenderVerifier` remains useful.

It already:

- takes a PID;
- uses the shared production PresentMon telemetry provider;
- waits for displayed-frame evidence;
- reports one positive `FirstDisplayedFrame` event;
- is bounded/explicitly stoppable.

Do not redesign its PresentMon sampling internals in R3.

Do not migrate its `generation` field to the new request model inside the production path yet unless a tiny overload/value type is necessary for compiling isolated tests.

PR4 will adapt production verifier events to `RendererVerificationRequest` ownership.

R3 tests should exercise the detector's logical completion API directly rather than starting real PresentMon verification.

---

## 26. Do not treat FPS values as game identity

The field diagnostic demonstrated that API2 FPS values are not safe game identity by themselves, and the production design intentionally uses renderer verification only as supporting evidence after hard foreground screen admission.

R3 must not add:

```text
FPS threshold
FPS > 0 -> game
TopGPU rank
swap-chain address requirement
presented/displayed numeric value comparisons
```

The detector only consumes the semantic fact:

```text
this exact process generation has renderer evidence
```

---

# Part I — Mandatory scenario tests

## 27. Add a dedicated detector test target

Suggested file:

```text
tests/ForegroundGameDetectorTests.cpp
```

Register a focused CTest target with only the sources required by the new core and R1/R2 policy dependencies.

Tests should use deterministic synthetic observations and injected process-instance queries.

Do not manipulate the real foreground window or require a game/Steam/PresentMon service in CI.

---

## 28. Test helpers

Create concise helpers equivalent to:

```cpp
GameScreenObservation AdmittedScreen(HWND hwnd, DWORD pid);
GameScreenObservation WindowedScreen(HWND hwnd, DWORD pid);
GameProcessInstance Process(DWORD pid, ULONGLONG creation);
```

Use the actual R1 admission evaluator in detector tests so R3 cannot silently diverge from the production hard-screen gate.

---

## 29. Mandatory false-positive regression

Directly preserve the field failure as a test.

Equivalent setup:

```text
WindowsTerminal foreground observation
executableExcluded = true
process generation resolves
cache may even contain renderer evidence
```

Expected:

```text
Hidden
no verification request
```

Known/renderer evidence must never bypass exclusion/admission failure.

---

## 30. Generic game flow

Test:

```text
unknown exact process generation
+ admitted fullscreen foreground
-> NeedsRendererVerification
-> request exists

positive completion for that request
-> cache.rendererVerified == true

fresh evaluation of same admitted foreground
-> Eligible
-> no new verification request
```

---

## 31. Repeated event/request dedupe

Test:

```text
same unknown admitted process generation
Evaluate #1 -> request ID 100
Evaluate #2 -> request ID 100
Evaluate #3 -> request ID 100
```

The exact numeric start value does not matter, only stability/non-zero identity.

Then switch to a different process generation:

```text
Game B -> request ID different from 100
```

This protects PR4 from SHOW/LOCATIONCHANGE verification churn.

---

## 32. Microsoft game fast path

Setup:

```text
cache.MarkMicrosoftGame(process A)
admitted fullscreen foreground A
```

Expected:

```text
Eligible immediately
no renderer request
```

Also prove a Microsoft-known process that fails R1 screen admission remains `Hidden`.

---

## 33. Renderer-known fast path

Setup:

```text
cache.MarkRendererVerified(process A)
admitted foreground A
```

Expected:

```text
Eligible immediately
```

This is what makes Alt+Tab return fast.

---

## 34. Alt+Tab sequence

Test the complete state transition:

```text
Game A renderer-known + admitted
-> Eligible

Explorer foreground / excluded or non-fullscreen
-> Hidden

Game A foreground again, same exact generation
-> Eligible immediately
```

Assert the detector's current snapshot does not remain Game A during the Explorer step.

---

## 35. Multiple games coexist

This is a mandatory structural regression test.

```text
Game A known and alive
Game A -> Eligible

Game B admitted foreground, unknown
-> NeedsRendererVerification for B

complete B verification
fresh evaluate B
-> Eligible B

KnownGameProcessCache still contains A and B
```

No A state may block B evaluation.

---

## 36. Stale async completion

Test:

```text
Game A -> NeedsRendererVerification(request A)
Explorer -> Hidden
request A completion arrives
```

Expected:

```text
A is renderer-known in cache
current foreground decision remains Hidden
```

Then:

```text
Game A returns
-> Eligible immediately
```

---

## 37. Stale completion while another game is foreground

Test:

```text
A requests verification
B becomes foreground and requests verification
A completion arrives
```

Expected:

```text
A may become renderer-known in cache
B remains current foreground decision
A completion must not switch current target back to A
```

If B is still unknown, B remains `NeedsRendererVerification`.

---

## 38. PID reuse

Test:

```text
PID 7000 generation A is renderer-known
PID 7000 generation B becomes current foreground
```

Expected:

```text
generation B does not inherit A evidence
-> NeedsRendererVerification
```

Also test an old generation-A completion cannot mark generation B.

---

## 39. Steam-only evidence

Test:

```text
SteamSessionContext active
process A admitted
cache only has observedDuringSteamSession=true
```

Expected:

```text
NeedsRendererVerification
not Eligible
```

Then mark Microsoft or renderer evidence and confirm the same admitted foreground becomes eligible.

---

## 40. Screen-admission regressions

At minimum cover through the detector:

```text
minimized known game -> Hidden
cloaked known game -> Hidden
work-area-sized known game -> Hidden
fullscreen-like ±3 px known game -> Eligible
process-inspection/exclusion failure -> Hidden
process-instance-query failure -> Hidden
```

The detector must never use known evidence to bypass R1.

---

# Part J — Logging and diagnostics

## 41. Keep the core log-independent where practical

Prefer returning structured decisions rather than logging directly from every pure policy branch.

PR4 will be the better place to emit production logs such as:

```text
foreground.eval
foreground.hidden reason=...
foreground.verify-request pid=... request=...
foreground.eligible pid=... evidence=...
renderer.verified-cache-store
```

If R3 adds debug-format helpers, keep them deterministic and low volume.

Do not log on every cache lookup solely for debugging convenience.

---

# Part K — Production wiring boundary

## 42. Do not cut over `GameSessionController` in R3

Do **not** replace or bypass the current production handling in:

```text
HandleProductionForegroundChanged
HandleProductionWindowEvent
HandleMicrosoftGameEvidence
HandleGameRenderVerifierEvent
ApplyProductionEvidence
HandleGameDetectionTransition
TryCommitReadyCandidateFromForeground
```

The existing detector remains the live production authority until PR4.

It is acceptable for `GameSessionController` to own the R2 cache already introduced by PR #201. Do not make the new R3 detector live simply because the cache is available.

---

## 43. Do not change HUD/FPS target semantics

R3 must not modify:

```text
GameSessionHooks::setCommittedProcess
GameSessionHooks::clearCommittedProcess
ProductionTelemetryController committedProcessId_
App::ReconcileHudVisibility()
ForegroundTracker tracked PID semantics
Graphics API probe target ownership
```

Those are migrated in PR4/PR5 according to the parent plan.

No user-visible detection behavior should intentionally change in R3.

---

# Part L — VRR / HUD presentation safety contract

## 44. Non-negotiable presentation boundary

This game-detection PR must not modify, replace, weaken, or work around any HUD presentation/VRR invariant, including:

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
Presentation API / DirectComposition production path
premultiplied-alpha presentation contract
```

Do not modify renderer/presentation behavior to implement game detection.

Existing presentation-contract regression tests must remain passing.

---

# Part M — Build integration

## 45. Add only required sources

Add the new detector source to the production build so later PRs can consume it, even though the active controller does not use it yet.

Add a focused test target equivalent to:

```text
ClawHUD.ForegroundGameDetectorTests
```

Include only the necessary sources, expected to include some subset of:

```text
ForegroundGameDetector.cpp
GameScreenAdmission.cpp
GameProcessInstance.cpp
KnownGameProcessCache.cpp
ProductionTargetPolicy.cpp
```

Avoid dragging `App.cpp`, full telemetry, or HUD presentation into the detector unit test.

Follow the repository's current CMake/test organization rather than introducing a new build system pattern.

---

# Part N — Non-goals

## 46. Explicit non-goals

Do not implement any of the following in R3:

- production `GameSessionController` cutover;
- HUD visibility behavior changes;
- FPS target changes;
- `committedProcessId_` renaming/removal;
- graphics API target changes;
- production `ForegroundTracker` cleanup;
- legacy `GameDetectionCoordinator` removal;
- legacy `ProductionProcessLifetimeWatcher` removal;
- production `GameRenderVerifier` redesign;
- Steam AppID-to-PID resolution;
- Steam process scanning;
- foreground polling;
- window polling;
- `EnumWindows` polling loops;
- WMI process polling;
- PDH TopGPU production use;
- API2 FPS values as game identity;
- Game Bar active-state heuristics;
- Ghost-window PID retention;
- title/class-name game scoring;
- fuzzy executable scoring;
- HUD renderer/presentation changes;
- VRR presentation contract changes.

---

# Part O — Acceptance criteria

## 47. Functional acceptance criteria

R3 is complete when all of the following are true:

1. A new `ForegroundGameDetector` core exists beside the legacy coordinator.
2. It has only `Hidden`, `NeedsRendererVerification`, and `Eligible` semantics; no committed state exists.
3. R1 `GameScreenAdmission` is always applied before known-game evidence.
4. Exact `GameProcessInstance` identity is required for cache-backed eligibility.
5. Microsoft-known admitted foreground processes are immediately eligible.
6. Renderer-known admitted foreground processes are immediately eligible.
7. Unknown admitted foreground processes request renderer verification.
8. Repeated evaluation of the same unknown process generation deduplicates the outstanding verification request.
9. Different processes/process generations receive distinct verification request identities.
10. Renderer completion stores evidence for the exact process generation only.
11. Renderer completion never directly makes a background process the current eligible target.
12. Stale completion after Alt+Tab leaves the current foreground decision unchanged.
13. A previously known live Game A cannot block independent evaluation of foreground Game B.
14. PID reuse cannot inherit previous generation evidence.
15. Steam-only context never produces `Eligible`.
16. No production HUD/FPS/game-session authority is intentionally changed in this PR.
17. Existing legacy game-detection tests still pass.
18. Existing HUD presentation/VRR contract tests still pass.
19. New focused detector scenario tests pass in Release and Debug configurations used by the project.
20. Full repository build/test validation remains clean except for any explicitly documented pre-existing unrelated issue.

---

## 48. Expected next PR

After R3 is merged and the core is fully unit-tested, proceed to:

```text
PR4 — Cut GameSessionController Over to Foreground-First Detection
```

PR4 will be the first PR that intentionally changes live In-Game Only game-detection behavior.

Its job will be to connect:

```text
EVENT_SYSTEM_FOREGROUND
SHOW / HIDE / LOCATIONCHANGE / DESTROY
Microsoft identity events
Steam RunningAppID context
GameRenderVerifier completion
```

into the R3 core while retaining temporary compatibility hooks for downstream HUD/FPS migration.

Do not pull that production cutover forward into R3.
