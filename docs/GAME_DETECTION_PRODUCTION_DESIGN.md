# ClawHUD Production Game Detection Design

Status: Final implementation design after diagnostic field validation  
Date: 2026-08-29  
Repository: `onehoon/ClawHUD`

---

## 1. Purpose

This document records the final production game-detection architecture agreed after implementing and field-testing the diagnostic sources for Windows game detection.

It is intentionally detailed so that a later implementation session can continue without reconstructing the design discussion from chat history.

The design is based on real ClawHUD logs covering:

- Steam game launch, gameplay, repeated Alt+Tab, and exit
- Non-Steam Win32 game launch, gameplay, repeated Alt+Tab, and exit
- Xbox / Microsoft Store packaged game launch, Gaming Services UI transitions, Windows picker/security UI transitions, gameplay, Alt+Tab, and exit
- Global PresentMon diagnostic capture
- Windows package/AppModel and `MicrosoftGame.config` identity probing
- Top-level window lifecycle events
- Steam `RunningAppID` registry notifications

The central conclusion is:

> ClawHUD should not have three separate game detectors with duplicated validation logic. It should have three independent event-driven wake-up triggers feeding one shared detection/verification/commit pipeline.

The three production triggers are:

1. Generic foreground trigger
2. Steam `RunningAppID` trigger
3. Microsoft/Xbox GameIdentity trigger

The trigger is not the verdict. A trigger only wakes or accelerates detection and supplies context. Final target verification and commit must use the shared core.

---

## 2. Non-negotiable project constraints

### 2.1 No polling

Production game detection must remain event-driven.

Do not introduce:

- repeated `EnumWindows`
- foreground polling timers
- process-list polling
- `FindWindow` loops
- repeated package enumeration
- repeated `GetTopWindow` scans
- `Sleep`-based detection loops
- WMI `__InstanceCreationEvent WITHIN ...`
- any other periodic game discovery loop

Allowed mechanisms include:

- `SetWinEventHook`
- `RegNotifyChangeKeyValue`
- one-shot metadata inspection in response to an event
- PID-filtered PresentMon launched only for an active candidate
- process handles / waitable process lifetime
- worker queues awakened by events

`Active monitoring` in this design means enabling more event-driven work while a detection context is armed. It does not mean polling faster.

### 2.2 HUD presentation / VRR contract must not change

Game detection work must not alter or work around any production HUD presentation invariant.

Do not modify:

- HUD `windowExStyle`
- `WS_EX_TRANSPARENT`
- `WS_EX_NOACTIVATE`
- `WS_EX_TOPMOST`
- existing `WS_EX_LAYERED` behavior
- `WM_NCHITTEST -> HTTRANSPARENT`
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`
- `ProductionHudPresentationContract()`
- independent-flip requirement
- production Presentation API / DirectComposition path
- premultiplied-alpha contract

Game detection is upstream of presentation. If an implementation appears to require changing the HUD presentation contract, stop and treat it as a design conflict rather than changing the contract.

### 2.3 PR size

Each implementation PR must stay under approximately 500 changed LOC.

Prefer small focused PRs even though the application is still pre-release.

Parallel work is allowed where dependencies do not overlap.

---

## 3. Diagnostic work already completed

The following diagnostic sources were built specifically to gather raw evidence before designing the production detector.

### 3.1 WindowsGameIdentitySource

Captures, among other fields:

- process image path / executable
- process AUMID
- window AUMID
- package full name
- package family name
- package identity/version/architecture/publisher/resource
- package info flags
- install/effective/mutable/external package roots
- `MicrosoftGame.config` presence/readability
- StoreId
- TitleId
- MSAAppId
- `ExecutableList`
- exact current executable match against `MicrosoftGame.config`

It currently exists primarily as a diagnostic logger and worker.

### 3.2 ProcessLifecycleSource

Uses WMI asynchronous extrinsic events:

- `Win32_ProcessStartTrace`
- `Win32_ProcessStopTrace`

Field test result:

```text
[ProcessLifecycle] start.result=API_FAILED stage=StartSubscription hr=0x80041003
```

`0x80041003` is `WBEM_E_ACCESS_DENIED`.

The source therefore produced no process start/stop event data in the normal non-elevated field run.

Production game detection must not depend on this source.

Do not solve this by:

- elevating ClawHUD
- adding WMI `WITHIN` polling
- weakening the no-polling architecture

A separate diagnostic cleanup may classify this result as `UNAVAILABLE / INSUFFICIENT_PRIVILEGE`, but it is not part of production game detection.

### 3.3 PresentActivitySource

Global diagnostic PresentMon observer.

Useful for field analysis because it showed which PIDs were actually presenting before/after foreground transitions.

However the global stream is noisy and delivery timing is not suitable as the authoritative production clock.

Observed non-game/global traffic included large amounts of:

- `<unknown>` PID traffic
- `ClawHUD.exe`
- `steamwebhelper.exe`

Global PresentActivity remains diagnostic-only.

### 3.4 WindowLifecycleSource

Diagnostic global top-level WinEvent observer for exact events:

- `EVENT_OBJECT_CREATE`
- `EVENT_OBJECT_DESTROY`
- `EVENT_OBJECT_SHOW`
- `EVENT_OBJECT_HIDE`
- `EVENT_OBJECT_NAMECHANGE`

It records extensive metadata such as title, class, style, rect, DWM cloaking, cached destroy metadata, etc.

Field tests showed that this information is highly useful for understanding launch transitions, but far too noisy/heavy to use unchanged as the production idle listener.

Production should use a lightweight window-trigger source that captures only the small amount of information necessary to wake detection.

---

## 4. Field-test evidence that drove the final design

The production design must preserve the behavior validated by actual logs, not assumptions.

### 4.1 Steam: Trails in the Sky 2nd Chapter

Observed order:

```text
20:37:50.858 Steam RunningAppID changed old=0 new=5010190
20:37:51.533 game top-level HWND CREATE pid=18812
20:37:52.376 game window becomes foreground
...
20:37:56.653 production target confirmed pid=18812
```

Important observations:

- Steam `RunningAppID` changed before the game foreground event.
- It gave roughly 1.5 seconds of advance notice before the actual game became foreground.
- The game was a normal Win32 executable (`sora_2nd.exe`).
- Windows AppModel/package identity was absent.
- `steamwebhelper.exe` was also presenting, so `RunningAppID != 0` cannot identify the game PID by itself.

Conclusion:

> `RunningAppID` is an excellent wake-up/session trigger, but not a PID verdict.

Production should use it to transition from idle into Steam-armed discovery and to give subsequent candidate proposals Steam context.

### 4.2 Steam Alt+Tab behavior

Repeated Alt+Tab sequences showed:

```text
game committed
-> foreground leaves game
-> foreground match becomes false
-> committed PID remains retained
-> production PresentMon remains running
-> user returns to game
-> foreground match becomes true
-> HUD resumes immediately
```

This behavior is correct and must remain.

Alt+Tab is a HUD visibility event, not a new game-detection session.

Do not restart detection merely because foreground leaves a committed game.

### 4.3 Steam exit behavior

Observed order was not a single synchronous exit event.

Rough sequence:

```text
game HWND HIDE / DESTROY
-> several seconds later RunningAppID becomes 0
-> much later target process actually exits
```

Therefore:

- window destroy is not authoritative process lifetime
- Steam `RunningAppID -> 0` is not authoritative committed-target lifetime
- committed target release should remain tied to process lifetime

`RunningAppID -> 0` should clear Steam session context, not immediately destroy a still-live committed target.

### 4.4 Generic non-Steam Win32: Beast of Reincarnation

Observed order:

```text
20:39:10.922 game top-level HWND CREATE pid=11532
20:39:10.940 SHOW
20:39:15.xxx global PresentActivity begins for game PID
20:39:17.620 game becomes foreground
```

Important observations:

- game window existed roughly 6.7 seconds before foreground
- renderer activity also existed before foreground
- no Steam AppID context
- no AppModel/package identity
- no Microsoft game identity

Conclusion:

> A generic foreground fallback is still required. Steam and Microsoft/Xbox triggers cannot cover all games.

The generic trigger must feed the same verifier used by platform-specific triggers.

### 4.5 Xbox / Microsoft Store: Minecraft

The Xbox launch sequence provided the strongest reason to use GameIdentity as an early wake-up mechanism.

Observed high-level order:

```text
GamingServicesUI foreground
-> actual Minecraft game HWND CREATE / SHOW
-> Windows picker/security UI takes foreground
-> Minecraft starts rendering
-> long delay
-> actual Minecraft finally becomes foreground
```

Key timestamps from the field log:

```text
20:41:05.789 Minecraft game HWND SHOW pid=6008
20:41:11.933 Minecraft.Windows.exe already presenting
20:41:56.025 Minecraft finally foreground pid=6008
```

So the real game process was visible and rendering around 44-50 seconds before it finally became foreground.

A foreground-only detector discards nearly all of that advance information.

### 4.6 Xbox false candidates

During the same launch sequence, the current generic foreground path proposed non-game helpers.

Examples:

```text
gamingservicesui.exe
pickerhost.exe
```

`GamingServicesUI` even had a title resembling the game name.

`PickerHost`/system UI could occupy a full-screen-sized window.

Therefore do not classify games based on:

- window title
- screen-sized rect
- fullscreen-like dimensions
- generic window class assumptions

The final PID-filtered renderer verifier prevented those candidates from being committed because they did not produce the expected displayed-game rendering evidence.

Known helpers should still be rejected before expensive verification where possible.

### 4.7 Xbox positive GameIdentity evidence

For the real Minecraft process, diagnostic GameIdentity produced strong facts:

```text
processAumid = Microsoft.MinecraftUWP_8wekyb3d8bbwe!Game
packageFullName = Microsoft.MinecraftUWP_...
MicrosoftGame.config exists=1
MicrosoftGame.config read.result=SUCCESS
configExecutable name="Minecraft.Windows.exe" id="Game" targetDeviceFamily="PC"
currentExecutableMatch.result=SUCCESS value=1
```

The important production fact is:

> `MicrosoftGame.config` exists and the current executable exactly matches a configured game executable.

This is strong Microsoft/Xbox game identity evidence.

But it should still not, by itself, display the HUD. It should wake the common verifier early so renderer readiness can be checked before foreground commit.

### 4.8 PresentMon verification timing

Current `PresentMonHudTelemetry` conflates two different concepts:

1. first valid displayed frame exists
2. 500 ms of samples are available to calculate FPS

Current logic waits for the 500 ms accumulation before the callback used for candidate confirmation.

That unnecessarily delays confirmation.

Production must split:

```text
first valid displayed frame
-> renderer validation signal

500 ms accumulated display intervals
-> FPS telemetry update
```

The same PID-filtered PresentMon process should provide both. Do not start a second PresentMon process just for detection.

---

## 5. Final architecture

### 5.1 High-level model

There are three trigger adapters and one common detection core.

```text
                    +-----------------------------+
                    |            IDLE             |
                    | cheap event listeners only  |
                    +--------------+--------------+
                                   |
             +---------------------+----------------------+
             |                     |                      |
             v                     v                      v
+----------------------+ +----------------------+ +----------------------+
| Generic Foreground   | | Steam RunningAppID   | | Microsoft Game      |
| Trigger              | | Trigger              | | Identity Trigger     |
+----------+-----------+ +----------+-----------+ +----------+-----------+
           |                        |                        |
           +------------------------+------------------------+
                                    |
                                    v
                      +-----------------------------+
                      | GameDetectionCoordinator    |
                      | common state/candidate core |
                      +--------------+--------------+
                                     |
                                     v
                      +-----------------------------+
                      | GameRenderVerifier          |
                      | PID-filtered PresentMon     |
                      +--------------+--------------+
                                     |
                           FirstDisplayedFrame
                                     |
                                     v
                      +-----------------------------+
                      | final foreground recheck    |
                      +--------------+--------------+
                                     |
                                     v
                      +-----------------------------+
                      |       COMMITTED GAME        |
                      +-----------------------------+
```

### 5.2 Trigger is not verdict

This distinction is mandatory.

A trigger means:

> Something happened that makes game discovery worth doing now.

It does not mean:

> This PID is definitely a game.

Examples:

- Steam AppID proves a Steam session is active, not which PID is the game.
- MicrosoftGame identity gives a very strong PID clue, but the renderer may not yet be ready or foreground.
- Generic foreground gives an immediate PID, but the PID may still be a normal rendering desktop application.

The common verifier and final foreground commit rule remain necessary.

---

## 6. Trigger layer

## 6.1 Trigger 1: GenericForegroundTrigger

Purpose:

- universal fallback for games with no Steam session and no Microsoft/Xbox identity
- preserve support for normal Win32/non-Steam titles

Event source:

```text
SetWinEventHook(EVENT_SYSTEM_FOREGROUND)
```

The existing `ForegroundTracker` is already event-driven and should continue to provide foreground changes.

On a foreground event:

1. get HWND/PID
2. perform cheap production rejection
3. if usable, submit a generic candidate proposal to the coordinator
4. coordinator decides whether to start/replace verification

Conceptual event:

```cpp
GameDetectionTriggerEvent{
    .kind = GameDetectionTriggerKind::GenericForeground,
    .window = hwnd,
    .processId = pid,
};
```

This path directly knows the candidate PID, so it can transition from Idle to Verifying without an intermediate Steam-like session-only armed state.

Known limitation:

A generic foreground application that uses D3D and produces displayed frames can resemble a game.

Therefore:

- platform triggers should take precedence when available
- known non-game helper policy remains important
- generic detection is the fallback path, not the preferred path when a stronger platform context exists
- future field evidence may add conservative admission rules, but not heuristic scoring without evidence

Do not use window title/fullscreen dimensions as the generic verdict.

## 6.2 Trigger 2: SteamRunningAppTrigger

Purpose:

- wake game discovery before the real Steam game reaches foreground
- provide Steam session context
- reduce dependence on blind foreground-only probing during launch

Existing source:

```text
SteamRunningAppIdSource
RegNotifyChangeKeyValue
```

Important transitions:

### `0 -> N`

Steam game session begins.

Coordinator should enter or augment an Armed context:

```text
steamAppId = N
steamSessionActive = true
```

Do not invent a game PID from AppID.

Subsequent lightweight window/foreground events can propose candidate PIDs under Steam context.

### `N -> N`

No state change.

### `N -> M`

Treat as a new Steam session generation.

Before production support for simultaneous/multi-game scenarios is explicitly added, a non-committed Steam candidate from the previous generation may be invalidated.

A live committed target must not be destroyed only because AppID changed. Preserve the committed-process lifetime rule.

### `N -> 0`

Clear Steam session context.

If no candidate/committed target remains, return to Idle.

If a candidate or committed process is still alive, do not kill it merely because Steam cleared the registry value. Field logs showed Steam session state can clear before the actual process exits.

### Steam candidate discovery

When Steam session is armed, the coordinator should pay attention to:

- foreground changes
- lightweight top-level window CREATE/SHOW events

Candidate proposals must still pass common cheap rejection.

Useful context to log:

```text
trigger=SteamRunningAppId
steamAppId=5010190
sessionGeneration=...
candidatePid=...
```

AppID should be treated as session context, not PID identity.

## 6.3 Trigger 3: MicrosoftGameTrigger

Purpose:

- identify Xbox/Microsoft game PIDs before they necessarily become foreground
- start common renderer verification early
- allow a render-ready candidate to wait until it actually becomes foreground

This trigger internally consists of two event-driven stages:

```text
lightweight top-level window event
-> one-shot GameIdentity probe for PID
-> MicrosoftGame.config exact executable match
-> emit MicrosoftGame trigger event
```

### Production window event source

Do not reuse the diagnostic `WindowLifecycleSource` unchanged.

Create a lightweight production source that observes only the events necessary to wake identity probing.

Recommended minimum:

- `EVENT_OBJECT_CREATE`
- `EVENT_OBJECT_SHOW`
- `EVENT_OBJECT_DESTROY` only if needed for candidate cleanup/context

Filter:

```text
idObject == OBJID_WINDOW
idChild == CHILDID_SELF
```

Top-level evidence should preserve callback-time root information so short-lived windows are not lost due to worker delay, following the lesson from the diagnostic source.

Production event payload should remain small, for example:

```cpp
struct ProductionWindowEvent
{
    std::uint64_t sequence{};
    WindowEventType type{};
    HWND window{};
    DWORD processId{};
    HWND immediateRoot{};
    bool immediateTopLevel{};
};
```

Do not gather diagnostic-only metadata in the callback/idle path:

- title
- class
- styles
- rect
- DWM cloaked state
- large snapshot caches

Callback remains thin and queues work.

### GameIdentity production probe

The production probe should return structured facts rather than only logging them.

Do not create an opaque `isGame=true` result if avoidable.

Recommended facts:

```cpp
struct MicrosoftGameIdentityFacts
{
    DWORD processId{};
    bool packagePresent{};
    bool microsoftGameConfigPresent{};
    bool microsoftGameConfigReadable{};
    bool currentExecutableMatched{};
    std::wstring packageFullName;
};
```

Production Microsoft game trigger condition:

```text
MicrosoftGame.config readable
AND
current executable exact-match succeeds
```

When this condition is true, emit a trigger with the known PID immediately.

Example:

```cpp
GameDetectionTriggerEvent{
    .kind = GameDetectionTriggerKind::MicrosoftGameIdentity,
    .window = hwnd,
    .processId = pid,
    .microsoftGameExactMatch = true,
};
```

The coordinator can then start PID-filtered renderer verification before the game is foreground.

This is where the Minecraft field log provides major latency improvement: the game renderer existed tens of seconds before foreground.

---

## 7. Common trigger/event representation

The common core should not need platform-specific detection code scattered through `App.cpp`.

A single event type should carry trigger context.

One possible shape:

```cpp
enum class GameDetectionTriggerKind
{
    GenericForeground,
    SteamRunningAppId,
    MicrosoftGameIdentity,
};

struct GameDetectionTriggerEvent
{
    GameDetectionTriggerKind kind{};
    std::uint64_t sequence{};

    HWND window{};
    DWORD processId{};

    std::uint32_t steamAppId{};
    bool microsoftGameExactMatch{};
};
```

The exact structure may change during implementation, but retain these principles:

- trigger kind is explicit
- PID may be unknown for a session-only Steam wake
- Steam AppID remains context
- Microsoft game identity remains a raw fact
- do not collapse all evidence into a confidence score

---

## 8. Common GameDetectionCoordinator

The coordinator owns shared state and candidate lifecycle.

Responsibilities:

- receive trigger events
- maintain platform/session context
- accept/deduplicate candidate proposals
- apply deterministic candidate replacement rules
- start/stop the common renderer verifier
- reject stale verifier callbacks
- promote a renderer-verified candidate when foreground matches
- retain committed target over Alt+Tab
- release or fall back when candidate/committed process exits

It must not:

- render the HUD
- modify presentation styles
- implement platform-specific registry/WMI/package querying itself
- run global PresentMon
- poll

---

## 9. Recommended state machine

Five explicit states are recommended because pre-foreground renderer verification is a major benefit of Steam/Xbox wake-up triggers.

```cpp
enum class GameDetectionState
{
    Idle,
    Armed,
    Verifying,
    Ready,
    Committed,
};
```

### 9.1 Idle

No active candidate and no platform session requiring active discovery.

Cheap always-on listeners remain active:

- foreground WinEvent
- Steam registry notification
- lightweight production window events for one-shot Microsoft identity discovery

No candidate PID-filtered PresentMon is running.

### 9.2 Armed

A wake-up context exists but no selected candidate PID is being verified yet.

Most common example:

```text
Steam RunningAppID 0 -> N
```

State contains session context such as:

```text
steamAppId
steamSessionGeneration
```

It waits for candidate proposals from foreground/window events.

### 9.3 Verifying

A candidate PID has been selected.

The common PID-filtered `GameRenderVerifier` is attached.

No valid displayed frame has yet been observed.

State should retain:

- candidate PID
- candidate HWND when known
- candidate generation/token
- evidence facts that caused or augmented the proposal
- process handle/liveness context if used

### 9.4 Ready

A valid displayed frame was observed for the candidate PID.

The renderer is verified, but the candidate is not yet committed because it is not currently foreground.

This state is critical for early Microsoft/Xbox discovery.

Example from Minecraft:

```text
MicrosoftGame identity positive
-> PID-filtered PresentMon verifies renderer
-> Ready for many seconds
-> game finally becomes foreground
-> immediate commit
```

### 9.5 Committed

The production game target is confirmed.

Committed semantics:

- HUD visibility follows whether committed PID is foreground
- Alt+Tab does not clear committed PID
- production PresentMon remains associated with target
- process lifetime remains authoritative for release

---

## 10. Core state transitions

### 10.1 Generic launch

```text
Idle
-> GenericForeground(pid)
-> Verifying(pid)
-> FirstDisplayedFrame(pid)
-> foreground still pid
-> Committed(pid)
```

If first displayed frame arrives after foreground changed:

```text
Verifying(pid)
-> FirstDisplayedFrame(pid)
-> foreground != pid
-> Ready(pid)
```

The candidate can commit later if it returns foreground and has not been invalidated.

### 10.2 Steam launch

```text
Idle
-> RunningAppID 0 -> N
-> Armed(Steam, AppID=N)
-> window/foreground candidate pid=P
-> Verifying(P)
-> FirstDisplayedFrame(P)
-> Ready(P) if not foreground
-> foreground=P
-> Committed(P)
```

If P is already foreground when the first frame signal is processed, Verifying may transition directly to Committed after final revalidation.

### 10.3 Microsoft/Xbox launch

```text
Idle
-> top-level window CREATE/SHOW pid=P
-> one-shot GameIdentity probe
-> MicrosoftGame.config exact executable match
-> Verifying(P)
-> FirstDisplayedFrame(P)
-> Ready(P)
-> later foreground=P
-> Committed(P)
```

This is the primary optimization enabled by the diagnostic work.

### 10.4 Candidate process exit

If candidate exits in Verifying/Ready:

```text
if Steam session context is still active:
    -> Armed
else:
    -> Idle
```

This supports launcher/game handoff without polling.

### 10.5 Steam AppID clears before process exit

```text
Steam context cleared
candidate/committed process still alive
-> keep candidate/committed state
```

Do not treat AppID 0 as authoritative process death.

### 10.6 Committed Alt+Tab

```text
Committed(P)
foreground != P
-> remain Committed(P)
-> HUD hidden

foreground == P
-> remain Committed(P)
-> HUD visible immediately
```

No re-detection and no new renderer verification should be required simply to return from Alt+Tab.

### 10.7 Committed process exit

```text
Committed(P)
-> process handle signaled / verified dead
-> release production target
```

Then:

```text
if a still-active platform/session context can discover a replacement:
    -> Armed
else:
    -> Idle
```

---

## 11. Candidate evidence model

Avoid confidence scores.

Keep raw evidence/facts and deterministic policy.

Example:

```cpp
struct GameCandidateEvidence
{
    bool genericForegroundObserved{};
    std::uint32_t steamAppId{};
    bool microsoftGameExactMatch{};
    bool displayedFrameObserved{};
};
```

The same PID may receive evidence from more than one trigger.

For example:

```text
Generic foreground proposes PID
-> later GameIdentity exact-match arrives for same PID
-> merge evidence
-> do not restart verifier
```

Likewise:

```text
Steam session armed
-> foreground proposes PID
-> same PID receives additional window events
-> deduplicate
```

---

## 12. Candidate precedence and replacement

Do not implement a numeric score.

Use deterministic precedence based on explicit evidence.

Recommended initial policy:

1. A live committed target is not replaced merely because another trigger fires.
2. Same PID proposals merge evidence rather than restart verification.
3. A `MicrosoftGame.config` exact-match candidate may replace a generic non-identified candidate while not committed.
4. A Steam-context candidate should be preferred over a generic candidate during an active Steam session when both are plausible and neither is committed.
5. A generic proposal must not displace a MicrosoftGame Ready candidate merely because a helper/dialog becomes foreground.
6. Dead candidate can always be replaced.
7. A newer candidate generation invalidates stale verifier callbacks from the prior generation.

The precise helper function names may vary, but policy should remain explicit/testable rather than embedded as ad-hoc branches in `App.cpp`.

### 12.1 Candidate generation/token

Every verifier attachment should get a monotonically increasing candidate token/generation.

Callbacks should carry at least:

```text
candidate generation
PID
renderer event type
```

Before applying a callback:

```text
callback generation == active candidate generation
AND
callback PID == active candidate PID
```

Otherwise ignore as stale.

This protects candidate handoff races and late UI-thread messages.

---

## 13. Known cheap rejection policy

Continue cheap image-based rejection before launching an expensive verifier.

Field-test false candidates that should be added to the existing policy include:

```text
mongmode.exe
gamingservicesui.exe
pickerhost.exe
```

The existing policy already rejects several system/Steam/browser processes.

This list is an optimization/safety filter, not the main game detector.

Do not attempt to solve Xbox detection only with a growing blacklist; the MicrosoftGame trigger exists specifically to provide positive identity.

---

## 14. GameRenderVerifier

The common verifier drives the shared production PresentMon **API2** frame query
(`PresentMonFrameTelemetry`, owned by `PresentMonTelemetryProvider`). It no
longer launches a `PresentMon.exe` child process, parses CSV, or manages an ETW
session. Process tracking on the shared session is reference counted
(`PresentMonProcessLease` / `ProcessTrackingRefCounts`), so the verifier and the
FPS path can hold the same PID without one releasing it from under the other.

It must not use the global diagnostic PresentActivity source.

### 14.1 Single responsibility

For one selected candidate PID, determine:

```text
Has this candidate produced a valid displayed frame?
```

Then continue to provide normal FPS telemetry after the candidate is committed.

### 14.2 Separate renderer proof from FPS aggregation

The verifier callback carries renderer proof only. Production FPS is supplied
separately by the PID-bound PresentMon API2 process telemetry. There is no
stream-lifecycle event: game-process exit is owned by
`ProductionProcessLifetimeWatcher`.

Current API:

```cpp
enum class GameRenderVerifierEventType
{
    FirstDisplayedFrame,
};

struct GameRenderVerifierEvent
{
    DWORD processId{};
    std::uint64_t generation{};
    GameRenderVerifierEventType type{};
};
```

The first-displayed-frame test is `PM_METRIC_BETWEEN_DISPLAY_CHANGE > 0`,
consumed via `pmConsumeFrames` on the shared session.

Semantics:

#### FirstDisplayedFrame

Emit exactly once after the first valid displayed frame row for the target process.

Do not wait for the FPS accumulation window.

#### StreamEnded

Represent unexpected/normal stream completion separately from `fps = nullopt` semantics.

### 14.3 Final commit rule

FirstDisplayedFrame is renderer proof, not final game commit by itself.

On UI/coordinator thread:

```text
active candidate generation matches
AND
active candidate PID matches verifier PID
AND
candidate process is alive
AND
current foreground PID == candidate PID
```

Then commit.

If renderer is verified but foreground does not match:

```text
-> Ready
```

and wait for a later foreground event.

### 14.4 Present mode is not a game criterion

Do not require:

- Independent Flip
- Hardware Independent Flip
- any specific PresentMode

Field tests showed real games can begin under `Composed: Flip` and later transition.

The validation event is the presence of a valid displayed frame for the PID-filtered candidate, not a particular presentation mode.

---

## 15. Foreground handling after commit

ForegroundTracker remains important after detection.

Its committed-state responsibility is simple:

```text
foreground PID == committed PID
-> HUD visible according to visibility mode

foreground PID != committed PID
-> HUD hidden for In-Game-only mode
```

The committed target does not get discarded on foreground mismatch.

This behavior was directly validated by repeated Alt+Tab field tests and should be treated as a regression contract.

---

## 16. Production window trigger vs diagnostic WindowLifecycleSource

Do not couple production to diagnostic logging details.

Recommended separation:

```text
Diagnostic WindowLifecycleSource
    - CREATE/DESTROY/SHOW/HIDE/NAMECHANGE
    - title/class/style/rect/cloak/cache metadata
    - debug logging only

Production GameWindowTriggerSource
    - CREATE/SHOW/(optional DESTROY)
    - HWND/PID/root/top-level fact only
    - event callback -> lightweight queue
    - no diagnostic snapshot/logging overhead
```

Shared pure helpers may be reused when appropriate, such as:

- object/child filter
- callback-time top-level determination
- event mapping

But production source ownership/lifecycle should remain independent from Debug Logging state.

---

## 17. GameIdentity production extraction

The diagnostic source currently performs both probing and verbose logging.

Production needs a reusable one-shot probe layer.

Recommended split:

```text
WindowsGameIdentityProbe
    -> returns raw structured facts

WindowsGameIdentitySource (diagnostic)
    -> calls probe + logs all facts

MicrosoftGameTrigger (production)
    -> calls probe + consumes only required facts
```

This avoids maintaining two independent implementations of AppModel/package/MicrosoftGame parsing.

Static package metadata caching may remain shared where safe.

Production probe must remain asynchronous/off the WinEvent callback path.

---

## 18. Idle vs active resource behavior

### 18.1 Idle

Cheap listeners only:

- foreground WinEvent
- Steam registry notification
- lightweight top-level window events

One-shot identity checks may be queued for newly relevant PIDs, but do not enumerate all packages/processes at startup.

Do not run global PresentMon.

Do not run candidate PID PresentMon when no candidate exists.

### 18.2 Armed / Verifying / Ready

More expensive work is allowed because a trigger justified it:

- one-shot GameIdentity work
- candidate correlation
- PID-filtered PresentMon
- candidate process handle/lifetime

Still no polling.

### 18.3 Committed

Stop broad discovery work that is not useful for the committed target where practical.

Continue:

- committed PID process lifetime
- foreground match
- production target PresentMon/FPS
- required telemetry

Platform session notifications can remain active because they are cheap and may be needed for cleanup/context.

---

## 19. Logging requirements

Production game detection should remain diagnosable without enabling the large diagnostic sources.

Recommended concise production logs:

```text
[GameDetection] state Idle -> Armed trigger=Steam appId=5010190
[GameDetection] candidate pid=18812 trigger=Steam generation=7
[GameDetection] verify.start pid=18812 generation=7
[GameDetection] renderer.ready pid=18812 generation=7
[GameDetection] commit pid=18812 trigger=Steam appId=5010190
[GameDetection] foreground.leave pid=18812
[GameDetection] foreground.return pid=18812
[GameDetection] release pid=18812 reason=process-exit
```

For Microsoft identity:

```text
[GameDetection] trigger=MicrosoftGame pid=6008 configMatch=1
```

For stale callbacks:

```text
[GameDetection] verifier.event ignored reason=stale-generation ...
```

Do not log every window event in normal production logging.

Verbose WindowLifecycle/GameIdentity/PresentActivity dumps remain Debug Logging features.

---

## 20. Proposed PR plan

Each PR must remain <= ~500 changed LOC.

The exact numbering depends on repository state when implementation begins; the labels below describe logical PRs.

## Wave 1 - independent foundations, can run in parallel

### PR A - Separate FirstDisplayedFrame from FPS updates

Estimated: 200-350 LOC

Scope:

- change production PresentMon callback to typed events
- emit FirstDisplayedFrame exactly once
- keep 500 ms FPS aggregation separately
- explicit stream-ended signal
- unit tests for parsing/event behavior

Do not integrate new game coordinator yet.

Dependencies: none

Can run in parallel with PR B and PR C.

### PR B - Lightweight production window trigger source

Estimated: 250-400 LOC

Scope:

- new production-only WinEvent source
- exact CREATE/SHOW and optional DESTROY hooks
- OBJID_WINDOW / CHILDID_SELF filter
- callback-time PID/root/top-level facts
- bounded queue / clean Stop
- no title/class/style/rect/DWM work
- tests for filtering/order/short-lived top-level preservation

Dependencies: none

Can run in parallel with PR A and PR C.

### PR C - Extract reusable Windows GameIdentity probe

Estimated: 300-450 LOC

Scope:

- extract reusable one-shot structured probe from diagnostic logger
- preserve current diagnostic output behavior
- expose MicrosoftGame config/read/exact-exe-match facts
- tests for exact matching and result mapping

Do not wire to production target selection yet.

Dependencies: none or minimal current WindowsGameIdentitySource code

Can run in parallel with PR A and PR B.

## Wave 2 - common core foundation

### PR D - Add GameDetectionCoordinator state/evidence model

Estimated: 300-450 LOC

Scope:

- Idle / Armed / Verifying / Ready / Committed model
- trigger kind/event types
- candidate generation
- evidence merge
- deterministic replacement policy pure helpers
- unit tests

No App.cpp cutover yet if it would exceed size.

Dependencies: interfaces from Wave 1 should be known, but the pure model can be developed with minimal coupling.

## Wave 3 - trigger adapters, largely parallel

### PR E - Generic foreground trigger adapter + helper rejects

Estimated: 150-300 LOC

Scope:

- feed usable foreground proposals into coordinator
- add known false-positive executable rejects:
  - `mongmode.exe`
  - `gamingservicesui.exe`
  - `pickerhost.exe`
- tests

Dependencies: PR D

### PR F - Steam RunningAppID trigger integration

Estimated: 200-350 LOC

Scope:

- `0 -> N` arms Steam context
- retain AppID/session generation
- merge foreground/window candidate proposals under Steam context
- `N -> 0` clears session context without prematurely killing live candidate/committed process
- tests for transitions and stale session generation

Dependencies: PR B + PR D

Can run in parallel with PR G.

### PR G - MicrosoftGame identity trigger integration

Estimated: 250-450 LOC

Scope:

- production window event -> async one-shot identity probe
- emit MicrosoftGame trigger only on readable config + exact current executable match
- dedupe same PID probes
- cache per-live-PID probe state as needed
- tests for positive/negative/helper paths

Dependencies: PR B + PR C + PR D

Can run in parallel with PR F.

## Wave 4 - common renderer verifier integration

### PR H - Add GameRenderVerifier common adapter

Estimated: 250-400 LOC

Scope:

- wrap candidate PID-filtered PresentMon
- candidate generation carried through callbacks
- FirstDisplayedFrame -> renderer-ready
- FPS updates kept separate
- stale callback rejection hooks
- process/stream end result
- unit tests

Dependencies: PR A + PR D

This can potentially run in parallel with PR E/F/G after PR A/D are merged.

## Wave 5 - production cutover

### PR I - Wire coordinator into production target flow

Estimated: 350-500 LOC

Scope:

- replace ad-hoc pending candidate flow with coordinator-managed candidate lifecycle
- all three triggers feed same common path
- Ready + foreground -> Commit
- committed PID still stored/used by existing production target/telemetry flow
- preserve existing PresentMon instance across candidate -> committed transition where possible
- preserve current HUD visibility behavior

Dependencies: PR E/F/G/H

If this exceeds 500 LOC, split into:

1. candidate/verifying/ready integration
2. commit/lifetime integration

Do not exceed the PR limit just to keep a conceptual feature in one PR.

## Wave 6 - regression hardening and cleanup

### PR J - Cross-trigger state/lifecycle regression tests

Estimated: 300-450 LOC

Cover all scenarios in Section 21.

### PR K - Cleanup old production detection helpers

Estimated: 100-300 LOC

Only after field validation.

Scope:

- remove dead pending-target helpers
- rename ambiguous `displayedFpsAvailable` style policy arguments to renderer-specific semantics
- keep App.cpp responsibilities small
- no behavior changes

---

## 21. Required automated test matrix

### 21.1 Generic Win32

```text
Idle
-> foreground usable PID
-> Verifying
-> FirstDisplayedFrame
-> same foreground PID
-> Committed
```

### 21.2 Generic stale verifier callback

```text
candidate P1
-> candidate replaced with P2
-> late FirstDisplayedFrame(P1, old generation)
-> ignored
```

### 21.3 Steam session wake

```text
RunningAppID 0 -> N
-> Armed
-> no immediate PID assumption
```

### 21.4 Steam candidate

```text
Armed(AppID=N)
-> game window/foreground P
-> Verifying P
-> displayed frame
-> foreground P
-> Committed P
```

### 21.5 Steam helper rejected

`steam.exe`, `steamwebhelper.exe`, known helpers must not become final candidate merely because Steam session is active.

### 21.6 Steam AppID clears before process death

```text
Committed P
-> RunningAppID N -> 0
-> P still alive
-> remain Committed P
```

### 21.7 Microsoft/Xbox pre-foreground verification

```text
window event P
-> GameIdentity exact match
-> Verifying P
-> FirstDisplayedFrame
-> foreground != P
-> Ready P
-> later foreground P
-> Committed immediately
```

### 21.8 Xbox helper false candidate

Ensure:

```text
gamingservicesui.exe
pickerhost.exe
```

cannot displace a stronger MicrosoftGame Ready candidate.

### 21.9 Same PID evidence merge

```text
Generic proposal P
-> MicrosoftGame exact-match P
-> merge evidence
-> verifier not restarted
```

### 21.10 Candidate process exits

```text
Steam Armed
-> Verifying P
-> P exits
-> back to Armed if AppID session still active
```

Generic/no-session case returns to Idle.

### 21.11 Alt+Tab after commit

```text
Committed P
-> foreground Explorer
-> remain Committed P
-> HUD hidden
-> foreground P
-> HUD visible
-> no re-detection
```

### 21.12 Process exit after commit

```text
Committed P
-> process exits
-> release target
-> Idle or Armed depending remaining platform context
```

### 21.13 Duplicate trigger events

Repeated CREATE/SHOW/foreground notifications for same PID must not restart the verifier repeatedly.

### 21.14 Stream failure

Unexpected candidate PresentMon stream end:

- do not commit
- cleanup verifier
- return to appropriate Armed/Idle state
- bounded recovery only if existing production policy explicitly allows it

### 21.15 Suspend/resume and diagnostics

Existing suspend/resume/diagnostic gates must continue to block or restore production detection consistently.

---

## 22. Required field validation after production cutover

Field validation is mandatory because the design intentionally uses real platform event ordering.

### 22.1 Steam

Test at least:

- direct-launch game
- game with launcher/splash if available
- repeated Alt+Tab
- exit

Validate logs show:

```text
RunningAppID wake
-> candidate acquisition
-> FirstDisplayedFrame
-> commit
```

Confirm detection latency improves versus waiting for a late 500 ms FPS callback.

### 22.2 Generic non-Steam Win32

Use a title with:

- no Steam AppID
- no package identity
- no MicrosoftGame config

Confirm generic foreground path still works.

### 22.3 Xbox / Microsoft Store

Use a title that reproduces helper/UI transitions.

Validate:

- game PID can be identified before foreground where possible
- GameIdentity exact-match wakes verifier
- renderer can reach Ready before foreground
- GamingServicesUI/PickerHost do not steal the candidate
- foreground arrival commits quickly

### 22.4 Alt+Tab during Verifying

Candidate becomes non-foreground before FirstDisplayedFrame callback.

Expected:

- no incorrect commit
- may become Ready after renderer proof
- commit only when foreground returns

### 22.5 Alt+Tab after Committed

Expected:

- committed PID retained
- PresentMon not restarted solely due Alt+Tab
- HUD hides/shows based on foreground

### 22.6 VRR validation

Even though presentation code is untouched, run standard hardware validation with Debug Logging OFF:

```text
VRR on
-> launch game
-> HUD appears
-> Alt+Tab
-> return
-> exit
```

Also perform one Debug Logging ON run to ensure diagnostic global PresentMon can coexist during development without altering production presentation contracts.

---

## 23. Deferred / intentionally unsupported questions

### 23.1 Multiple simultaneously running games

Current production policy retains a live committed target.

Do not change this behavior based only on theory.

A separate field test is required for:

```text
Game A committed and still alive in background
-> launch Game B
```

Only after logs exist should handoff semantics be designed.

### 23.2 Numeric confidence scoring

Not part of v1.

The design intentionally uses explicit facts and deterministic transitions.

Do not add `confidence=0.82`, weighted title heuristics, etc. without field evidence demonstrating a need.

### 23.3 Window-title/fullscreen heuristics

Not part of v1.

Field logs demonstrated that system/helper UI can have game-like titles and full-screen-sized windows.

### 23.4 Independent Flip as game identity

Not part of v1.

Real games may temporarily use composed presentation modes.

Independent Flip remains a HUD/VRR presentation requirement where relevant, not a game-identity classifier.

### 23.5 ProcessLifecycleSource as production dependency

Explicitly excluded.

Normal-user WMI trace subscription failed with `WBEM_E_ACCESS_DENIED` in field testing.

Do not elevate or poll just to restore this source.

### 23.6 Global PresentActivity in production

Explicitly excluded from initial production detector.

Keep it diagnostic-only unless later evidence demonstrates a compelling low-overhead use.

---

## 24. Implementation guidance for App.cpp

The current production code contains candidate/pending/confirmation state inside `App`.

The migration goal is not to rewrite the entire application in one PR.

Preferred incremental direction:

```text
App
  receives external UI/system events
        |
        v
GameDetection trigger adapters
        |
        v
GameDetectionCoordinator
        |
        v
GameRenderVerifier
        |
        v
App commits/releases production target and reconciles HUD visibility
```

Eventually `App.cpp` should not need to know the platform-specific reason why a candidate was discovered.

It should receive high-level outcomes such as:

```text
candidate selected
renderer ready
commit target
release target
```

Platform details remain useful for logging/debug context but should not duplicate final validation code.

---

## 25. Architectural reason for the shared verifier

Without the common verifier, implementation tends to become:

```text
Steam detector -> Steam-specific confirmation logic
Xbox detector -> Xbox-specific confirmation logic
Generic detector -> generic confirmation logic
```

That creates three places to fix:

- Alt+Tab races
- stale callbacks
- PresentMon recovery
- candidate process exit
- foreground revalidation
- HUD commit timing

The chosen design instead is:

```text
Generic trigger ---------+
Steam trigger -----------+--> common coordinator --> common verifier --> commit
MicrosoftGame trigger ---+
```

This means future platform-specific work is usually isolated to trigger/candidate acquisition.

Examples:

- Steam registry behavior changes -> Steam trigger only
- Xbox package metadata changes -> MicrosoftGame trigger only
- PresentMon validation race -> common verifier only
- Alt+Tab bug -> coordinator/committed lifecycle only

This is the primary maintainability goal of the final architecture.

---

## 26. Future trigger extensibility

If a reliable event-driven source is discovered later for Epic, GOG, another launcher, or a first-party gaming API, it should be added as another trigger adapter rather than another full detector.

Conceptually:

```text
Generic --------+
Steam ----------+
MicrosoftGame --+--> common detection core
FutureTrigger --+
```

New trigger requirements:

- event-driven
- cheap while idle
- provide raw context/facts
- do not bypass common renderer verification/foreground commit unless a future design review explicitly changes the contract

---

## 27. Summary of final decisions

### Use in production

- `EVENT_SYSTEM_FOREGROUND` as generic fallback trigger
- Steam `RunningAppID` registry notification as Steam wake/session trigger
- lightweight top-level window events + one-shot Windows GameIdentity as Microsoft/Xbox trigger
- shared `GameDetectionCoordinator`
- PID-filtered PresentMon as common renderer verifier
- `FirstDisplayedFrame` separated from FPS aggregation
- explicit Idle / Armed / Verifying / Ready / Committed lifecycle
- final foreground PID revalidation before commit
- process lifetime as authoritative committed-target release
- Alt+Tab retains committed target and only changes HUD visibility

### Keep diagnostic-only

- global `PresentActivitySource`
- verbose `WindowLifecycleSource`
- verbose `WindowsGameIdentitySource` logging layer
- `ProcessLifecycleSource` (optional/unavailable under normal permissions)

### Do not use as game verdict

- title
- fullscreen rect
- window class alone
- PresentMode alone
- Steam AppID alone
- package presence alone
- numeric confidence score

### Key design principle

> Three independent wake-up triggers, one common candidate/renderer verification pipeline, one common commit/lifetime contract.

This is the production design to implement unless new field evidence demonstrates a concrete reason to revise it.
