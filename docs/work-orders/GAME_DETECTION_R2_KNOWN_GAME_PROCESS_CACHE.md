# Work Order — Game Detection R2: Process-Generation-Aware Known Game Cache

Status: implementation work order  
Repository: `onehoon/ClawHUD`  
Baseline: latest `main` after PR #200 (`a62d189705ba9b38d7725a607c5b983f74cb5f17`)  
Parent design: `docs/GAME_DETECTION_REDESIGN_PR_PLAN_2026-08-31.md`  
Field evidence: `docs/GAME_DETECTION_FIELD_ANALYSIS_2026-08-31.md`  
R1 foundation: `docs/work-orders/GAME_DETECTION_R1_FOREGROUND_SCREEN_ADMISSION.md`

---

## 1. Goal

Implement the second foundation slice of the foreground-first In-Game Only redesign.

This PR must introduce a reusable, process-generation-aware cache for positive game evidence so later detector logic can remember that a process is a known game without making that process globally authoritative.

The new foundation must answer questions such as:

```text
Have we already proven that this exact process instance is a Microsoft/Xbox game?
Have we already verified renderer activity for this exact process instance?
Did a numeric PID get reused for a different process generation?
Can multiple known games coexist without one blocking another?
```

The core invariant is:

> Game evidence is remembered per **process instance** (`PID + process creation time`), never by numeric PID alone.

This PR should also converge the existing `MicrosoftGameTrigger` process-generation cache onto the shared process-instance abstraction and shared known-game evidence store.

The important boundary is:

> Build and populate the known-game cache foundation, but do **not** make it authoritative for HUD visibility, FPS targeting, current foreground selection, Steam PID selection, renderer-verifier ownership, or the legacy `Committed` state machine yet.

PR3 will build the new foreground-first detector core on top of this cache.

---

## 2. Why this PR exists

The current production architecture remembers one candidate/committed PID globally. The field run demonstrated that this is structurally unsafe: a false positive such as `WindowsTerminal.exe` can become `Committed`, remain alive, and prevent later real games from being considered.

The target design instead separates:

```text
Process is known to be a game
```

from:

```text
This process is the current foreground game screen
```

The first statement may remain true while the process is backgrounded. The second must be re-evaluated from the current foreground HWND/PID.

Therefore the redesign needs persistent positive evidence that survives Alt+Tab without becoming a sticky target.

### 2.1 Numeric PID alone is not sufficient

Windows can reuse a numeric PID after a process exits. Any cache keyed only by PID can incorrectly transfer old game evidence to an unrelated later process.

The existing `MicrosoftGameTrigger` already protects against this by pairing:

```text
PID
+
GetProcessTimes() creation time
```

This PR should generalize that exact concept into a shared game-process identity abstraction instead of keeping a Microsoft-specific duplicate.

### 2.2 Multiple games must be allowed to coexist

The cache must support scenarios such as:

```text
Game A remains alive in the background
Game B launches and becomes foreground
```

Both processes may be known games simultaneously.

No cache API may encode:

```text
one current game globally
one committed game globally
new known game replaces all old known games
```

Those concepts belong to the legacy state machine and are intentionally not part of this new foundation.

---

## 3. Current-main context

Review current `main` before editing. At the baseline for this work order, PR #200 has already landed and provides `GameScreenAdmission` plus HIDE / LOCATIONCHANGE WinEvent support.

### 3.1 Existing Microsoft process identity

`src/ClawHUD/GameDetection/MicrosoftGameTrigger.h` currently defines:

```cpp
struct MicrosoftGameProcessIdentity
{
    DWORD processId{};
    ULONGLONG creationTime{};
};
```

with helpers equivalent to:

```cpp
bool IsSameMicrosoftGameProcessInstance(...);
std::optional<MicrosoftGameProcessIdentity>
QueryMicrosoftGameProcessIdentity(DWORD processId) noexcept;
```

`QueryMicrosoftGameProcessIdentity()` currently uses:

```text
OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)
GetProcessTimes()
FILETIME creation -> ULONGLONG
```

This is the right process-generation mechanism. Generalize it rather than creating a second implementation.

### 3.2 Existing Microsoft positive cache

`MicrosoftGameTrigger` currently owns:

```cpp
std::unordered_map<DWORD, ULONGLONG> positiveProcessCache_;
```

On CREATE/SHOW events it:

1. queries the current process creation time;
2. treats the same PID + same creation time as a Microsoft-positive cache hit;
3. otherwise executes the full `WindowsGameIdentityProbe`;
4. stores a positive result by PID + creation time.

This optimization is useful, but its storage should converge into the shared known-game cache introduced by this PR.

### 3.3 Existing active detector remains legacy

`GameSessionController` still owns:

```text
GameDetectionCoordinator
SteamRunningAppTrigger
GenericForegroundTrigger
MicrosoftGameTrigger
ProductionProcessLifetimeWatcher
GameRenderVerifier
ForegroundTracker
```

The coordinator still uses the legacy candidate/Ready/Committed semantics.

Do not cut this over in R2.

### 3.4 R1 is already available

`GameScreenAdmission` and the extended production WinEvent source are now available after PR #200.

Do not modify their policy in this PR unless a direct compile/integration fix is required.

---

## 4. Scope summary

Implement:

```text
shared GameProcessInstance abstraction
+ safe process-generation query helper
+ KnownGameProcessCache
+ evidence merge semantics
+ PID-reuse protection
+ MicrosoftGameTrigger migration to shared process identity/cache
+ GameSessionController ownership of the shared cache
+ deterministic tests
+ minimal CMake registration
```

Do not yet implement:

```text
ForegroundGameDetector
current foreground game authority
HUD/FPS target cutover
renderer completion -> known cache production integration
Steam session -> PID authority
legacy coordinator removal
legacy lifetime watcher removal
```

---

# Part A — shared process-instance identity

## 5. Add a generic game-process identity type

Create a reusable type under the game-detection domain.

Suggested files:

```text
src/ClawHUD/GameDetection/GameProcessInstance.h
src/ClawHUD/GameDetection/GameProcessInstance.cpp
```

Suggested shape:

```cpp
struct GameProcessInstance
{
    DWORD processId{};
    ULONGLONG creationTime{};

    friend bool operator==(
        const GameProcessInstance&,
        const GameProcessInstance&) = default;
};
```

The exact names may vary, but preserve the semantic identity:

```text
same numeric PID + same creation time = same process instance
same numeric PID + different creation time = different process instance
```

Do not use executable name as part of process identity.

Do not use HWND as part of process identity. One process may own multiple windows and windows can be recreated while the process remains the same game instance.

---

## 6. Add one process-generation query helper

Provide one reusable function equivalent to:

```cpp
std::optional<GameProcessInstance>
QueryGameProcessInstance(DWORD processId) noexcept;
```

Required behavior:

```text
PID == 0
-> nullopt

OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) fails
-> nullopt

GetProcessTimes() fails
-> nullopt

success
-> { processId, creationTime }
```

Use the existing Microsoft implementation as the source behavior:

```cpp
FILETIME creation{};
FILETIME exit{};
FILETIME kernel{};
FILETIME user{};

GetProcessTimes(...)

ULARGE_INTEGER value{};
value.LowPart = creation.dwLowDateTime;
value.HighPart = creation.dwHighDateTime;
```

No process handle should be retained by this identity helper.

Close the handle on all paths.

A small local RAII wrapper is acceptable if it improves safety, but do not introduce a broad process-management abstraction in this PR.

---

## 7. Remove Microsoft-specific duplicate identity concepts

After the shared type exists, remove or replace:

```text
MicrosoftGameProcessIdentity
IsSameMicrosoftGameProcessInstance
QueryMicrosoftGameProcessIdentity
```

with the shared `GameProcessInstance` API.

Do not keep two independent PID-generation types with identical semantics.

Tests that currently exercise Microsoft process identity should be updated to exercise the shared abstraction or Microsoft trigger behavior using the shared type.

---

# Part B — `KnownGameProcessCache`

## 8. Add the cache component

Suggested files:

```text
src/ClawHUD/GameDetection/KnownGameProcessCache.h
src/ClawHUD/GameDetection/KnownGameProcessCache.cpp
```

The cache is a positive-evidence store.

It is **not**:

```text
a target selector
a current foreground tracker
a Steam game resolver
a process scanner
a lifetime watcher
a scoring engine
```

It should remain small and deterministic.

---

## 9. Evidence model

Use an evidence structure equivalent to:

```cpp
struct KnownGameEvidence
{
    bool microsoftGameIdentity{};
    bool rendererVerified{};
    bool observedDuringSteamSession{};
};
```

The first two fields are positive game evidence.

`observedDuringSteamSession` is context only.

Define game-known semantics explicitly:

```cpp
bool IsKnownGameEvidence(const KnownGameEvidence& evidence) noexcept
{
    return evidence.microsoftGameIdentity ||
           evidence.rendererVerified;
}
```

Therefore:

```text
Microsoft identity only          -> known game
Renderer verified only           -> known game
Both                              -> known game
Steam-observed only               -> NOT a known game
No evidence                       -> not known
```

Do not allow Steam context alone to become game identity.

---

## 10. Recommended cache storage shape

A simple per-PID map is sufficient if every entry stores the complete process generation.

For example:

```cpp
struct KnownGameProcessEntry
{
    GameProcessInstance process;
    KnownGameEvidence evidence;
};

std::unordered_map<DWORD, KnownGameProcessEntry> entries_;
```

This avoids a custom hash for the compound key while still being PID-reuse safe.

The invariant is:

> At most one current cached generation exists for a numeric PID. Observing a different creation time for that PID invalidates/replaces the old generation.

A compound-key map is also acceptable if implemented cleanly, but do not introduce unnecessary complexity solely for theoretical multiple generations of the same numeric PID being simultaneously alive; Windows does not have two live processes with the same PID.

---

## 11. Required cache operations

Expose a narrow API equivalent to:

```cpp
class KnownGameProcessCache
{
public:
    void MarkMicrosoftGame(const GameProcessInstance& process) noexcept;
    void MarkRendererVerified(const GameProcessInstance& process) noexcept;
    void MarkObservedDuringSteamSession(
        const GameProcessInstance& process) noexcept;

    std::optional<KnownGameEvidence> Lookup(
        const GameProcessInstance& process) noexcept;
    bool IsKnownGame(const GameProcessInstance& process) noexcept;

    void Remove(const GameProcessInstance& process) noexcept;
    void Clear() noexcept;
};
```

Exact names may differ.

Keep the API evidence-oriented.

Do not expose generic mutable access to the internal map.

---

## 12. Evidence merge behavior

Mark operations must merge evidence for the same process generation.

Example:

```text
MarkMicrosoftGame(PID 100, generation A)
-> microsoft=true

later MarkRendererVerified(PID 100, generation A)
-> microsoft=true
-> renderer=true
```

Never clear an already-known positive flag when adding another flag.

---

## 13. PID-reuse behavior

This is the most important cache correctness requirement.

If the cache contains:

```text
PID 100, creation A
microsoft=true
```

and later it sees:

```text
PID 100, creation B
```

then generation A evidence must not be returned for B.

Preferred behavior:

```text
lookup/mark for PID 100 generation B
-> detect generation mismatch
-> discard/replace stale generation A entry
-> generation B starts with only the evidence explicitly supplied for B
```

A stale generation must never leak any of:

```text
microsoftGameIdentity
rendererVerified
observedDuringSteamSession
```

into a reused PID.

---

## 14. Remove semantics

`Remove(process)` must be generation-aware.

Example:

```text
cache currently contains PID 100 generation B
Remove(PID 100 generation A)
-> MUST NOT erase generation B
```

Only remove when both PID and creation time identify the cached generation.

This is important for future asynchronous process-exit/verifier events that may arrive after a PID generation has changed.

---

## 15. Cache concurrency model

Keep the cache owner-thread based.

The current production game-session event sources marshal their meaningful state transitions back through the controller/message path. The future renderer-verifier completion is also consumed by `GameSessionController` rather than mutating detector state directly from an arbitrary worker callback.

Therefore this PR should **not** add mutexes or lock-free machinery to `KnownGameProcessCache` without an actual cross-thread mutation path.

Document the ownership expectation if useful:

> `KnownGameProcessCache` is owned and mutated by `GameSessionController` on its normal event/message handling path.

Avoid defensive synchronization for hypothetical future use.

---

# Part C — Microsoft identity integration

## 16. `GameSessionController` should own the shared cache

Add one cache instance to `GameSessionController`, declared before components that depend on it.

Conceptually:

```cpp
KnownGameProcessCache knownGameProcesses_;
MicrosoftGameTrigger microsoftGameTrigger_{knownGameProcesses_};
```

The exact member ordering must satisfy normal C++ initialization order.

The cache belongs to the game-session/detection domain, not `App`, HUD presentation, or telemetry.

Do not add an `App` hook for the cache.

---

## 17. Migrate `MicrosoftGameTrigger` to the shared cache

Remove the trigger's private duplicate:

```cpp
std::unordered_map<DWORD, ULONGLONG> positiveProcessCache_;
```

Instead, give `MicrosoftGameTrigger` a non-owning reference to `KnownGameProcessCache`.

Recommended production constructor shape:

```cpp
explicit MicrosoftGameTrigger(
    KnownGameProcessCache& knownGames) noexcept;
```

For tests, preserve dependency injection for:

```text
Windows game identity probe
process-instance query
```

For example:

```cpp
using ProbeFunction =
    std::function<WindowsGameIdentityProbeResult(DWORD)>;
using ProcessInstanceQuery =
    std::function<std::optional<GameProcessInstance>(DWORD)>;

MicrosoftGameTrigger(
    KnownGameProcessCache& knownGames,
    ProbeFunction probe,
    ProcessInstanceQuery identityQuery);
```

Exact ordering/naming may vary.

---

## 18. Microsoft cache-hit semantics

When inspecting a relevant CREATE/SHOW event:

```text
query GameProcessInstance
-> cache lookup for exact process generation
-> if evidence.microsoftGameIdentity == true
   -> emit MicrosoftGameTriggerEvidence without re-running the full probe
```

Important:

```text
rendererVerified == true
```

must **not** be treated as a Microsoft identity cache hit.

A process that was generically renderer-verified is a known game for future foreground admission, but that does not mean `MicrosoftGame.config` identity was ever proven.

Use the specific evidence flag.

---

## 19. Microsoft positive-probe semantics

If there is no Microsoft identity cache hit:

```text
run WindowsGameIdentityProbe
```

If `HasReadableMicrosoftGameExecutableMatch(result)` is true and a `GameProcessInstance` was successfully resolved:

```cpp
knownGames.MarkMicrosoftGame(instance);
```

Then emit the same legacy `MicrosoftGameTriggerEvidence` as before.

The active legacy coordinator still receives the evidence through the existing path.

This preserves production behavior while also populating the new cache for PR3.

---

## 20. Identity-query failure must preserve current functional behavior

The existing code treats process-generation identity as a cache optimization. If creation-time lookup fails, it still runs the full Microsoft game probe.

Preserve that behavior.

Required flow:

```text
process-instance query fails
-> do NOT use cache
-> run full Microsoft identity probe

full probe negative
-> no evidence

full probe positive
-> emit Microsoft trigger evidence as today
-> do not persist it in KnownGameProcessCache because safe process generation is unavailable
```

Do not make Microsoft detection newly depend on `GetProcessTimes()` success.

The cache must fail closed for persistence without breaking the existing positive identity path.

---

## 21. Negative Microsoft probe behavior

A negative probe must not create a positive cache entry.

Do not add a durable negative cache in this PR.

Reasons include:

- executable/package state may not yet be fully initialized when early window events arrive;
- later SHOW/CREATE evidence may deserve a fresh probe;
- the redesign currently only requires reusable positive evidence.

Keep the cache positive-only.

---

## 22. Preserve Microsoft trigger event scope

R1 added HIDE and LOCATIONCHANGE events to `ProductionGameWindowSource`.

Do not broaden Microsoft identity probing in R2.

`ShouldInspectMicrosoftGameWindowEvent()` should continue to accept only the existing relevant top-level events:

```text
CREATE
SHOW
```

and continue rejecting:

```text
HIDE
LOCATIONCHANGE
DESTROY
non-top-level events
PID 0
```

LOCATIONCHANGE will be consumed by the new foreground detector in a later PR; it is not a reason to repeatedly inspect `MicrosoftGame.config`.

---

# Part D — renderer and Steam evidence placeholders

## 23. Add renderer evidence support but do not wire it into production yet

The cache must support:

```cpp
MarkRendererVerified(process)
```

because PR3/PR4 will use the existing `GameRenderVerifier` to mark generic game processes.

However R2 must not change the current verifier completion behavior or legacy coordinator state transitions.

Specifically do not modify:

```text
GameRenderVerifier ownership
StartCandidateRenderVerification()
HandleGameRenderVerifierEvent()
Ready/Committed transitions
HUD visibility
FPS target
```

solely to populate this flag in R2.

The API and tests are sufficient foundation for this PR.

---

## 24. Steam context must not become game identity

The cache may expose:

```cpp
MarkObservedDuringSteamSession(process)
```

for later use as research/context evidence.

Do not derive a process association from `RunningAppID` in this PR.

Do not implement:

```text
Steam AppID -> PID mapping
Steam active -> current foreground is known game
Steam active -> cache all created windows as games
Steam active -> HUD visible
```

`observedDuringSteamSession` alone must never make `IsKnownGame()` return true.

If there is no clean production call site for this context in R2, leave the API unused until the foreground detector/cutover work. Do not invent weak association logic merely to exercise the field.

---

# Part E — logging

## 25. Preserve useful Microsoft cache diagnostics

Current production debug logging includes concepts equivalent to:

```text
microsoft.identity-cache-hit
microsoft.identity-cache-store
```

Preserve equivalent visibility after migrating to `KnownGameProcessCache`.

Suggested examples:

```text
[GameDetection] known-game.microsoft-cache-hit pid=1234
[GameDetection] known-game.microsoft-cache-store pid=1234
```

or retain the existing message text if that minimizes churn.

Do not add high-volume logs on every cache lookup.

Log meaningful evidence transitions/cache hits only at Debug level.

No user-visible UI is required.

---

# Part F — tests

## 26. Add dedicated `KnownGameProcessCache` tests

Suggested file:

```text
tests/KnownGameProcessCacheTests.cpp
```

Prefer direct deterministic construction of `GameProcessInstance` values instead of depending on OS PID reuse in CI.

Cover at minimum all scenarios below.

### 26.1 Empty cache

```text
Lookup(PID 100, generation A) -> none
IsKnownGame(...) -> false
```

### 26.2 Microsoft evidence

```text
MarkMicrosoftGame(100/A)
-> lookup returns microsoft=true
-> IsKnownGame=true
```

### 26.3 Renderer evidence

```text
MarkRendererVerified(100/A)
-> renderer=true
-> IsKnownGame=true
```

### 26.4 Evidence merge

```text
MarkMicrosoftGame(100/A)
MarkRendererVerified(100/A)
-> both flags true
```

### 26.5 Steam-only context is not a game verdict

```text
MarkObservedDuringSteamSession(100/A)
-> steam context true
-> microsoft=false
-> renderer=false
-> IsKnownGame=false
```

### 26.6 Multiple known games coexist

```text
100/A Microsoft
200/B renderer verified
-> both entries remain independently known
```

No operation on 200/B should replace 100/A simply because it is newer.

### 26.7 PID reuse cannot inherit game evidence

```text
MarkMicrosoftGame(100/A)
Lookup(100/B)
-> no Microsoft evidence
-> no renderer evidence
-> no Steam evidence
-> old generation is not returned
```

If the implementation evicts stale generation A during lookup, verify that behavior too.

### 26.8 Marking a reused PID starts a clean generation

```text
MarkMicrosoftGame(100/A)
MarkRendererVerified(100/B)
-> current 100 entry is generation B
-> renderer=true
-> microsoft=false
```

Old generation A's Microsoft flag must not leak into B.

### 26.9 Generation-aware remove

```text
current cache = 100/B
Remove(100/A)
-> 100/B remains

Remove(100/B)
-> entry removed
```

### 26.10 Clear

Populate multiple entries, call `Clear()`, verify no evidence remains.

---

## 27. Add/update process-instance tests

Cover the pure identity semantics:

```text
100/A == 100/A
100/A != 100/B
100/A != 200/A
```

Also exercise `QueryGameProcessInstance()` where practical:

```text
PID 0 -> nullopt
current process PID -> returns matching PID and non-zero creation time
```

Do not build timing-sensitive PID-reuse integration tests.

---

## 28. Update MicrosoftGameTrigger tests

The trigger tests must prove the shared cache did not change existing functional semantics.

Cover at minimum:

### Positive result stores shared evidence

```text
CREATE/SHOW for PID 100 generation A
probe => Microsoft positive
-> trigger evidence emitted
-> cache microsoftGameIdentity=true for 100/A
```

### Same generation reuses Microsoft cache

```text
second relevant event for 100/A
-> no second full probe
-> trigger evidence emitted from microsoft identity cache hit
```

### PID reuse forces a fresh probe

```text
first event: 100/A positive
later event: 100/B
-> must not use A cache
-> full probe runs again
```

### Renderer-only known evidence is not a Microsoft cache hit

```text
cache 100/A rendererVerified=true only
Microsoft trigger inspects 100/A
-> full Microsoft probe must still run
```

This prevents evidence-type confusion.

### Identity query failure preserves positive detection

```text
process-instance query -> nullopt
full Microsoft probe -> positive
-> trigger evidence emitted
-> shared cache not persisted for unsafe/unknown generation
```

### Negative probe is not cached

```text
probe negative
-> no trigger evidence
-> no Microsoft cache flag
```

### Event filtering remains unchanged

```text
CREATE/SHOW top-level -> eligible for inspection
HIDE -> ignored
LOCATIONCHANGE -> ignored
DESTROY -> ignored
non-top-level -> ignored
```

---

## 29. Regression tests to keep green

Run the full existing suite, especially:

```text
ProductionTargetPolicyTests
ProductionGameWindowSourceTests
GameScreenAdmissionTests
MicrosoftGameTriggerTests / existing Microsoft identity tests
GameSessionController-related tests
```

R2 should not change R1 admission behavior or event mapping.

---

# Part G — build integration

## 30. CMake

Add the new production sources to the appropriate ClawHUD target:

```text
GameProcessInstance.cpp
KnownGameProcessCache.cpp
```

Register focused test targets following the repository's current test organization.

Do not perform unrelated CMake test-layout cleanup in this PR.

If an existing test target already compiles the required production source files, extend it minimally instead of duplicating broad dependency lists.

---

# Part H — hard constraints

## 31. No active detector cutover

R2 must not change the final result of production game detection.

The existing legacy coordinator still decides candidate/Ready/Committed state until later PRs.

Microsoft positive evidence may now populate the new cache in parallel, but its existing trigger path into `GameDetectionCoordinator` remains intact.

Do not make `KnownGameProcessCache::IsKnownGame()` drive any production visibility decision yet.

---

## 32. No sticky target in the new cache

Do not add APIs such as:

```text
SetCurrentGame()
CommittedGame()
PrimaryGame()
ActiveGamePid()
ReplaceCurrentGame()
```

The cache stores evidence only.

Current foreground authority belongs to PR3's `ForegroundGameDetector`.

---

## 33. No process polling or global scanning

Do not add:

```text
process-list polling
EnumProcesses timers
WMI WITHIN queries
periodic cache cleanup timers
repeated EnumWindows
PDH polling
```

Generation validation occurs when an observed PID is queried/marked.

A separate polling cleanup loop is unnecessary.

---

## 34. Do not add one lifetime watcher per cached game

The cache may contain multiple known game processes.

Do not create a dedicated wait thread/handle watcher for every cache entry merely to remove entries immediately at process exit.

Process-generation validation already prevents stale evidence from being applied to a reused PID.

Later cleanup/lifetime simplification is planned separately.

---

## 35. Preserve FPS provider separation

Do not modify `ProductionTelemetryController` or the FPS provider semantics in this PR.

The canonical separation remains:

```text
policy selects PID
-> FPS provider receives PID
-> provider calculates/publishes telemetry for that PID only
```

The cache does not know FPS values and must not query PresentMon directly.

`rendererVerified` is only a boolean evidence slot for later verifier integration.

---

## 36. Preserve Steam semantics

`SteamRunningAppID` remains session context only.

Do not infer a game PID from the AppID.

Do not make Steam-active state sufficient to mark `microsoftGameIdentity`, `rendererVerified`, or `IsKnownGame()`.

---

## 37. HUD presentation / VRR safety contract — non-negotiable

This game-detection foundation must not modify, replace, weaken, or work around any production HUD presentation invariant.

Do not touch:

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
Presentation API / DirectComposition production presentation path
premultiplied-alpha presentation contract
```

No HUD rendering/presentation change is required for R2.

Existing presentation regression tests/assertions must remain green.

---

# Part I — explicit non-goals

## 38. Do not implement PR3 early

Do not add `ForegroundGameDetector` in this PR.

Do not evaluate:

```text
foreground + fullscreen + known cache
```

as a new production verdict yet.

That is the next PR.

---

## 39. Do not modify fullscreen admission policy

R1 established the current 8-pixel physical monitor coverage rule and exact executable rejection foundation.

Do not tune geometry thresholds in R2 unless a direct correctness bug in R1 is independently demonstrated.

---

## 40. Do not connect renderer verification yet

Do not change `GameRenderVerifier` to write the cache in R2.

R2 defines and tests the evidence slot only.

The verifier ownership/request-generation semantics are part of the foreground-first detector work and should remain isolated to the later PR.

---

## 41. Do not remove legacy Microsoft trigger behavior

The shared cache is an optimization/foundation addition in this PR.

Do not remove:

```text
MicrosoftGameTriggerEvidence
ApplyEvidence()
legacy coordinator wake/transition
```

until the production cutover PR.

---

## 42. Do not add negative game classification

`KnownGameProcessCache` is a positive evidence store.

Do not persist:

```text
knownNotGame
rendererNegative
MicrosoftProbeNegative
blacklistedUntilRestart
```

Negative screen/process admission remains the responsibility of R1 policy and later current-foreground evaluation.

---

# Part J — recommended implementation sequence

## 43. Suggested order inside the PR

Implement in this order:

```text
1. Add GameProcessInstance type/query helper
2. Add deterministic GameProcessInstance tests
3. Add KnownGameEvidence + KnownGameProcessCache
4. Add cache unit tests including PID reuse
5. Migrate MicrosoftGameTrigger identity type to GameProcessInstance
6. Make GameSessionController own the shared cache
7. Inject cache into MicrosoftGameTrigger
8. Replace private Microsoft positiveProcessCache_ with shared cache lookups
9. Update Microsoft trigger tests
10. Register production/test sources in CMake
11. Run focused tests
12. Run full Release test/build validation
13. Run Debug build / focused tests as practical
```

Keeping the process-instance and cache tests green before trigger integration will make regressions easier to isolate.

---

# Part K — acceptance criteria

## 44. Functional acceptance

R2 is complete when all of the following are true:

1. There is one shared process-instance abstraction based on PID + creation time.
2. The Microsoft-specific duplicate process identity type/helper has been removed or fully replaced by the shared abstraction.
3. `KnownGameProcessCache` can store Microsoft, renderer, and Steam-context evidence independently.
4. `IsKnownGame()` is true only for Microsoft or renderer positive evidence, not Steam context alone.
5. Evidence merges for the same process generation.
6. A reused numeric PID cannot inherit any evidence from the old generation.
7. Multiple known game processes can coexist in the cache.
8. Generation-aware remove cannot delete a newer process generation because of a stale event.
9. `GameSessionController` owns the shared cache.
10. `MicrosoftGameTrigger` uses the shared cache for positive Microsoft identity caching.
11. Renderer-only cached evidence does not masquerade as Microsoft identity.
12. Identity-query failure still allows the existing full Microsoft probe to emit positive evidence, while skipping unsafe cache persistence.
13. Existing legacy coordinator behavior remains the production authority.
14. HUD visibility and FPS target behavior are unchanged by this PR.
15. No new polling/global scan/lifetime-watcher fan-out is introduced.
16. HUD/VRR presentation contracts are untouched.

---

## 45. Test acceptance

At minimum, demonstrate passing tests for:

```text
same PID + same generation cache hit
same PID + different generation cache miss/reset
multiple game generations/PIDs coexist
Microsoft evidence merge
renderer evidence merge
Steam-only evidence not known-game
stale-generation Remove does not erase newer generation
Microsoft trigger same-generation cache hit
Microsoft trigger PID reuse re-probe
renderer-only cache does not bypass Microsoft probe
identity-query failure fallback
negative Microsoft probe not cached
CREATE/SHOW-only Microsoft inspection
full existing test suite
```

No timing-dependent test should be required to prove PID reuse.

---

## 46. Expected end state after R2

After this PR, production still behaves through the legacy detector, but the codebase now has the second major building block required for the redesign:

```text
R1
GameScreenAdmission
+ required WinEvents

R2
GameProcessInstance
+ KnownGameProcessCache
+ Microsoft identity stored per process generation

next: R3
ForegroundGameDetector
```

Conceptually, the repository should now be ready to express:

```text
current foreground HWND/PID
-> R1 screen admission
-> R2 exact process-generation lookup
-> known Microsoft/renderer evidence?
-> otherwise request renderer verification
```

without relying on one globally committed PID.

That decision core itself belongs to R3 and must not be implemented early in R2.
