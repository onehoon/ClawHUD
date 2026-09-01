# Work Order — Game Detection R4: Cut `GameSessionController` Over to Foreground-First Detection

Status: implementation work order  
Repository: `onehoon/ClawHUD`  
Implementation baseline: latest `main` after PR #202 (`Add foreground-first game detector core`)  
Validated baseline at authoring time: `fbfacdd1a6468880fde4926322ff41176749e729`  
Parent design: `docs/GAME_DETECTION_REDESIGN_PR_PLAN_2026-08-31.md`  
Field evidence: `docs/GAME_DETECTION_FIELD_ANALYSIS_2026-08-31.md`  
R1 work order: `docs/work-orders/GAME_DETECTION_R1_FOREGROUND_SCREEN_ADMISSION.md`  
R2 work order: `docs/work-orders/GAME_DETECTION_R2_KNOWN_GAME_PROCESS_CACHE.md`  
R3 work order: `docs/work-orders/GAME_DETECTION_R3_FOREGROUND_FIRST_DETECTOR_CORE.md`

---

# 1. Goal

Make the R3 `ForegroundGameDetector` the **production game-screen authority** inside `GameSessionController`.

This is the first redesign PR that intentionally changes live In-Game Only game-detection behavior.

The production decision must become:

```text
current foreground HWND/PID
-> R1 screen admission
-> exact R2 process generation
-> R3 known-game / renderer-verification decision
-> current eligible foreground game PID, or none
```

The production detector must no longer depend on the legacy global state-machine rule:

```text
one candidate
-> Ready
-> Committed
-> keep that PID authoritative while it remains alive
```

The central invariant for this PR is:

> Every meaningful foreground/window transition may cause the **current foreground screen** to be evaluated independently. A previously verified or still-running game may remain in `KnownGameProcessCache`, but it must never block evaluation of a newer foreground PID.

This PR should preserve the current downstream App/HUD/telemetry interfaces through a temporary compatibility bridge where practical. PR5 will cleanly rename and migrate downstream `Committed`/tracked-target semantics.

---

# 2. Required architectural outcome

After R4, production should conceptually be:

```text
ProductionGameWindowSource --------┐
ForegroundTracker -----------------┤
MicrosoftGameTrigger --------------┤
SteamRunningAppIdSource -----------┤
                                   ▼
                         GameSessionController
                                   │
                                   ▼
                         ForegroundGameDetector
                                   │
                    ┌──────────────┴──────────────┐
                    │                             │
                 Eligible              NeedsRendererVerification
                    │                             │
                    │                    GameRenderVerifier
                    │                             │
                    │                     exact request result
                    │                             │
                    └──────────────┬──────────────┘
                                   │
                         fresh foreground recheck
                                   │
                                   ▼
                    current eligible foreground PID
                                   │
                        temporary compatibility bridge
                                   │
          ┌────────────────────────┼────────────────────────┐
          ▼                        ▼                        ▼
 ForegroundTracker          FPS target hook          Graphics API probe
 tracked PID                 existing name            existing hooks
```

The old `GameDetectionCoordinator` and related legacy classes may remain in the repository temporarily for PR6 cleanup, but **they must stop being the production decision authority in R4**.

Do not implement a half-cutover where the new detector decides some paths while `Committed` still vetoes or owns others.

---

# 3. Current-main facts to account for

At the R4 baseline, `GameSessionController` still actively owns and uses:

```text
GameDetectionCoordinator
SteamRunningAppTrigger
GenericForegroundTrigger
MicrosoftGameTrigger
ProductionProcessLifetimeWatcher
GameRenderVerifier
ForegroundTracker
SteamRunningAppIdSource
```

The most important old behavior is in `HandleProductionForegroundChanged()`:

```text
TryCommitReadyCandidateFromForeground()
-> if Committed and old PID alive: return
-> if Ready: return
-> only otherwise consider generic foreground
```

That early-return behavior is the structural bug demonstrated by the field capture. It must disappear from the live production path in this PR.

The old renderer path also maps `GameRenderVerifierEvent` into `GameDetectionCoordinator::generation`, then moves `Verifying -> Ready -> Committed`. R4 must instead map verifier work to R3 `RendererVerificationRequest`.

R1 and R2/R3 foundations already exist on main:

```text
GameScreenAdmission
GameProcessInstance
KnownGameProcessCache
ForegroundGameDetector
```

Use them rather than duplicating policy.

---

# 4. Scope summary

Implement in R4:

```text
ForegroundGameDetector ownership in GameSessionController
fresh-current-foreground evaluation helper
foreground event -> new detector
SHOW/HIDE/LOCATIONCHANGE/DESTROY -> conditional reevaluation
Microsoft positive evidence -> cache + reevaluation, not legacy coordinator
Steam RunningAppID -> SteamSessionContext only
R3 verification request -> production GameRenderVerifier adapter
renderer completion -> exact cache evidence + fresh foreground reevaluation
current Eligible/Hidden/NeedsVerification -> compatibility target effects
removal of live legacy coordinator authority
production logging for new decisions
resume/suspend compatibility
regression tests for real field sequences
```

Do not yet perform the broad cleanup/rename planned for PR5/PR6/PR7.

---

# 5. Non-goals

Do **not** in this PR:

- rename every `setCommittedProcess`/`clearCommittedProcess` downstream API;
- redesign `ProductionTelemetryController` FPS semantics beyond what is required to feed the current eligible PID;
- delete all legacy coordinator source files;
- delete all legacy state-machine tests solely because they are no longer production-authoritative;
- delete `ForegroundTracker`;
- perform the final `ForegroundTracker`/lifetime simplification planned later;
- add Game Bar as a game authority;
- add PDH TopGPU to production;
- add process scanning;
- add `EnumWindows` discovery loops;
- add periodic game-detection polling;
- add arbitrary Steam launch timeouts;
- map Steam AppID directly to PID;
- make renderer activity alone bypass R1 screen admission;
- change PresentMon FPS provider ownership/semantics beyond the verifier adapter;
- modify HUD presentation/window contracts.

---

# 6. HUD / VRR safety contract — mandatory

This game-detection cutover must not modify, replace, weaken, or work around any production HUD presentation invariant:

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
- premultiplied-alpha contract.

No detector issue is justification to change HUD presentation.

Preserve all existing presentation-contract tests/assertions.

---

# Part A — `GameSessionController` ownership and migration boundary

# 7. Add the R3 detector to `GameSessionController`

Add `ForegroundGameDetector` as a controller-owned component using the existing shared `KnownGameProcessCache`.

Conceptually:

```cpp
KnownGameProcessCache knownGameProcesses_;
ForegroundGameDetector foregroundGameDetector_{knownGameProcesses_};
MicrosoftGameTrigger microsoftGameTrigger_{knownGameProcesses_};
```

Member order must respect normal C++ initialization order.

The same `KnownGameProcessCache` must be shared between:

```text
MicrosoftGameTrigger
ForegroundGameDetector
renderer-verification completion handling
```

Do not create a second known-game cache.

---

# 8. Stop feeding production decisions into the legacy coordinator

Once the R4 cutover is complete, the normal production decision path must no longer rely on:

```text
GameDetectionCoordinator::ObserveWake
GameDetectionCoordinator::ReplaceCandidate
GameDetectionCoordinator::MarkRendererReady
GameDetectionCoordinator::CommitCandidate
DecideCandidateDisposition
ShouldCommitReadyCandidate
Committed-state early returns
Ready-state early returns
```

The old objects may remain present temporarily if deleting them would unnecessarily enlarge this PR, but they should be **dormant compatibility/dead code**, not the live authority.

In particular, remove live calls from these paths:

```text
InitializeSteamSession
HandleSteamRunningAppIdChanged
HandleProductionForegroundChanged
HandleMicrosoftGameEvidence
HandleProductionWindowEvent
HandleGameRenderVerifierEvent
```

Do not leave a hidden old veto such as:

```cpp
if (gameDetectionCoordinator_.Context().state == GameDetectionState::Committed)
    return;
```

Any such veto would preserve the field bug.

---

# Part B — one current-foreground evaluation path

# 9. Add one controller helper that reads the current foreground fresh

Prefer one central helper, for example:

```cpp
void EvaluateCurrentForeground(const wchar_t* reason);
```

or equivalent.

Its production flow should be:

```text
1. Check runtime gate
2. GetForegroundWindow()
3. GetWindowThreadProcessId()
4. ObserveGameScreen(foreground, pid)
5. foregroundGameDetector_.Evaluate(observation)
6. ApplyForegroundEvaluation(...)
```

Do not spread slightly different game-admission logic across foreground, Microsoft, renderer, and window-event handlers.

The fresh Win32 read is important. Event payload HWND/PID values are evidence that something changed, not permanent target authority.

---

# 10. Runtime gate

Continue respecting the existing application gate:

```text
HUD disabled
or
system suspended
```

must prevent activation of a production In-Game Only target.

When evaluation is requested while activation is prohibited:

- do not make a new PID eligible;
- do not start a new verifier;
- ensure current compatibility target is not incorrectly made visible;
- preserve the known-game cache unless there is a separate lifecycle reason to remove an exact process generation.

Do not clear useful positive game evidence merely because the HUD is temporarily disabled or the system suspends.

---

# 11. Apply R1 admission before all positive evidence

`ForegroundGameDetector::Evaluate()` already calls R1 `EvaluateGameScreenAdmission()`.

Do not bypass it.

Even if a process is Microsoft-known or renderer-known:

```text
not visible      -> Hidden
minimized        -> Hidden
cloaked          -> Hidden
excluded image   -> Hidden
work-area sized  -> Hidden
fullscreen-like  -> may proceed
```

The R1 `8 px` physical edge tolerance remains the intended policy.

---

# Part C — foreground event path

# 12. Replace `HandleProductionForegroundChanged()` logic

The current function's legacy candidate/Committed behavior should be replaced by fresh evaluation.

Conceptually:

```cpp
void GameSessionController::HandleProductionForegroundChanged(
    HWND, DWORD)
{
    EvaluateCurrentForeground(L"foreground");
}
```

The callback-supplied HWND/PID may be used for diagnostics, but the decision path should prefer a fresh `GetForegroundWindow()` read before admission.

This guarantees:

```text
Game A alive in background
Game B becomes foreground
-> B is evaluated immediately
```

with no old-game liveness veto.

---

# 13. Preserve the existing App foreground-tail hook ordering

`StartForegroundTracking()` currently calls:

```text
hooks_.onForegroundChanged(window, processId)
then game detection handling
```

Preserve ordering unless there is a concrete correctness reason to change it.

That hook feeds generic production telemetry/Always-mode foreground behavior and should remain separate from game identity.

Do not make the FPS provider itself decide whether the PID is a game.

---

# Part D — window lifecycle reevaluation

# 14. Use the R1 event extensions in production

R1 added:

```text
CREATE
SHOW
HIDE
LOCATIONCHANGE
DESTROY
```

to `ProductionGameWindowSource`.

R4 must consume the events for two separate purposes:

```text
CREATE/SHOW
-> Microsoft identity discovery remains allowed

SHOW/HIDE/LOCATIONCHANGE/DESTROY
-> may require reevaluating the current foreground screen
```

Do not treat the event PID itself as the game target simply because it generated an event.

---

# 15. Reevaluate only when the event can affect current foreground state

Avoid broad work on every global WinEvent.

A helper may decide whether the event is relevant by comparing against:

```text
current GetForegroundWindow()/foreground PID
and/or
foregroundGameDetector_.Current() HWND/PID
```

The intent is:

```text
event belongs to current foreground HWND/PID
or
event invalidates/hides/destroys the detector's last current HWND/PID
-> EvaluateCurrentForeground()
```

Unrelated background-window location changes should not trigger expensive process/monitor checks.

Keep the design event-driven and lightweight.

---

# 16. Mandatory Minecraft sequence

The field capture showed:

```text
Minecraft foreground
window 0,0,1920,1128
-> Hidden

same foreground HWND later LOCATIONCHANGE
window -3,-3,1923,1203
-> admitted by 8px tolerance
-> Microsoft-known -> Eligible
```

No second foreground event is required.

R4 must therefore make current-foreground `LOCATIONCHANGE` capable of changing the live decision from Hidden to Eligible/NeedsVerification.

This is a required regression scenario.

---

# 17. Mandatory Mafia sequence

The field capture showed foreground arriving before the game window was fully shown/fullscreen:

```text
foreground event
visible=false / non-fullscreen
-> Hidden

SHOW
LOCATIONCHANGE -> 0,0,1920,1200
-> current foreground reevaluated
-> generic game enters renderer verification
```

R4 must support this without timers or polling.

---

# 18. HIDE / DESTROY behavior

If HIDE or DESTROY invalidates the current eligible game window:

```text
fresh foreground evaluation
-> Hidden or evaluate replacement foreground
```

The HUD/current game target must not remain sticky merely because the old game process itself is still alive.

If Windows has already moved foreground to another real game, that replacement must be evaluated immediately rather than first forcing a long-lived “released” state.

---

# Part E — Microsoft identity integration

# 19. Keep `MicrosoftGameTrigger` as strong identity discovery

`MicrosoftGameTrigger` already:

- inspects CREATE/SHOW only;
- performs exact MicrosoftGame.config executable matching;
- stores generation-safe positive evidence in `KnownGameProcessCache` when process identity is available;
- preserves full-probe fallback when process-generation query fails.

Keep that role.

Do not make `MicrosoftGameTrigger` select the production PID directly.

---

# 20. `HandleMicrosoftGameEvidence()` becomes a reevaluation trigger, not coordinator evidence

Remove the legacy behavior:

```text
Microsoft evidence
-> ApplyProductionEvidence(...)
-> coordinator candidate/replace/commit
```

Instead, after valid Microsoft evidence:

```text
positive cache already populated when process generation was available
-> fresh current foreground reevaluation
```

If the Microsoft process is currently foreground and R1-admitted, it should become `Eligible` immediately.

If it is background, the current foreground decision must remain unchanged.

If process-generation lookup failed during Microsoft probing, the legacy trigger may still emit positive evidence for compatibility, but the new detector must not persist unsafe PID-only identity. The current process can still fall through to renderer verification when foreground.

---

# 21. Preserve Microsoft event message plumbing unless removing it clearly simplifies the patch

At the current baseline, Microsoft inspection runs on the controller's window-event message path and posts `kMicrosoftGameEvidence` back into the same controller queue.

R4 may preserve this message temporarily to minimize migration risk.

If preserved:

- `HandleMicrosoftGameEvidence()` must no longer feed the legacy coordinator;
- it should validate obvious staleness and request current foreground reevaluation;
- duplicate reevaluation is acceptable, but duplicate authority is not.

The extra message can be removed in a later cleanup PR.

---

# Part F — Steam RunningAppID becomes context only

# 22. Initialize R3 Steam session context

In `InitializeSteamSession()`:

```text
read current RunningAppID
-> foregroundGameDetector_.UpdateSteamSession(appId)
```

Retain useful logs.

Do not initialize/arm a production PID candidate through `SteamRunningAppTrigger` after cutover.

---

# 23. Handle Steam AppID transitions as context only

In `HandleSteamRunningAppIdChanged()`:

```text
0 -> N
N -> M
N -> 0
```

should update:

```cpp
foregroundGameDetector_.UpdateSteamSession(currentAppId);
```

The R3 context generation should advance only on semantic AppID changes.

Do not perform:

```text
Steam AppID -> candidate PID
Steam AppID -> foreground process assumed game
Steam AppID -> HUD visible
Steam AppID -> skip R1 admission
```

Steam context may cause an admitted process to record `observedDuringSteamSession`, but that flag alone must not make it `Eligible`.

---

# 24. Remove live Steam window-candidate authority

The current CREATE/SHOW handler has legacy behavior equivalent to:

```text
if coordinator Armed with steamAppId
and event looks like eligible process
-> ObserveWake(SteamRunningAppId, event PID)
```

This must stop being a production-authority path in R4.

A Steam game should become eligible through the same current-screen path as any other game:

```text
current fullscreen foreground
-> unknown exact generation
-> renderer verification
-> known game
-> eligible on fresh current-screen recheck
```

Steam simply provides session/supporting context.

---

# Part G — production renderer-verifier adapter

# 25. Reuse `GameRenderVerifier`; do not rewrite PresentMon verification

The existing production verifier remains the correct low-level mechanism:

```text
PID-filtered PresentMon API2 frame telemetry
-> FirstDisplayedFrame
```

Do not replace it with PDH TopGPU, FPS-number comparison, swapchain address heuristics, PresentMon.exe, or a multi-PID shared DynamicQuery.

R4 only changes **ownership identity and completion routing**.

---

# 26. Adapt R3 `RendererVerificationRequest` onto the existing verifier

R3 returns:

```cpp
RendererVerificationRequest
{
    requestId,
    GameProcessInstance{pid, creationTime}
}
```

Use:

```text
process.processId -> GameRenderVerifier PID
requestId         -> GameRenderVerifier generation/token field
```

The old coordinator generation must no longer be the verifier identity.

---

# 27. Preserve the full process generation in the posted completion payload

This is important.

`GameRenderVerifierEvent` currently contains:

```text
PID
uint64 generation/token
```

but R3 stale-completion safety requires the full original:

```text
PID + process creation time + requestId
```

Therefore the controller adapter should preserve/capture the exact `RendererVerificationRequest` when starting the verifier and include it in the message delivered back to the owner thread.

One acceptable shape:

```cpp
struct GameRenderVerifierUpdate
{
    RendererVerificationRequest request;
    GameRenderVerifierEventType type;
};
```

or equivalent.

When the verifier callback fires, ensure the low-level event matches the request that started it:

```text
event.processId == request.process.processId
event.generation == request.requestId
```

Then post the full request to the controller message queue.

Do not reconstruct process creation time later from numeric PID. That would break stale-generation safety.

---

# 28. Track the currently running verifier request in the controller

The controller should keep enough adapter state to know which exact R3 request is currently using the one production `GameRenderVerifier` worker.

For example:

```cpp
std::optional<RendererVerificationRequest> activeRendererRequest_;
```

This is adapter state only.

It is not a replacement for R3 detector state and must not become a sticky game target.

---

# 29. Do not restart the verifier for repeated identical R3 requests

R3 already deduplicates repeated evaluations of the same unknown exact process generation.

The controller must respect that.

If:

```text
activeRendererRequest == evaluation.verificationRequest
and verifier is running
```

return without stop/start churn.

This is required for frequent SHOW/LOCATIONCHANGE reevaluation.

---

# 30. Handoff when a new unknown foreground process needs verification

Only one `GameRenderVerifier` worker exists.

If Game A is being verified and Game B becomes the new admitted unknown foreground:

```text
R3 returns request B
request B != active request A
-> stop verifier A
-> invalidate/complete A as non-positive in detector if needed to clear reusable request state
-> start verifier B
```

Do not keep verifying A at the expense of current foreground B.

A completion for A that was already posted before handoff may still be consumed safely because its full `RendererVerificationRequest` is preserved and R2 `TryMarkRendererVerified()` is generation-safe.

---

# 31. Foreground temporarily becomes non-game while a verifier is running

If Game A is being verified and the user switches to Explorer before completion, there is no requirement to cancel A immediately.

Useful allowed behavior:

```text
Game A request running
-> Explorer foreground -> Hidden
-> A verifier completes in background
-> exact A generation becomes rendererVerified
-> Explorer remains current Hidden
-> later Alt+Tab back to A -> immediate Eligible
```

This is explicitly supported by the R3 design.

However, if a new foreground Game B needs verification, B takes precedence and A's worker should be handed off as described above.

---

# 32. Verifier start failure

A failed verifier start must not leave the detector permanently believing an unstarted request is outstanding.

Recommended behavior:

```text
R3 emits request X
GameRenderVerifier::Start fails
-> log error
-> report/complete request X as verified=false (or explicitly invalidate it)
-> do not loop immediately
```

A later meaningful foreground/window event may evaluate again and produce a fresh request.

Do not add a retry timer solely for this.

---

# 33. Verifier stop/invalidation semantics

Whenever the controller intentionally stops an active verifier because of:

```text
new target handoff
HUD disable
suspend
shutdown
explicit reset
```

make sure adapter state and R3 outstanding-request state stay coherent.

If R3 only exposes `CompleteRendererVerification({request, false})` for clearing the matching outstanding request, use that for intentional non-positive cancellation where appropriate.

Do not leave:

```text
R3 outstanding request X
but no production verifier exists for X
```

indefinitely.

---

# Part H — renderer completion semantics

# 34. Positive completion updates cache first

On `FirstDisplayedFrame` for exact request A:

```text
foregroundGameDetector_.CompleteRendererVerification({A, true})
```

must be invoked.

R3/R2 will then safely mark exact process generation A renderer-verified using the generation-preserving cache operation.

This must happen even if A is no longer the foreground process, unless the event is otherwise invalid/corrupt.

That preserves useful evidence across Alt+Tab.

---

# 35. Positive completion must never directly activate A

After marking A renderer-verified:

```text
read GetForegroundWindow() again
-> run full R1/R2/R3 evaluation of CURRENT foreground
-> apply that result
```

Do not implement:

```cpp
hooks_.setCommittedProcess(event.processId);
```

directly from a renderer completion.

Do not set `ForegroundTracker` to the completed PID directly.

The completed process may already be backgrounded.

---

# 36. Mandatory stale-completion scenario

Cover:

```text
Game A fullscreen -> request A
Explorer foreground -> Hidden
request A completes
-> cache A rendererVerified
-> current stays Hidden
```

and:

```text
Game A request
Game B becomes foreground
A completion arrives late
-> A evidence may be cached safely
-> B remains the current decision
```

---

# 37. Preserve the PR202 PID-reuse fix

PR #202 fixed an important stale-completion issue with:

```text
KnownGameProcessCache::TryMarkRendererVerified()
```

R4 must use the R3 completion API that preserves this behavior.

Never regress to direct asynchronous:

```cpp
knownGameProcesses_.MarkRendererVerified(oldRequest.process);
```

if that operation can replace evidence for a newer generation already occupying the same numeric PID.

Required invariant:

```text
old PID generation completion
must not erase or alter newer-generation evidence
```

---

# Part I — applying R3 decisions to existing production hooks

# 38. Add one compatibility bridge for the current eligible PID

PR5 will rename downstream terminology. R4 may temporarily keep existing hook names such as:

```text
setCommittedProcess
clearCommittedProcess
```

but the **semantic value passed through them must become the current eligible foreground game PID**, not a sticky lifetime-owned game.

Keep this semantic distinction documented in code comments.

Suggested controller state if needed:

```cpp
DWORD bridgedEligibleProcessId_{};
```

or derive from `ForegroundTracker`/R3 current snapshot.

Do not create another independent authority.

---

# 39. `Eligible` effects

When a fresh evaluation returns `Eligible` for process P:

```text
P is current foreground
P passed R1 admission
P exact generation is known
```

Apply compatibility effects idempotently.

If P is newly eligible:

```text
foregroundTracker_.SetTrackedProcessId(P)
hooks_.setCommittedProcess(P)          // temporary legacy name
hooks_.startGraphicsApiProbe(P) or ensure equivalent current-target probe
hooks_.startProductionSampling()
hooks_.reconcileHudVisibility()
```

Avoid restarting expensive work if P is already the current eligible PID.

If a verifier is still running for P but P became eligible through Microsoft identity, it may be stopped because renderer proof is no longer required for admission.

If a verifier is running for a different old/background process and the current foreground is already strongly known, prefer stopping that obsolete worker rather than letting it consume resources indefinitely.

---

# 40. `Hidden` effects

When current evaluation becomes `Hidden`, the current In-Game Only compatibility target must be cleared immediately.

Conceptually:

```text
foregroundTracker_.SetTrackedProcessId(0)
hooks_.clearCommittedProcess()         // temporary legacy name
stop graphics API probe for prior game target
stop/reset In-Game Only FPS sampling as required by current telemetry API
hooks_.reconcileHudVisibility()
```

Do **not** clear `KnownGameProcessCache` merely because the game backgrounded.

Do **not** tear down global telemetry that is owned by HUD visibility/application lifecycle rather than the current game target.

Preserve the existing separation captured by the committed-target release plan: target release must not incorrectly own global telemetry lifetime.

---

# 41. `NeedsRendererVerification` effects

An admitted but unknown current foreground is **not yet eligible**.

Therefore:

```text
clear any previous current eligible game bridge
hide In-Game Only target if necessary
start/ensure the exact R3 verification request
```

Do not leave Game A visible while Game B is current foreground but still being verified.

This is required to eliminate sticky-current behavior.

---

# 42. Direct eligible-game switch

If Game A is eligible and Game B is already known/eligible when foreground switches directly A -> B:

```text
clear/retarget A-specific effects
-> set B as tracked/current target
-> target FPS/graphics probe at B
-> reconcile visibility
```

No intermediate process-lifetime wait is allowed.

The old A process may remain alive and cached.

---

# Part J — `ForegroundTracker` temporary role

# 43. Keep `ForegroundTracker` as a compatibility presentation/visibility bridge

Do not delete it in R4.

After cutover, its tracked PID should represent:

> the current R3 `Eligible` foreground game PID

not:

> a committed game process retained across Alt+Tab.

Therefore:

```text
Eligible P -> SetTrackedProcessId(P)
Hidden / NeedsVerification -> SetTrackedProcessId(0)
```

This lets existing App code using:

```cpp
gameSession_.ForegroundIsTrackedProcess()
```

continue to drive HUD visibility until PR5 renames downstream semantics.

---

# 44. Alt+Tab behavior under the compatibility bridge

Required live behavior:

```text
verified Game A foreground
-> Eligible
-> tracked PID = A
-> HUD may show

Explorer foreground
-> Hidden
-> tracked PID = 0
-> HUD hides immediately
-> A stays known in cache

Game A foreground again
-> R1 admission pass
-> exact cache hit
-> Eligible immediately
-> tracked PID = A
-> HUD may show immediately
```

Do not re-run renderer verification for the same exact process generation after successful verification.

---

# Part K — process lifetime and PID reuse

# 45. Do not use one `ProductionProcessLifetimeWatcher` as game authority

The legacy watcher exists because the old architecture owned one candidate/Committed PID.

That concept is no longer authoritative.

R4 should stop arming the watcher as part of candidate selection.

The member/message code may remain temporarily if removing it is better deferred to PR6/PR7, but it should no longer decide which game is current.

---

# 46. Cache lifetime remains generation-based

Known-game cache entries do not require one watcher handle per game.

When evaluating a PID:

```text
QueryGameProcessInstance(pid)
```

must match the exact cached generation.

PID reuse therefore naturally misses old evidence.

If an exact current eligible process is detected as exited or generation-changed, clear the current compatibility target and reevaluate foreground.

---

# 47. Rework `CommittedProcessAliveOrNone()` compatibility query

This public method is currently consumed by App lifecycle/resume logic.

Until PR5 cleanup, preserve it with safe new semantics:

```text
no current eligible process -> true
current eligible exact process generation still exists -> true
numeric PID dead or generation no longer matches -> false
```

Do not implement it as only:

```cpp
ProcessAlive(pid)
```

when exact `GameProcessInstance` is available, because PID reuse can make a stale numeric PID appear alive.

The legacy method name may remain temporarily; PR5 can rename it.

---

# 48. Rework `ReleaseCommittedIfForegroundGone()` compatibility entry point

This App-facing method currently exists in HUD reconciliation.

R4 should remove sticky-commit semantics from it.

Acceptable compatibility behavior:

```text
if current bridged eligible process no longer matches its exact process generation
-> clear current compatibility target
-> request fresh foreground evaluation

otherwise
-> no-op
```

Foreground loss itself should already have been handled by foreground/window events and R3 evaluation; this method is only a liveness/recovery safety net now.

Do not use it to retain a background game.

---

# 49. Rework `ClearCandidateIfNotCommitted()` compatibility entry point

`App::StopHud()` currently calls this legacy-named method.

R4 may keep the method name but change its internal meaning to:

```text
cancel/stop active renderer verification
clear current transient compatibility target
preserve known-game cache
preserve Steam session context unless the source itself changes
```

Do not preserve a “Committed” target simply because the method name still contains that word.

PR5/PR6 will clean the API name.

---

# Part L — suspend/resume

# 50. Preserve existing suspend/resume safety

R4 must not break the established lifecycle behavior:

- HUD hidden on suspend;
- production sampling paused appropriately;
- queued game-session events discarded where intended;
- resume recovery remains bounded;
- final successful recovery reevaluates foreground.

Do not redesign all resume logic in this PR.

---

# 51. Stop/cancel verifier coherently on suspend

If suspend stops the current `GameRenderVerifier`, also clear/invalidate the corresponding R3 outstanding request adapter state so a verifier that no longer exists is not treated as active forever.

Do not clear positive known-game cache entries just because of suspend.

After resume/final reevaluation, an already-known exact game may become Eligible immediately; an unknown exact process can receive a new verification request.

---

# 52. Renderer completion during suspend/resume recovery

A renderer completion must never directly show the HUD during suspended/resume-recovery state.

Safe policy:

```text
trusted completion may update exact known-game evidence
but activation waits for fresh foreground evaluation after runtime gate permits it
```

If the existing queue-discard policy drops the completion during suspend, that is also acceptable provided adapter/outstanding-request state is reset coherently and later reevaluation can request verification again.

Avoid creating a second resume-specific detector state machine.

---

# 53. Preserve App-facing compatibility queries for this PR

The current App resume code reads:

```text
TrackedProcessId()
VerifierProcessId()
VerifierRunning()
ForegroundIsTrackedProcess()
```

R4 should keep these functioning enough for existing resume logic.

Recommended semantics:

```text
TrackedProcessId
-> current compatibility eligible PID, or 0

VerifierProcessId
-> currently running R3 verification request PID, or 0

VerifierGeneration
-> requestId while the legacy method name remains

ForegroundIsTrackedProcess
-> current foreground matches the current eligible compatibility PID
```

PR5 may rename/refactor these APIs after live cutover is validated.

---

# Part M — stop/shutdown behavior

# 54. `StopRenderVerification()` must clear both low-level and adapter state

Current code only stops `GameRenderVerifier` and optionally FPS sampling.

After R4 it also needs to keep R3 request state coherent.

Conceptually:

```text
capture activeRendererRequest
stop worker
clear activeRendererRequest_
notify detector request was cancelled/non-positive if required
```

Avoid callback/message races by preserving the existing owner-thread / queued-message model.

A completion already posted before stop may arrive later and must be treated as stale exact evidence, never as current-target authority.

---

# 55. `StopSources()`

On application shutdown:

```text
stop window source
stop foreground source
stop renderer verifier coherently
stop Steam watcher
clear compatibility target as needed
DiscardPendingEvents()
```

The legacy process-lifetime watcher may still be disarmed for cleanup even if it is no longer armed in normal R4 production operation.

No background thread or process handle should remain owned after shutdown.

---

# Part N — logging

# 56. Replace legacy state-transition logs with foreground-first decision logs in the live path

The old production log vocabulary includes:

```text
candidate.start
candidate.replace
RendererReady
Committed
released
steam.armed
ready.waiting-foreground
```

These become misleading once the coordinator is no longer authoritative.

Do not necessarily delete every legacy string/source in R4, but new live decisions should log the new model clearly.

Suggested important logs:

```text
[GameDetection] foreground.evaluate reason=Foreground hwnd=... pid=...
[GameDetection] foreground.hidden pid=... reason=NotFullscreenLike
[GameDetection] foreground.verify-request pid=... creation=... requestId=...
[GameDetection] verifier.start pid=... requestId=...
[GameDetection] renderer.first-frame pid=... requestId=...
[GameDetection] foreground.eligible pid=... source=Microsoft
[GameDetection] foreground.eligible pid=... source=Renderer
[GameDetection] foreground.clear oldPid=... reason=ForegroundChanged
[GameDetection] steam.session oldAppId=... newAppId=... generation=...
```

Exact format may vary.

Keep high-frequency no-op reevaluations at Debug level or suppress them.

Do not log every global LOCATIONCHANGE event.

---

# 57. Admission reason visibility

When a current foreground candidate is hidden by R1, include `GameScreenAdmissionReason` in Debug logging where useful.

This is important for field validation of:

```text
NotFullscreenLike
ExcludedExecutable
Minimized
Cloaked
ProcessUnavailable
```

Avoid excessive user-facing logs.

---

# Part O — required behavior scenarios

# 58. WindowsTerminal regression — mandatory

Sequence:

```text
WindowsTerminal foreground
renderer activity exists
window may otherwise look substantial
```

Required:

```text
R1 executable exclusion -> Hidden
no verifier request
no current eligible game
no HUD target
```

The old field failure must be impossible through the new production authority.

---

# 59. Diablo Steam sequence — mandatory

Expected behavior:

```text
Steam AppID becomes active
-> only SteamSessionContext updates

Diablo renderer/process exists in background
-> no HUD merely because Steam active

Diablo becomes current fullscreen foreground
-> admitted
-> if unknown: NeedsRendererVerification
-> verifier confirms displayed frame
-> exact cache marked
-> fresh current foreground recheck
-> Eligible

Explorer / Search / Ghost becomes foreground
-> Hidden

Diablo returns
-> exact cache hit
-> Eligible immediately
```

A still-live older Diablo PID/generation must not prevent a newer Diablo PID or another game from being evaluated.

---

# 60. Minecraft Microsoft sequence — mandatory

```text
Microsoft identity discovered
-> cache Microsoft evidence

Minecraft first foreground 1920x1128
-> Hidden / NotFullscreenLike

LOCATIONCHANGE to -3,-3,1923,1203
-> R1 admitted
-> exact Microsoft cache hit
-> Eligible immediately
```

No renderer delay is required after strong Microsoft identity if R1 admission is satisfied.

---

# 61. Dave the Diver Steam sequence — mandatory

```text
Steam session active
Dave foreground fullscreen
-> Steam context alone does not authorize
-> unknown exact generation -> verification
-> renderer positive -> fresh reevaluation -> Eligible

Ghost/Explorer foreground
-> Hidden

Dave returns
-> cache hit -> immediate Eligible
```

---

# 62. Mafia generic sequence — mandatory

```text
foreground arrives while window invisible/non-full
-> Hidden

SHOW / LOCATIONCHANGE to fullscreen
-> current foreground reevaluated
-> unknown exact generation -> NeedsRendererVerification
-> displayed frame -> cache
-> fresh current foreground still Mafia/fullscreen -> Eligible
```

CrashReportClient foreground must be rejected/Hidden and must not steal durable game identity.

A Mafia relaunch with a new PID/process generation is independently evaluated.

---

# 63. Two live games — mandatory

```text
Game A verified, remains alive in background
Game B becomes fullscreen foreground
```

Required:

```text
Game B evaluated independently
if unknown -> verification B
A does not block B
```

Then:

```text
B verified
-> cache may contain A and B simultaneously
```

This is the direct structural replacement for legacy sticky `Committed` behavior.

---

# 64. Fast A -> Explorer -> A

Required:

```text
A Eligible
Explorer -> Hidden immediately
A return -> Eligible from cache immediately
```

Do not require A process exit/relaunch.

Do not re-run verifier after exact renderer evidence is cached.

---

# 65. A verifying -> Explorer -> late A completion

Required:

```text
A request active
Explorer -> Hidden
A completion -> exact A cache updated
current remains Explorer/Hidden
A return -> Eligible
```

---

# 66. A verifying -> B foreground

Required:

```text
A request active
B admitted unknown foreground
-> B gets distinct request
-> production verifier hands off to B
```

A stale already-posted completion must not activate A or corrupt B/newer PID-generation evidence.

---

# Part P — tests

# 67. Keep R1/R2/R3 direct tests intact

Do not weaken or remove the already-added direct tests for:

- R1 admission;
- known-game generation safety;
- Steam-only non-authority;
- foreground detector decision flow;
- request deduplication;
- stale completion;
- newer-generation evidence preservation.

R4 should build on them.

---

# 68. Add controller-cutover tests at the narrowest useful level

Prefer deterministic tests over UI/integration-only validation.

If directly constructing `GameSessionController` is heavy because of the PresentMon provider and Win32 sources, extract narrow pure helpers/policies where useful rather than creating a giant mock framework.

Useful testable seams include:

```text
window event should trigger current-foreground reevaluation?
R3 evaluation -> compatibility target transition plan
R3 request -> verifier adapter identity
verifier event -> exact request completion mapping
exact current process generation liveness check
```

Do not introduce an abstract event bus or large DI framework solely for tests.

---

# 69. Required cutover regression tests

Cover at minimum:

### 69.1 Old game does not block new foreground

```text
A eligible/alive
B current/admitted
-> decision/work targets B
```

### 69.2 Hidden clears compatibility target

```text
A eligible
Explorer hidden result
-> tracked/current compatibility target becomes 0
```

### 69.3 NeedsVerification clears old eligible target

```text
A eligible
B admitted unknown
-> A target cleared
-> B verification requested
```

### 69.4 Eligible switch retargets

```text
A eligible
B known eligible
-> target changes A -> B
```

### 69.5 LOCATIONCHANGE reevaluation

```text
current Minecraft-like screen initially non-full
-> later current LOCATIONCHANGE full
-> reevaluation occurs
```

### 69.6 HIDE/DESTROY current window

```text
current eligible window hidden/destroyed
-> fresh current foreground evaluation
-> no sticky old target
```

### 69.7 Steam AppID no PID authority

```text
Steam session active
unrelated admitted/unknown process
-> still requires renderer verification
```

### 69.8 Microsoft positive background evidence

```text
Microsoft game evidence arrives for background process A
Explorer is foreground
-> cache A may update
-> current remains Hidden
```

### 69.9 Renderer stale completion

```text
request A
current changes to B/Hidden
completion A
-> A cache only
-> current unchanged until fresh reevaluation
```

### 69.10 Verifier handoff

```text
request A running
request B becomes current
-> A stopped/inactivated
-> B starts
-> adapter identities remain distinct
```

### 69.11 PID generation liveness

```text
current target PID 5000 creation A
query now returns PID 5000 creation B
-> old current target considered invalid
```

---

# 70. Existing HUD presentation regression tests must remain green

Because R4 changes when visibility can be reconciled, ensure all existing tests/assertions around:

- click-through;
- no activation;
- topmost;
- transparent hit testing;
- independent flip;
- premultiplied alpha;
- production presentation contract

remain unchanged and passing.

Do not “fix” a detector test by changing presentation semantics.

---

# Part Q — field validation after merge

# 71. R4 is a field-validation milestone

After implementation, this is the first redesign PR worth rerunning against the mixed real-game field sequence.

Recommended manual/Diag validation order:

```text
WindowsTerminal / Explorer baseline
Diablo IV
Minecraft for Windows
Dave the Diver
Mafia: The Old Country
Alt+Tab / Search / Ghost / crash UI transitions
```

Expected production logs should make it possible to confirm:

```text
which foreground was evaluated
R1 admission reason
exact process generation/request ID
renderer verification
current Eligible/Hidden transition
Steam context only
```

The new production detector should no longer report a long-lived unrelated desktop process as an authority that suppresses later games.

---

# 72. Specific field acceptance checks

### WindowsTerminal

```text
never becomes current eligible game
```

### Minecraft

```text
1920x1128 -> Hidden
-3,-3,1923,1203 -> Eligible after LOCATIONCHANGE
```

### Steam game startup

```text
RunningAppID may lead foreground by seconds
but HUD does not show until current screen is admitted/known
```

### Alt+Tab

```text
HUD hides immediately on non-game foreground
known game remains cached
return is immediate
```

### Multiple game PIDs

```text
new foreground PID is evaluated even while old game PID remains alive
```

---

# Part R — acceptance criteria

# 73. R4 is complete only when all statements below are true

## Production authority

- `ForegroundGameDetector` is the live production game-screen decision authority inside `GameSessionController`.
- `GameDetectionCoordinator::Committed` no longer vetoes or owns normal production foreground selection.
- a live older game cannot block a newer foreground PID.

## Screen admission

- every eligible game must pass R1 current-screen admission;
- positive cache evidence never bypasses visibility/minimized/cloak/exclusion/fullscreen checks;
- current LOCATIONCHANGE can cause reevaluation without a new foreground event.

## Known-game evidence

- exact Microsoft evidence can make an admitted current game immediately Eligible;
- renderer evidence is process-generation aware;
- multiple known games coexist;
- backgrounding a game does not erase its positive cache;
- PID reuse does not inherit old evidence.

## Steam

- RunningAppID updates `SteamSessionContext` only;
- Steam AppID does not choose PID;
- Steam context alone cannot make a process Eligible.

## Renderer verification

- R3 request ID, PID, and creation time are preserved through the controller adapter;
- repeated identical requests do not restart the verifier;
- new foreground unknown game can hand off the verifier;
- late completion updates exact cache evidence only;
- late completion never directly activates its PID;
- PR202 newer-generation-preservation behavior remains intact;
- verifier start failure/cancellation does not leave an orphaned outstanding request forever.

## Compatibility bridge

- existing App/HUD downstream interfaces may retain legacy names temporarily;
- their effective PID represents the **current eligible foreground game**, not a sticky lifetime-owned game;
- Hidden/NeedsVerification clears the prior game target;
- direct known-game switch retargets immediately;
- global telemetry ownership is not accidentally moved into game-target release.

## Lifecycle

- suspend/resume remains bounded and safe;
- shutdown leaves no verifier/source worker behind;
- current target liveness is process-generation aware where exact identity exists.

## VRR/presentation

- no HUD presentation contract item is changed;
- all presentation/independent-flip/premultiplied-alpha safety tests remain green.

---

# 74. Expected next PR after R4

Do not include broad downstream renaming/cleanup in this work unless directly required for correctness.

The planned next slice is:

```text
PR5 — Replace downstream “Committed PID” semantics/names with
      “Current In-Game Foreground PID” semantics
```

That PR should cleanly migrate:

```text
ProductionTelemetryController committedProcessId_
setCommittedProcess / clearCommittedProcess naming
HUD visibility queries
FPS target naming
Graphics API target naming where appropriate
mode-switch/resume-facing terminology
```

R4's job is to make the **new detector behavior real and correct first**.

---

# 75. Final implementation principle

The live production detector after this PR should be understandable with one sentence:

> **ClawHUD shows In-Game Only telemetry only for the currently admitted foreground process instance that is already strongly identified or renderer-verified; all other evidence is context or cache, never sticky target authority.**
