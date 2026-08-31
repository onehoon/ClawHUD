# Work Order — Game Detection R1: Foreground Screen Admission Foundation

Status: implementation work order  
Repository: `onehoon/ClawHUD`  
Baseline: latest `main` at planning time (`6f68cb1532c09df2a1019374c03f1056c996cf0b`)  
Parent design: `docs/GAME_DETECTION_REDESIGN_PR_PLAN_2026-08-31.md`  
Field evidence: `docs/GAME_DETECTION_FIELD_ANALYSIS_2026-08-31.md`

---

## 1. Goal

Implement the first foundation slice of the foreground-first In-Game Only redesign.

This PR must establish two reusable pieces without switching production game detection to the new model yet:

1. a testable `GameScreenAdmission` policy that can answer whether a supplied HWND/PID observation is a usable fullscreen-like foreground game-screen candidate; and
2. the missing production WinEvent coverage required by field evidence: `EVENT_OBJECT_HIDE` and `EVENT_OBJECT_LOCATIONCHANGE`.

Also extend the existing exact executable exclusion policy with the high-confidence non-game processes demonstrated or justified by the field run.

The important boundary for this PR is:

> Build and test the new screen-admission foundation, but do **not** make it authoritative for HUD visibility, FPS targeting, candidate selection, renderer verification, Steam session behavior, or the legacy `Committed` state machine yet.

Later PRs will consume this foundation.

---

## 2. Why this PR exists

The 2026-08-31 mixed game-detection field run demonstrated two facts that the current production detector does not encode correctly.

### 2.1 Renderer evidence is not sufficient game identity

The existing generic path can accept ordinary rendering desktop processes. In the field run, `WindowsTerminal.exe` was accepted as a generic candidate, reached renderer-ready, and became the globally committed process. Because the current coordinator protects a live `Committed` PID, later real games were ignored.

This PR does **not** solve the sticky commit model yet. It provides the first hard screen gate that future detector logic will use before renderer verification.

### 2.2 Foreground events alone are not sufficient

Field examples showed that a real game may become foreground before its final fullscreen/borderless geometry is established.

Observed examples:

```text
Minecraft:
foreground -> ~1920x1128
LOCATIONCHANGE -> approximately -3,-3,1923,1203
```

and:

```text
Mafia:
foreground while final visible/fullscreen state is not complete
SHOW / LOCATIONCHANGE shortly afterward
-> 0,0,1920,1200
```

Therefore later foreground-first production logic must be able to re-evaluate the current foreground HWND when geometry or visibility changes without polling.

The existing `ProductionGameWindowSource` currently listens only for CREATE, SHOW, and DESTROY. This PR adds HIDE and LOCATIONCHANGE so the later cutover can remain event-driven.

---

## 3. Current-main context

Review current `main` before editing. The relevant production shape at the planning baseline is:

### `src/ClawHUD/GameDetection/ProductionGameWindowSource.*`

Current event enum:

```cpp
enum class ProductionWindowEventType
{
    Create,
    Show,
    Destroy
};
```

Current hooks:

```text
EVENT_OBJECT_CREATE
EVENT_OBJECT_SHOW
EVENT_OBJECT_DESTROY
```

The source already:

- filters to `OBJID_WINDOW` + `CHILDID_SELF`;
- captures PID / thread / immediate root at callback time;
- accepts only immediate top-level observations;
- queues events through a bounded queue;
- dispatches them through a worker;
- contains no polling.

Preserve that architecture.

### `src/ClawHUD/ProductionTargetPolicy.*`

The exact executable reject list is centralized in:

```cpp
bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept;
```

and normalized executable eligibility is already available through:

```cpp
bool IsEligibleProductionTargetImage(std::wstring_view image) noexcept;
```

`InspectProductionTargetProcess()` already resolves a process image using `PROCESS_QUERY_LIMITED_INFORMATION` and `QueryFullProcessImageNameW`.

Do not create a second unrelated exclusion list.

### `src/ClawHUD/main.cpp`

Production already calls:

```cpp
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
```

before `App` is created.

Do **not** add another production DPI initialization path in this PR.

The standalone Diag 150% scaling problem was a diagnostic coordinate-space bug, not evidence that production lacks DPI awareness.

---

## 4. Scope summary

Implement:

```text
GameScreenAdmission foundation
+ deterministic geometry/admission policy tests
+ Win32 observation adapter/helper
+ HIDE WinEvent mapping/hook
+ LOCATIONCHANGE WinEvent mapping/hook
+ exact executable exclusion additions
+ corresponding unit tests
```

Do not wire `GameScreenAdmission` into the active `GameSessionController` decision path yet.

---

# Part A — `GameScreenAdmission`

## 5. Add a dedicated component

Create a narrow component under the game-detection domain, suggested files:

```text
src/ClawHUD/GameDetection/GameScreenAdmission.h
src/ClawHUD/GameDetection/GameScreenAdmission.cpp
```

The component should separate:

1. **Win32 observation capture** — obtaining current window/process/geometry facts; and
2. **pure admission policy** — deciding PASS/FAIL from those facts.

Do not bury all logic directly inside `GameSessionController`.

This separation is important because most policy behavior must be unit-testable without creating real windows, changing the real foreground window, or depending on DWM state in CI.

---

## 6. Recommended observation model

Use a model equivalent to the following. Exact naming may vary if a cleaner shape fits the codebase.

```cpp
struct GameScreenObservation
{
    HWND window{};
    DWORD processId{};

    bool topLevel{};
    bool visible{};
    bool minimized{};
    bool cloaked{};

    bool processAvailable{};
    std::wstring imageName;

    bool monitorAvailable{};
    bool boundsAvailable{};
    RECT windowBounds{};
    RECT monitorBounds{};
};
```

Keep raw observed facts separate from the decision.

Do not place Steam state, Microsoft identity, renderer state, FPS, or any game-scoring heuristic in this struct.

Those belong to later PRs.

---

## 7. Recommended result model

Expose a result that explains rejection, for example:

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
    BoundsUnavailable,
    NotFullscreenLike,
};

struct GameScreenAdmissionResult
{
    bool admitted{};
    GameScreenRejectReason reason{GameScreenRejectReason::None};
    HWND window{};
    DWORD processId{};
    std::wstring imageName;
    RECT windowBounds{};
    RECT monitorBounds{};
};
```

The exact shape can differ, but preserve a machine-readable rejection reason.

Future production logging must be able to answer why a foreground process was rejected without reconstructing the condition externally.

Do not make rejection reason strings the primary state; use an enum and format it for logging if needed.

---

## 8. Pure policy function

Provide a deterministic policy entry point equivalent to:

```cpp
GameScreenAdmissionResult EvaluateGameScreenAdmission(
    const GameScreenObservation& observation) noexcept;
```

Recommended evaluation order:

```text
1. HWND exists
2. top-level/root window valid
3. visible
4. not minimized
5. not DWM-cloaked
6. process/PID successfully inspected
7. executable is not a high-confidence excluded image
8. monitor resolved
9. window and monitor bounds resolved
10. fullscreen-like monitor coverage passes
```

Return the first applicable rejection reason.

Do not add fuzzy scoring or weak heuristic aggregation.

---

## 9. Win32 observation capture

Provide a narrow helper that gathers the observation from an HWND/PID, for example:

```cpp
GameScreenObservation ObserveGameScreen(HWND window, DWORD processId) noexcept;
```

or an equivalent optional/result-returning helper.

The helper should gather only the facts needed by admission.

### Required window checks

Use current Win32/DWM APIs to determine:

```text
HWND validity
root/top-level relationship
IsWindowVisible
IsIconic
DWM cloaked state
```

For top-level status, remain compatible with the production source's existing root-window concept. Do not introduce an aggressive owner-window rule that could reject valid borderless/FSE game windows without field evidence.

### DWM cloaking

Use:

```cpp
DwmGetWindowAttribute(
    hwnd,
    DWMWA_CLOAKED,
    ...)
```

A positive cloaked value must reject the screen.

If querying cloaking fails, choose a deterministic conservative behavior and cover it in tests/implementation comments. Do not silently classify an unknown window as a known valid game screen solely because DWM inspection failed.

### Process image

Reuse or factor the existing `ProductionTargetPolicy` process-image normalization/exclusion logic rather than creating a parallel list.

The admission component must be able to distinguish at least conceptually between:

```text
process could not be inspected
vs.
process image was inspected and is explicitly excluded
```

If the current `InspectProductionTargetProcess()` abstraction makes that distinction impossible, perform the smallest sensible refactor so both call sites share one normalized image/exclusion authority.

Do not duplicate `Basename`/ASCII-lowercase/exclusion semantics in two divergent implementations.

---

## 10. Geometry collection

Use physical desktop bounds consistently.

Preferred window bounds:

```cpp
DwmGetWindowAttribute(
    hwnd,
    DWMWA_EXTENDED_FRAME_BOUNDS,
    &rect,
    sizeof(rect))
```

with a safe fallback to:

```cpp
GetWindowRect(hwnd, &rect)
```

if extended frame bounds are unavailable.

Resolve the relevant monitor with:

```cpp
MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
```

and retrieve monitor geometry through:

```cpp
GetMonitorInfoW(...)
```

For fullscreen-like admission compare against:

```cpp
MONITORINFO::rcMonitor
```

not `rcWork`.

A maximized normal desktop window that leaves the taskbar/work area uncovered must not pass merely because it fills `rcWork`.

---

## 11. Fullscreen-like rule

Start with an 8 physical-pixel edge tolerance:

```cpp
inline constexpr LONG kGameFullscreenTolerancePx = 8;
```

Provide a pure helper equivalent to:

```cpp
bool CoversMonitorBounds(
    const RECT& windowBounds,
    const RECT& monitorBounds,
    LONG tolerancePx = kGameFullscreenTolerancePx) noexcept;
```

Compare all four edges.

Equivalent behavior:

```cpp
return std::abs(window.left   - monitor.left)   <= tolerancePx &&
       std::abs(window.top    - monitor.top)    <= tolerancePx &&
       std::abs(window.right  - monitor.right)  <= tolerancePx &&
       std::abs(window.bottom - monitor.bottom) <= tolerancePx;
```

The implementation may use a safer integer-difference helper to avoid signed overflow concerns, but preserve the semantics.

### Required field-derived cases

With:

```text
monitor = 0,0,1920,1200
```

these must behave as follows:

```text
window =  0, 0,1920,1200    -> PASS
window = -3,-3,1923,1203    -> PASS
window = -8,-8,1928,1208    -> PASS
window = -9,-9,1929,1209    -> FAIL
window =  0, 0,1920,1128    -> FAIL
```

The `1920x1128` case is important: it represents a work-area-sized/maximized shape and must not be treated as fullscreen-like on a `1920x1200` monitor.

Do not add percentage-area scoring in this PR.

---

## 12. DPI constraints

Production is already Per-Monitor-V2 DPI aware.

Therefore:

- do not add a new DPI-awareness call;
- do not convert one side to logical coordinates while leaving the other physical;
- do not use the standalone Diag's old `1280x800` virtualized monitor values as production expectations;
- keep `DWMWA_EXTENDED_FRAME_BOUNDS` and `rcMonitor` comparisons in the production process's current coordinate space.

If a helper comment is useful, state explicitly that fullscreen comparison assumes the existing Per-Monitor-V2 process contract established by `main.cpp`.

---

# Part B — production WinEvent foundation

## 13. Extend `ProductionWindowEventType`

Change the enum to include the two newly required event types:

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

Keep the names explicit. Do not collapse them into a generic changed event; later production logic needs to retain event provenance for debug traces and targeted re-evaluation.

---

## 14. Extend event mapping

`MapProductionWindowEvent()` must map:

```text
EVENT_OBJECT_CREATE         -> Create
EVENT_OBJECT_SHOW           -> Show
EVENT_OBJECT_HIDE           -> Hide
EVENT_OBJECT_LOCATIONCHANGE -> LocationChange
EVENT_OBJECT_DESTROY        -> Destroy
```

Unknown events continue returning `std::nullopt`.

---

## 15. Extend WinEvent hooks

The current source has three independently registered exact-event hooks.

Expand the hook/event arrays to cover five events while preserving the existing model:

```text
CREATE
SHOW
HIDE
LOCATIONCHANGE
DESTROY
```

Do not replace this with a broad range hook unless there is a concrete implementation reason. Exact hooks keep callback volume and policy obvious.

Preserve:

```text
WINEVENT_OUTOFCONTEXT
OBJID_WINDOW filtering
CHILDID_SELF filtering
immediate top-level filtering
bounded queue
worker dispatch
safe Stop()/unhook behavior
```

Do not add:

```text
foreground polling
EnumWindows loops
process-list polling
timers
FindWindow loops
WMI polling
PDH polling
```

This PR must remain event-driven.

---

## 16. Do not consume the new events as new production decisions yet

This boundary is important.

The current `GameSessionController::HandleProductionWindowEvent()` only reacts to CREATE/SHOW for the legacy Steam candidate path.

In this PR:

- adding HIDE and LOCATIONCHANGE to the source is required;
- delivering them through the existing queue/callback path is required;
- changing the legacy candidate/commit logic to use them is **not** required;
- do not accidentally let LOCATIONCHANGE create/replace legacy candidates;
- do not make HIDE clear a committed target.

Later cutover PRs will use these events to call one foreground-first re-evaluation path.

If the current handler naturally ignores the new enum values because it checks CREATE/SHOW explicitly, preserve that behavior and add tests if useful.

---

# Part C — exact executable exclusions

## 17. Update the centralized reject list

At minimum add the field-demonstrated false positive:

```text
windowsterminal.exe
```

Also add the following high-confidence non-game Windows/helper executables if they are not already present:

```text
runtimebroker.exe
dllhost.exe
backgroundtaskhost.exe
werfault.exe
crashreportclient.exe
```

Use normalized lowercase exact executable basenames consistent with the existing policy.

Do not add title matching.

Do not add wildcard/fuzzy executable matching.

Do not broadly reject shared runtimes such as `msedgewebview2.exe` without specific field evidence; the existing test intentionally protects that behavior.

---

## 18. Keep exclusion semantics narrow

The purpose of the exclusion list is high-confidence negative evidence.

It is not intended to become a complete list of every non-game executable on Windows.

Do not add speculative entries only because they look system-like.

Every new entry in this PR should satisfy one of:

1. directly demonstrated false positive / observed helper in the field capture; or
2. clearly non-game OS crash/runtime infrastructure with no plausible supported game-target role.

---

# Part D — tests

## 19. Add `GameScreenAdmission` tests

Add a dedicated deterministic test target/file, suggested:

```text
tests/GameScreenAdmissionTests.cpp
```

Prefer testing pure observation -> decision behavior rather than manipulating real foreground windows in CI.

Cover at minimum:

### Basic rejection order

```text
null HWND -> NoWindow
not top-level -> NotTopLevel
not visible -> NotVisible
minimized -> Minimized
cloaked -> Cloaked
process unavailable -> ProcessUnavailable
excluded image -> ExcludedExecutable
monitor unavailable -> NoMonitor
bounds unavailable -> BoundsUnavailable
fullscreen mismatch -> NotFullscreenLike
valid observation -> admitted
```

Exact enum names may differ, but equivalent behavioral coverage is required.

### Geometry

Cover:

```text
exact 1920x1200 -> pass
3px overscan each side -> pass
8px edge tolerance -> pass
9px edge mismatch -> fail
1920x1128 work-area-sized -> fail
non-zero monitor origin -> same behavior
negative monitor coordinates -> same behavior
```

The last two are important for multi-monitor desktop coordinates.

Example non-zero monitor:

```text
monitor = 1920,0,3840,1200
window  = 1917,-3,3843,1203
-> PASS
```

Do not assume the primary monitor begins at `(0,0)`.

### Image rejection

Ensure a normalized excluded executable cannot be admitted even when every window/geometry condition passes.

---

## 20. Update `ProductionGameWindowSourceTests`

Existing tests currently expect HIDE to be unsupported. Replace that expectation.

Required mappings:

```cpp
Check(MapProductionWindowEvent(EVENT_OBJECT_CREATE) == ProductionWindowEventType::Create, ...);
Check(MapProductionWindowEvent(EVENT_OBJECT_SHOW) == ProductionWindowEventType::Show, ...);
Check(MapProductionWindowEvent(EVENT_OBJECT_HIDE) == ProductionWindowEventType::Hide, ...);
Check(MapProductionWindowEvent(EVENT_OBJECT_LOCATIONCHANGE) == ProductionWindowEventType::LocationChange, ...);
Check(MapProductionWindowEvent(EVENT_OBJECT_DESTROY) == ProductionWindowEventType::Destroy, ...);
```

Also retain a check that an unrelated unsupported event returns no mapping.

Preserve existing tests for:

- object/child filtering;
- top-level filtering;
- FIFO queue order;
- bounded queue capacity;
- dropped event accounting;
- repeated `Stop()` safety;
- empty callback rejection.

Do not weaken queue tests to accommodate the new events.

---

## 21. Update `ProductionTargetPolicyTests`

Add explicit exact-rejection checks for the new entries.

At minimum:

```text
windowsterminal.exe
runtimebroker.exe
dllhost.exe
backgroundtaskhost.exe
werfault.exe
crashreportclient.exe
```

Also preserve the existing positive controls:

```text
game.exe
DaveTheDiver.EXE
Game-Win64-Shipping.exe
msedgewebview2.exe remains not globally rejected
```

The PR must not accidentally broaden normalization/exclusion behavior beyond exact basename matching.

---

## 22. Build/test registration

Register any new test source/target consistently with the repository's current CMake test organization.

Do not reorganize unrelated CMake test infrastructure as part of this PR.

Use the existing test style unless a minimal helper extraction is necessary.

Run the repository's relevant test/build validation required by the existing project instructions, including at least:

```text
GameScreenAdmission tests
ProductionGameWindowSource tests
ProductionTargetPolicy tests
full ClawHUD test suite
Debug/Release build paths required by current repository practice
```

If a live DWM/foreground test would be environment-sensitive, keep it out of CI and cover the decision logic through injected/pure observations instead.

---

# Part E — production behavior constraints

## 23. Preserve current detector behavior in this PR

Do not yet change:

```text
GameDetectionCoordinator states
Idle / Armed / Verifying / Ready / Committed
CandidateDisposition
SteamRunningAppTrigger behavior
MicrosoftGameTrigger behavior
GenericForegroundTrigger behavior
GameRenderVerifier lifecycle
TryCommitReadyCandidateFromForeground
ForegroundTracker tracked PID semantics
ProductionProcessLifetimeWatcher
ReleaseCommittedProductionTarget
App::ReconcileHudVisibility
ProductionTelemetryController committed FPS target
Always-mode FPS target behavior
```

The new admission component may be compiled and tested but remain unused by production decision code until the later detector-core/cutover PRs.

This intentional staging keeps PR1 independently reviewable.

---

## 24. Do not introduce a second game detector

PR1 is a foundation PR, not a temporary shadow production detector.

Do not add code that evaluates every foreground change through both old and new detectors in production merely for logging.

The standalone Diag already exists for research.

If minimal debug-only formatting for admission results is useful for unit tests, keep it local; do not create another runtime observation pipeline.

---

# Part F — HUD / VRR safety contract

## 25. Non-negotiable presentation invariants

This PR is game-detection foundation work only.

Do not modify, replace, weaken, or work around any of the following:

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
existing Presentation API / DirectComposition production path
premultiplied-alpha contract
```

Do not touch HUD renderer opacity behavior or presentation composition.

Existing presentation regression tests/assertions must remain intact.

No game-detection requirement in this PR justifies a change to the VRR-critical HUD presentation contract.

---

# Part G — expected files

## 26. Expected primary touch points

Likely files:

```text
src/ClawHUD/GameDetection/GameScreenAdmission.h             new
src/ClawHUD/GameDetection/GameScreenAdmission.cpp           new
src/ClawHUD/GameDetection/ProductionGameWindowSource.h      modify
src/ClawHUD/GameDetection/ProductionGameWindowSource.cpp    modify
src/ClawHUD/ProductionTargetPolicy.cpp                      modify
possibly src/ClawHUD/ProductionTargetPolicy.h               small refactor only if needed to share normalized image inspection

tests/GameScreenAdmissionTests.cpp                          new
tests/ProductionGameWindowSourceTests.cpp                   modify
tests/ProductionTargetPolicyTests.cpp                       modify
CMakeLists / cmake test registration                        minimal update
```

Do not treat this list as permission to move unrelated code.

If the cleanest implementation requires one small shared process-image helper file, that is acceptable, but keep the abstraction narrow and explain why in the PR description.

---

# Part H — acceptance scenarios

## 27. Admission policy examples

The pure admission policy must support these outcomes.

### Real fullscreen-like candidate

```text
visible=1
minimized=0
cloaked=0
topLevel=1
image=game.exe
monitor=0,0,1920,1200
window=0,0,1920,1200

=> admitted
```

### Borderless overscan example from field data

```text
image=minecraft.windows.exe
monitor=0,0,1920,1200
window=-3,-3,1923,1203

=> admitted
```

### Maximized work-area-sized desktop window

```text
image=notepad.exe (assuming not otherwise excluded)
monitor=0,0,1920,1200
window=0,0,1920,1128

=> NotFullscreenLike
```

### Field-demonstrated Terminal false positive

```text
image=windowsterminal.exe
all window conditions otherwise valid
monitor coverage passes

=> ExcludedExecutable
```

This prevents the later detector from depending on renderer evidence to reject Terminal.

### Cloaked fullscreen-sized window

```text
cloaked=1
geometry covers monitor

=> Cloaked
```

Geometry must never override a hard visibility rejection.

---

## 28. Event source examples

These callbacks must be representable in `ProductionWindowEvent` after this PR:

```text
CREATE
SHOW
HIDE
LOCATIONCHANGE
DESTROY
```

The source must still discard:

```text
OBJID_CLIENT
child objects
non-top-level immediate observations
unknown/unmapped events
```

No polling is introduced.

---

# Part I — non-goals

## 29. Explicit non-goals

Do **not** implement any of the following in this PR:

- `KnownGameProcessCache`;
- process-generation-aware shared game cache;
- new `ForegroundGameDetector` core;
- Steam session-context redesign;
- replacement of `GameDetectionCoordinator`;
- removal of `Committed`;
- current-foreground game PID authority;
- HUD visibility cutover;
- In-Game Only FPS target cutover;
- renderer-verification token redesign;
- graphics API target semantic changes;
- suspend/resume redesign;
- process lifetime redesign;
- Ghost retention;
- Game Bar special continuity;
- PresentMon TopGPU production use;
- PDH production polling;
- generic process scanning;
- window-title heuristics;
- fuzzy executable matching;
- support for windowed games outside the current fullscreen-like product direction.

Those belong to later PRs or are intentionally excluded from the design.

---

# Part J — completion criteria

## 30. Definition of done

This work order is complete when all of the following are true:

1. `GameScreenAdmission` exists as a standalone testable component.
2. Win32 observation capture and pure admission policy are separated enough for deterministic tests.
3. admission checks include top-level, visibility, minimized, DWM cloaking, process availability/exclusion, monitor availability, bounds availability, and fullscreen-like coverage.
4. fullscreen-like comparison uses monitor bounds (`rcMonitor`) with an 8 physical-pixel edge tolerance.
5. exact fullscreen and Minecraft-style ±3 px overscan pass.
6. work-area-sized `1920x1128` on `1920x1200` fails.
7. multi-monitor/non-zero/negative desktop coordinates are covered by tests.
8. `ProductionGameWindowSource` supports CREATE, SHOW, HIDE, LOCATIONCHANGE, DESTROY.
9. existing top-level/object/queue/stop semantics remain intact.
10. `windowsterminal.exe` is rejected by the centralized production image policy.
11. the additional high-confidence helper/system images listed in this work order are rejected unless current-main code already rejects them.
12. existing known-valid game executable normalization tests still pass.
13. `msedgewebview2.exe` is not accidentally globally rejected.
14. no active game-session decision logic has been cut over to `GameScreenAdmission` yet.
15. no HUD visibility, FPS target, Steam, Microsoft identity, renderer verification, or sticky coordinator semantics change in this PR.
16. no production polling is added.
17. HUD presentation / VRR safety contract is untouched.
18. relevant targeted tests pass.
19. full repository test/build validation required by current project policy passes.

---

## 31. PR description expectations

In the PR description, state clearly that this is **R1 foundation only**.

Summarize:

```text
- adds reusable foreground screen-admission policy
- adds HIDE + LOCATIONCHANGE production WinEvent coverage
- adds field-backed exact non-game exclusions
- adds deterministic tests
- intentionally does not switch production detector behavior yet
```

Reference:

```text
docs/GAME_DETECTION_FIELD_ANALYSIS_2026-08-31.md
docs/GAME_DETECTION_REDESIGN_PR_PLAN_2026-08-31.md
```

Do not describe the PR as fixing the complete In-Game Only detector. The sticky committed-state removal and production cutover occur in later PRs.
