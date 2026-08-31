# ClawHUD Production Refactor Plan

Status: **ACTIVE — re-baselined after PR #175 and PR #176**  
Baseline commit: `c0a2dcbd598ad7a31fa7dd28fec09cbd9c29e1f2`  
Date: 2026-08-31  
Repository: `onehoon/ClawHUD`

This document is the continuation artifact for the production-code refactor. It is
intentionally detailed so a later ChatGPT/Codex session can continue without needing the
conversation that produced the plan.

The previous version of this document was written while the application still contained
several in-app diagnostics and the legacy `PresentMon.exe` verifier/debug paths. That
architecture no longer exists. **Do not execute the old PR 3/5/6/7/8/9 sequence from git
history as-is.** This document replaces it with a fresh plan based on current `main` after:

- PR #175: production `GameRenderVerifier` and debug `PresentActivitySource` moved to the
  shared PresentMon API2 session and `PresentMon.exe` was removed from runtime/build/package.
- PR #176: the in-app PresentMon API2 diagnostic and `GameDetectionProbe` were archived and
  removed from `ClawHUD.exe`.

The active tree is now production-oriented enough that the remaining `App` responsibilities
can be evaluated without diagnostic lifecycle noise.

---

## 1. Executive conclusion

The application does need another structural refactor, but **not** a broad rewrite and not
an attempt to turn every subsystem into an interface.

Most lower-level components are already reasonably isolated and tested. The remaining
architectural problem is concentrated in `App`:

- `App.cpp` is still roughly two thousand lines of production orchestration.
- `App.h` still owns state from HUD presentation, telemetry, foreground/game detection,
  process lifetime, PresentMon targeting, EC, power/battery, graphics-API probing,
  suspend/resume, debug instrumentation, settings, tray lifecycle, updates, and tweaks.
- Existing domain classes are generally good, but `App` still directly wires all of them
  together and contains the high-level state transitions.
- `TrayIcon` and `SettingsWindow` correctly use `App` as their application facade; changing
  those UI call sites is not necessary for this refactor.

The recommended end state is:

```text
main.cpp
  |
  `-- App                         composition root + app shell + facade
       |
       |-- HudSettingsStore       persistence
       |-- PresentMonTelemetryProvider
       |       shared ONE production API2 session
       |
       |-- HudController          HUD state + presentation + visibility
       |       `-- HudPresentation   VRR-critical black box
       |
       |-- ProductionTelemetryController
       |       |-- EcHelperClient
       |       |-- HudTelemetryAggregator
       |       |-- BatteryPowerEstimator
       |       |-- AlwaysModeFpsTarget / FpsStaleHold
       |       `-- IntelGraphicsApiProbe
       |
       |-- GameSessionController
       |       |-- ForegroundTracker
       |       |-- GameDetectionCoordinator
       |       |-- Steam / Generic / Microsoft triggers
       |       |-- ProductionGameWindowSource
       |       |-- ProductionProcessLifetimeWatcher
       |       |-- SteamRunningAppIdSource
       |       `-- GameRenderVerifier ----> shared PresentMonTelemetryProvider
       |
       |-- DebugObservationController      optional late extraction
       |       |-- WindowsGameIdentitySource
       |       |-- ProcessLifecycleSource
       |       |-- WindowLifecycleSource
       |       `-- PresentActivitySource --> shared PresentMonTelemetryProvider
       |
       `-- TweakStartupCoordinator         already isolated; keep as-is
```

`App` remains the mediator at the top. That is deliberate. The goal is to remove ownership
of subsystem internals from `App`, not to introduce an event bus, dependency-injection
framework, or abstract base class for every component.

A final `App.cpp` around several hundred lines is desirable, but **line count is not an
acceptance criterion**. Ownership clarity and preservation of behavior are more important
than forcing `App` below an arbitrary number.

---

## 2. What was reviewed for this re-baseline

The plan was rebuilt from current `main`, with emphasis on the active production tree:

### App shell / orchestration

- `src/ClawHUD/App.cpp`
- `src/ClawHUD/App.h`
- `src/ClawHUD/main.cpp`
- `src/ClawHUD/TrayIcon.*`
- `src/ClawHUD/SettingsWindow*`
- `src/ClawHUD/HudSettingsStore.*`

### HUD / presentation

- `HudModel.*`
- `HudSize.*`
- `HudRenderer.*`
- `HudPresentation.*`
- `HudPresentationContract.*`
- `HudPresentationLifecycle.*`
- `HudWindowGeometry.*`

### Production telemetry

- `HudTelemetryAggregator.*`
- `EcHelperClient.*`
- `MsiEcHudTelemetry.*`
- `WindowsPowerTelemetry.*`
- `WindowsMemoryTelemetry.*`
- `BatteryPowerEstimator.*`
- `AlwaysModeFpsTarget.*`
- `FpsStaleHold.*`
- `IntelGraphicsApiProbe.*`

### PresentMon API2

- `PresentMonApi2Client.*`
- `PresentMonTelemetryProvider.*`
- `PresentMonProcessTelemetry.*`
- `PresentMonSystemTelemetry.*`
- `PresentMonFrameTelemetry.*`
- `PresentMonDebugFrameTelemetry.*`
- `PresentMonRuntimeBootstrap.*`

### Production game detection

- `GameDetection/GameDetectionCoordinator.*`
- `GameDetection/GameRenderVerifier.*`
- `GameDetection/SteamRunningAppTrigger.*`
- `GameDetection/GenericForegroundTrigger.*`
- `GameDetection/MicrosoftGameTrigger.*`
- `GameDetection/ProductionGameWindowSource.*`
- `GameDetection/ProductionProcessLifetime.*`
- `ForegroundTracker.*`
- `SteamRunningAppIdSource.*`
- `ProductionTargetPolicy.*`
- scenario/unit tests under `tests/`

### Other active runtime pieces

- `GameDetection/WindowsGameIdentitySource.*`
- `GameDetection/ProcessLifecycleSource.*`
- `GameDetection/WindowLifecycleSource.*`
- `GameDetection/PresentActivitySource.*`
- `Tweaks/*`
- `SupportedHardware.*`
- `UninstallCleanup.*`
- active test declarations in `CMakeLists.txt`

The current test baseline after PR #176 is **46 active CTest targets**.

---

## 3. What is already in good shape — do not refactor just for symmetry

A major change from the old plan is that a lot of the code no longer needs architectural
work.

### 3.1 `HudPresentation` and `HudRenderer`

These are already meaningful components. `HudPresentation` owns the production Presentation
API / DirectComposition / D3D / D2D path and `HudRenderer` owns drawing/layout work.

Do not split the presentation internals into more layers during this refactor. The future
`HudController` should **contain and call `HudPresentation` as a black box**, not rewrite it.

### 3.2 PresentMon API2 stack

PR #175 established a good shared model:

```text
PresentMonTelemetryProvider
  `-- PresentMonApi2Client
       ONE session
       per-PID reference-counted tracking

consumers:
  - PresentMonProcessTelemetry        HUD FPS
  - PresentMonSystemTelemetry         CPU/GPU/system telemetry
  - PresentMonFrameTelemetry          GameRenderVerifier first-frame evidence
  - PresentMonDebugFrameTelemetry     debug PresentActivitySource
```

Do not create a session per controller and do not wrap the provider in another generic
telemetry abstraction. Keep `PresentMonTelemetryProvider` as a concrete shared service at
the composition-root level and inject it by reference into consumers that need it.

The per-PID tracking reference counts are an important production invariant: FPS telemetry,
render verification, and debug observation must not accidentally stop tracking a PID while
another consumer still holds it.

### 3.3 Game-detection state machine and trigger classes

`GameDetectionCoordinator` already owns the important pure state:

```text
Idle -> Armed -> Verifying -> Ready -> Committed
```

The three-trigger model is already separated:

- Steam `RunningAppID`
- generic foreground
- Microsoft/Xbox identity

The refactor should move **orchestration around these objects**, not redesign the state
machine or trigger policy.

`docs/GAME_DETECTION_PRODUCTION_DESIGN.md` remains the behavioral design reference for the
production detector. Historical diagnostic sections in that file are not architectural
requirements for this refactor.

### 3.4 Settings UI

`SettingsWindow` is already split across tab/source files. It can continue holding an
`App&` and calling the `App` facade. Injecting `HudController`, telemetry, or game-session
objects into the window would couple UI directly to runtime implementation and provide
little benefit.

### 3.5 Tweaks

`TweakStartupCoordinator` and `Tweaks/IntelVrr/*` are already isolated enough. Leave them
alone unless an independent functional change requires work there.

### 3.6 Small pure helpers already extracted

Keep and reuse the existing tested components:

- `Win32Format`
- `ProcessLiveness`
- `HudSettingsStore`
- `HudTelemetryAggregator`
- `HudModel` decision helpers
- `AlwaysModeFpsTarget`
- `FpsStaleHold`
- `BatteryPowerEstimator`
- `ProductionTargetPolicy`

Do not merge them back into controllers merely to reduce file count.

---

## 4. Current `App` responsibility map

The remaining problem is easier to see by grouping current `App` members and methods by
actual responsibility.

### 4.1 Application shell

Current responsibilities that legitimately belong near `App`:

- `HINSTANCE`
- single-instance mutex
- update check / Velopack handoff
- supported-hardware gate
- startup shortcut registration
- tray construction/destruction
- settings-window ownership
- global message loop
- application exit
- top-level logging initialization
- loading/saving aggregate user settings

These can stay in `App`.

### 4.2 HUD presentation and HUD user state

Currently owned directly by `App`:

```text
hudPresentation_
hudOptions_
hudFont_
hudSizeOffset_
manualHudVisibilityOverride_
mockHudEnabled_
hudInitializedLogged_
hudRenderFailureLogged_
hudShowFailureLogged_
hudHideFailureLogged_
```

Related methods include:

```text
EnsureMockHud
StopMockHud
SetHudEnabled
SetHudAlignment
SetHudFont
SetHudBackgroundMode
SetHudOpacity
SetHudSizeOffset
BuildHudRenderOptions
RecreateHudPresentation
RefreshMockHud
RenderProductionHud
SetHudVisibilityMode
HandleHudToggleHotkey
ReconcileHudVisibility
MockHudVisible
```

This is the clearest candidate for a dedicated HUD controller.

### 4.3 Production telemetry and sampling

Currently owned directly by `App`:

```text
ecHudClient_
telemetryAggregator_
latestProcessFps_
fpsStaleHold_
lastFpsCompareLogTick_
alwaysFpsTarget_
latestPowerTelemetry_
batteryPowerEstimator_
batteryEcOnDc_
batteryEcReadyLogged_
graphicsApiProbe_
latestGraphicsApi_
graphicsApiProcessId_
graphicsApiAttempts_
ecHudSamplingActive_
```

Related methods:

```text
ReadHudEcTelemetry
SampleProductionTelemetry
SampleProductionBatteryTelemetry
SampleProductionFpsTelemetry
StartProductionEcSampling
StopProductionEcSampling
StartProductionFpsSampling
StopProductionFpsSampling
PauseProductionSamplingForSuspend
StartGraphicsApiProbe
StopGraphicsApiProbe
TryGraphicsApiProbe
```

This is one subsystem despite the historical `Ec` naming. The sampling start/stop methods
manage much more than EC and should move together.

### 4.4 Production game-session detection

Currently owned directly by `App`:

```text
foregroundTracker_
gameDetectionCoordinator_
steamRunningAppTrigger_
genericForegroundTrigger_
microsoftGameTrigger_
productionGameWindowSource_
productionProcessLifetimeWatcher_
gameRenderVerifier_
steamRunningAppIdSource_
steamRunningAppId_
```

Related `App` glue:

```text
ReevaluateProductionGameDetection
HandleProductionForegroundChanged
HandleProductionWindowEvent
HandleProductionProcessExit
HandleMicrosoftGameEvidence
ApplyProductionEvidence
HandleGameDetectionTransition
StartCandidateRenderVerification
ArmProductionProcessLifetime
HandleGameRenderVerifierEvent
TryCommitReadyCandidateFromForeground
ReleaseProductionGameCandidate
ClearProductionCandidate
ReleaseCommittedProductionTarget
```

`App` also owns the `WM_APP` update structs/messages for verifier, production-window,
Microsoft evidence, process-exit, foreground and Steam registry changes.

This is the largest remaining domain-specific orchestration block and should eventually
become `GameSessionController`.

### 4.5 Suspend/resume

Current `App` owns:

```text
suspended_
resumeRecoveryActive_
resumeRecoveryAttempts_
```

and:

```text
HandleSystemSuspend
HandleSystemResume
TryResumeRecovery
CancelResumeRecovery
```

Suspend/resume intentionally crosses HUD presentation, telemetry, foreground state,
render verification, graphics API state, and game-detection re-evaluation. It should **not**
be the first controller extracted. Keep it as top-level orchestration until the three
production runtime boundaries exist.

### 4.6 Debug-only observation

Current `App` owns:

```text
debugLoggingEnabled_
windowsGameIdentitySource_
processLifecycleSource_
windowLifecycleSource_
presentActivitySource_
```

This is separable, but it is not the core architectural risk. It can be extracted later as
`DebugObservationController` with a very small API:

```text
SetEnabled(bool)
OnForegroundChanged(hwnd, pid)
Stop()
```

It must remain observation-only and must never become game-detection authority.

---

## 5. Main architectural problems to solve

### 5.1 Ownership is spread through `App`

The lower-level classes are separated, but their state is not. To understand a HUD hide,
game commit, suspend, or target exit, a maintainer still has to follow many `App` members
and methods across a large file.

The refactor should make each state variable have an obvious owner.

### 5.2 Cross-subsystem methods have misleading ownership

Examples in current `main`:

- `StartProductionEcSampling()` starts EC/system telemetry **and** render verification
  **and** FPS sampling.
- `StopProductionEcSampling()` stops EC/battery/FPS and optionally render verification.
- `ResumeRecoveryCanRetainPresentMon()` now checks `GameRenderVerifier`, not the old
  `PresentMon.exe` child process.
- `CommittedTargetReleasePlan::stopPresentMon` now means render-verifier stop.
- several HUD names still contain the old `MockHud` terminology although this is the
  production HUD.

These names make later extraction harder because they preserve historical implementation
boundaries that no longer describe the code.

### 5.3 `ReconcileHudVisibility()` crosses domains

It currently performs more than show/hide:

1. checks suspend/resume state;
2. checks committed-game liveness and may release the game target;
3. checks graphics-API target liveness and may stop the probe;
4. resolves HUD visibility;
5. calls `HudPresentation::Show/Hide`;
6. starts/stops production sampling.

The target design should leave liveness/game-session decisions with the game-session side,
graphics-probe ownership with telemetry, and HUD visibility decisions with the HUD
controller.

Do not change these responsibilities in one untestable rewrite. Separate them incrementally
while preserving call order.

### 5.4 `TrayIcon` knows subsystem timer details

`TrayIcon::WindowProc` currently knows all timer IDs and directly calls:

```text
SampleProductionTelemetry
SampleProductionBatteryTelemetry
TryGraphicsApiProbe
TryResumeRecovery
SampleProductionFpsTelemetry
```

The tray/message HWND should remain the Win32 timer host, but `TrayIcon` should not need to
know which production subsystem owns each timer. Eventually it should forward timer events
to one `App::HandleTimer(id)` facade method; `App` then delegates to the owning controller.

### 5.5 Game-session `WM_APP` plumbing is owned by `App`

The posted-event update structs, message IDs, dequeue/delete logic and discard helpers all
exist because game-session sources cross threads. Once `GameSessionController` exists, that
controller should own this plumbing and expose a single message-dispatch entry point to
`App`.

### 5.6 Shutdown logic is duplicated

`App::~App()` and `App::Exit()` both stop most runtime sources in nearly the same order.
This is workable because the stop operations are mostly idempotent, but it is easy for one
path to miss a newly added subsystem.

Do not immediately create a generic lifecycle framework. During extraction, make each
controller's `Stop/Shutdown` idempotent; then centralize the common runtime stop sequence in
`App` once the number of calls is small.

---

## 6. Non-negotiable HUD / VRR safety boundary

The production HUD presentation contract is not part of this refactor.

Never modify, replace, weaken, or route around:

- HUD `windowExStyle`
- `WS_EX_TRANSPARENT`
- `WS_EX_NOACTIVATE`
- `WS_EX_TOPMOST`
- existing `WS_EX_LAYERED` behavior
- `WM_NCHITTEST -> HTTRANSPARENT`
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`
- `ProductionHudPresentationContract()`
- the independent-flip requirement
- the existing Presentation API / DirectComposition production path
- premultiplied-alpha presentation behavior

The future `HudController` owns the **lifecycle of the existing `HudPresentation` object**.
It does not own or redesign the presentation backend.

No refactor PR may use HUD cleanup as a reason to alter window opacity behavior,
click-through, activation, hit testing, buffer format/count, independent flip, or the
Presentation API path.

Existing presentation contract/lifecycle tests must remain unchanged and green except for
purely mechanical include/path updates.

---

## 7. Production behavior invariants that every extraction must preserve

These are more important than class boundaries.

### 7.1 HUD state / visibility

- Persisted HUD enabled state is restored at startup.
- `Always` mode shows the HUD independently of a committed game.
- `InGameOnly` uses the tracked committed-game foreground state.
- F8/manual visibility override wins over the configured visibility mode until reset by the
  existing setting transitions.
- Alt+Tab must hide/show according to current mode without destroying the committed game
  session.
- HUD hide stops expensive production telemetry sampling through the existing policy but
  must not accidentally clear a live committed game target.
- layout/font/background/size changes preserve current recreate/rollback behavior.
- a failed presentation recreation must roll the setting back exactly as today.

### 7.2 FPS target authority

- `Always` mode FPS target = current foreground PID only.
- `InGameOnly` FPS target = committed game PID only.
- a foreground PID change invalidates the previous Always-mode FPS immediately.
- no FPS value may leak from the old PID into a new PID.
- `FpsStaleHold` retains only short same-PID misses (current 2-second behavior).
- displayed FPS remains the primary HUD FPS source.
- switching visibility mode resets target authority and stale state as today.

### 7.3 PresentMon API2

- exactly one production `PresentMonTelemetryProvider` / API2 session.
- process tracking remains reference counted per PID.
- FPS, `GameRenderVerifier`, and debug frame observation may coexist on the same PID.
- `GameRenderVerifier` remains a first-displayed-frame verifier based on
  `BETWEEN_DISPLAY_CHANGE > 0`.
- verifier attempts remain generation-safe and re-arm/flush per attempt.
- no `PresentMon.exe` child-process path may return.

### 7.4 EC / battery / telemetry

- normal usage/system sampling interval remains 1 second.
- battery/power sampling interval remains 5 seconds.
- PresentMon FPS sampling interval remains 500 ms.
- graphics API probe retry remains 500 ms, bounded to the current maximum attempt count.
- EC/system missing-field retention keeps its current per-field threshold behavior.
- EC helper failure-abort semantics remain unchanged.
- battery estimator history resets on AC transition and invalid DC samples exactly as now.
- HUD rendering continues to use retained telemetry rather than dropping every segment on a
  single missed sample.

### 7.5 Game detection

- production detection stays event-driven; do not add discovery polling.
- Steam `RunningAppID` is a session/wake signal, not a PID verdict.
- generic foreground and Microsoft identity remain independent evidence sources.
- candidate generations isolate stale verifier/process events.
- candidate becomes `Ready` only after renderer evidence.
- commit requires candidate `Ready`, candidate alive, and candidate PID foreground.
- committed target survives Alt+Tab.
- Steam AppID clearing does not tear down an already committed live game.
- Microsoft identity candidate protection and existing replacement policy remain intact.
- process exit remains authoritative through `ProductionProcessLifetimeWatcher`.
- Steam Armed window CREATE/SHOW acceleration remains supported.

### 7.6 Suspend / resume

- suspend hides the HUD and pauses the production sampling/probe path.
- pending verifier/window/Microsoft events are discarded where they are today.
- resume recovery remains bounded: current 500 ms interval / six-attempt limit.
- missed-suspend fallback remains supported.
- a HUD expected to be visible must obtain a fresh presentable frame before being restored.
- game detection is re-evaluated only after successful recovery according to current policy.
- presentation recreation during recovery must not change the HUD presentation contract.

### 7.7 Debug logging

- enabling debug logging starts the current observation sources.
- disabling it stops them.
- `PresentActivitySource` remains API2-backed and observation-only.
- debug observers never feed production game-detection decisions.

---

## 8. Target controller boundaries

### 8.1 `HudController`

Recommended ownership:

```text
std::unique_ptr<HudPresentation>
HudLayoutOptions
HudFont
size offset
manual visibility override
HUD enabled state
presentation init/render/show/hide failure log latches
```

Recommended responsibilities:

```text
Ensure presentation
Shutdown presentation
Build HudRenderOptions
Render a supplied HudTelemetrySnapshot
Show / hide reconciliation from supplied high-level state
Set alignment / font / background mode / opacity / size
Set visibility mode
manual/F8 override state
presentation recreation + rollback
visibility/query getters used by App facade
```

It must **not** own:

- game-detection coordinator or candidate state;
- process-lifetime detection;
- PresentMon provider/session;
- EC helper;
- battery estimator;
- graphics API probe;
- game-render verifier;
- startup/update/tweak logic.

Suggested high-level inputs rather than back-references into `App`:

```text
foregroundGameActive
suspended
resumeRecoveryActive
HudTelemetrySnapshot
```

The class should not receive an `App&`.

### 8.2 `ProductionTelemetryController`

Recommended ownership:

```text
EcHelperClient
HudTelemetryAggregator
latest FPS
FpsStaleHold
AlwaysModeFpsTarget
FPS compare-log tick
Windows power snapshot
BatteryPowerEstimator
battery estimator transition/log flags
IntelGraphicsApiProbe
latest graphics API
graphics API PID / retry count
sampling-active state
```

Dependencies:

```text
PresentMonTelemetryProvider&   shared, non-owning
HWND timer host                or small timer callbacks from App
render-request callback        optional; keep narrow if used
```

Recommended responsibilities:

```text
Read EC telemetry
Sample 1s system/EC telemetry
Sample 5s battery telemetry
Sample 500ms FPS telemetry
Build/fill the complete telemetry snapshot
Start/stop/pause sampling
Manage foreground FPS target for Always mode
Manage committed PID FPS target for InGameOnly mode
Start/stop/retry graphics API probe
Reset telemetry on suspend/target changes
```

The controller should expose explicit target inputs rather than reading
`GameDetectionCoordinator` directly, for example:

```text
OnForegroundProcessChanged(pid)
SetCommittedProcess(pid)
ClearCommittedProcess()
SetVisibilityMode(mode, currentForegroundPid)
```

This keeps telemetry independent of how a game was detected.

The controller must **not** stop `GameRenderVerifier`. Today
`StopProductionEcSampling(bool stopRenderVerification, ...)` mixes those responsibilities.
After extraction the orchestration layer must call game-session stop and telemetry stop
separately, in the same order required by the current behavior.

### 8.3 `GameSessionController`

Recommended ownership:

```text
ForegroundTracker
GameDetectionCoordinator
SteamRunningAppTrigger
GenericForegroundTrigger
MicrosoftGameTrigger
ProductionGameWindowSource
ProductionProcessLifetimeWatcher
GameRenderVerifier
SteamRunningAppIdSource
current Steam RunningAppID
all game-session WM_APP message IDs/update payloads
```

Dependency:

```text
PresentMonTelemetryProvider&   for GameRenderVerifier only
```

Recommended responsibilities:

```text
Start/stop production detection sources
foreground reconciliation
generic foreground evidence
Steam session changes
Steam Armed CREATE/SHOW candidates
Microsoft identity evidence application
candidate merge/replace/clear
renderer-ready handling
commit eligibility/commit
process-lifetime arm/exit handling
candidate/session generation management
posted-event queue cleanup
game-session logging
```

It should not call renderer/presentation internals directly.

Use a **small concrete callback/hook surface** to tell `App` about effects that cross the
game-session boundary. Do not introduce a generic event bus. The likely useful external
effects are:

```text
foreground PID changed
tracked/committed foreground match changed
candidate started/replaced/cleared
committed target changed (PID or none)
HUD visibility may need reconciliation
```

Do not finalize the hook list before preparing the behavior inventory for the extraction.
If the hook list grows into dozens of callbacks, the boundary is wrong; keep top-level
orchestration in `App` instead of forcing it into the controller.

`GameSessionController` should also own a message-dispatch function so `App::ProcessMessages`
can become approximately:

```cpp
if (gameSession_.HandleMessage(message))
    continue;
```

rather than knowing every cross-thread payload type.

### 8.4 `DebugObservationController` — optional late extraction

Recommended ownership:

```text
ProcessLifecycleSource
WindowLifecycleSource
PresentActivitySource
WindowsGameIdentitySource
```

Dependency:

```text
PresentMonTelemetryProvider&
```

API should stay small:

```text
SetEnabled(bool)
OnForegroundChanged(HWND, DWORD)
Stop()
```

This extraction is useful for reducing `App` noise but is not a prerequisite for the core
HUD/game-session architecture.

### 8.5 What `App` should own at the end

Expected long-lived ownership:

```text
HINSTANCE
single-instance mutex
TrayIcon
SettingsWindow
HudSettingsStore
PresentMonTelemetryProvider
HudController
ProductionTelemetryController
GameSessionController
(optional) DebugObservationController
TweakStartupCoordinator
start-with-Windows / debug/tweak top-level user settings
suspend/resume top-level orchestration state if still warranted
```

Expected responsibilities:

```text
composition/wiring
startup order
shutdown order
SettingsWindow / TrayIcon facade methods
persistence around controller setters
high-level cross-controller reactions
suspend/resume orchestration
message loop
update/single-instance/hardware gate
```

That is a valid final mediator. Do not remove it merely to claim that `App` is empty.

---

## 9. Recommended PR sequence

The old PR sequence is obsolete. Use the following sequence from current `main`.

### R0 — Post-cleanup naming and dead-surface normalization

Risk: **very low**  
Goal: remove historical names that obscure the real production boundaries before moving
code.

Recommended mechanical cleanup:

```text
mockHudEnabled_                 -> hudEnabled_
EnsureMockHud                   -> EnsureHud
StopMockHud                     -> StopHud
RefreshMockHud                  -> RefreshHud
MockHudVisible                  -> HudVisible

StartProductionEcSampling       -> StartProductionSampling
StopProductionEcSampling        -> StopProductionSampling
ecHudSamplingActive_            -> productionSamplingActive_

ResumeRecoveryCanRetainPresentMon
                                -> ResumeRecoveryCanRetainVerifier

CommittedTargetReleasePlan.stopPresentMon
CommittedTargetReleaseOps.stopPresentMon
                                -> stopRenderVerification
```

Also clean post-diagnostic dead signatures where still present in active production logic,
for example `ShouldSampleProductionTelemetry(resolvedShow, false, suspended_)` should no
longer carry a permanently-false diagnostic argument.

`TrackMockGameWindow` currently has no external production caller; verify with code search
at implementation time and remove it if still dead.

Rules:

- rename/move only;
- no policy changes;
- full tests;
- do not mix structural extraction into this PR.

Why first: every later behavior inventory becomes much easier to read when names describe
what the code actually does in 2026-08-31 `main`.

### R1 — Move suspend/resume pure policy out of `App.h`

Risk: **very low**  
Goal: reduce `App.h` as a grab bag and make suspend/resume tests independent of `App`.

Create a small production policy file, e.g.:

```text
SuspendResumePolicy.h
SuspendResumePolicy.cpp   # only if non-constexpr code is needed
```

Move the current pure helpers/constants used by `SuspendResumeRecoveryTests`, including the
500 ms / six-attempt policy and retain/wait/show decisions.

`tests/SuspendResumeRecoveryTests.cpp` should include the policy header directly rather than
`App.h`.

Do **not** move `HandleSystemSuspend`, `HandleSystemResume`, or `TryResumeRecovery` yet.
Those are cross-controller orchestration and become easier only after R2-R4.

### R2 — Extract `ProductionTelemetryController`

Risk: **medium-high**  
This is the first important runtime move.

Move the telemetry/sampling state listed in §8.2 and the corresponding methods out of
`App`.

Preserve exact timer intervals, reset behavior, logging cadence, target semantics and
sampling order.

Implementation strategy:

1. Add controller with existing state and methods moved as close to verbatim as possible.
2. Inject the existing `PresentMonTelemetryProvider&`; do not move/copy/create another
   provider.
3. Keep thin `App` forwarding methods initially so `TrayIcon` and game-session call sites do
   not all change in the same commit.
4. Make `App::RenderProductionHud` consume a snapshot produced/finalized by the telemetry
   controller, or use one narrowly-scoped render callback. Avoid a general host interface.
5. Move `IntelGraphicsApiProbe` into the telemetry controller in this PR; graphics API is a
   telemetry field, not game-detection state.
6. Keep render verification in the game-session side. Any current combined stop call must be
   decomposed at `App` while preserving its original order.
7. After behavior is stable, let `TrayIcon::WindowProc(WM_TIMER)` call one
   `App::HandleTimer(id)` method instead of individual telemetry methods. `App` delegates
   telemetry timer IDs to the controller and retains resume-recovery timer handling.

Expected state removed from `App` after R2:

```text
ecHudClient_
telemetryAggregator_
latestProcessFps_
fpsStaleHold_
lastFpsCompareLogTick_
alwaysFpsTarget_
latestPowerTelemetry_
batteryPowerEstimator_
batteryEcOnDc_
batteryEcReadyLogged_
graphicsApiProbe_
latestGraphicsApi_
graphicsApiProcessId_
graphicsApiAttempts_
productionSamplingActive_
```

Required tests before merge:

- existing `HudTelemetryAggregatorTests`
- `AlwaysModeFpsTargetTests`
- `FpsStaleHoldTests`
- PresentMon process/system/provider tests
- `WindowsPowerTelemetryTests`
- `MsiEcHudTelemetryTests`
- `TelemetryRetentionTests`
- `IntelGraphicsApiProbeTests`
- full suite
- add controller-level tests for pure target/reset decisions introduced by the move; do not
  create a fake Win32 universe solely to unit-test `SetTimer`.

### R3 — Extract `HudController`

Risk: **high because it touches presentation lifecycle; no presentation contract changes**.

Move the HUD state/methods listed in §8.1.

Important design rule:

> `HudPresentation` moves under the controller as the exact same concrete implementation.
> The controller is a lifecycle/state owner around it, not a replacement presentation
> layer.

Keep `App` facade methods used by `SettingsWindow` and `TrayIcon`. For example:

```text
App::SetHudAlignment -> hudController_.SetAlignment -> persist on success
App::SetHudFont      -> hudController_.SetFont      -> persist on success
App::HudOptions      -> hudController_.Options
```

Do not make `SettingsWindow` aware of `HudController`.

Split `ReconcileHudVisibility` carefully:

- game-process liveness/release must not be buried inside the HUD controller;
- graphics-probe liveness belongs to telemetry;
- resolved show/hide and Presentation `Show/Hide` belong to HUD;
- starting/stopping production sampling is a top-level reaction to HUD visibility and may be
  wired by `App` during this PR.

Do not simultaneously change visibility policy. Use existing `ResolveHudVisible` behavior.

Required tests:

- `HudModelTests`
- `HudSizeTests`
- `HudWindowGeometryTests`
- `HudPresentationContractTests`
- `HudPresentationLifecycleTests`
- `HudRendererTests`
- any tests covering recreate/rollback helpers
- full suite

Hardware smoke is strongly recommended before merge because a compile-clean presentation
lifecycle move can still reorder Show/Hide/Recreate calls.

### R4 — Extract `GameSessionController`

Risk: **highest refactor PR**.  
Do this only after R2 and R3 provide concise production APIs for telemetry and HUD.

Move the game-session ownership listed in §8.3.

Key requirement: keep `GameDetectionCoordinator` and the three-trigger production policy
unchanged. The PR is primarily ownership/message-plumbing relocation.

Recommended staged implementation inside the PR/branch:

1. create controller and move coordinator/triggers/Steam state;
2. move `ProductionGameWindowSource` and `ProductionProcessLifetimeWatcher` lifecycle;
3. move `GameRenderVerifier` ownership using the existing shared provider reference;
4. move `ForegroundTracker` and Steam registry source;
5. move game-session `WM_APP` IDs/update structs/posting/discard logic;
6. move transition handling and candidate/commit/release methods;
7. expose only the minimum cross-domain callbacks needed by `App`;
8. make `App::ProcessMessages` delegate game-session messages to the controller;
9. delete the old forwarding methods only after all call sites are clean.

Do not make the controller call `HudPresentation` or EC APIs directly. Cross-domain effects
should be high-level calls routed by `App`, such as:

```text
candidate changed -> telemetry FPS target/reset reaction
commit(pid)        -> telemetry committed target + graphics probe + sampling
release            -> telemetry clear target + visibility reconcile
foreground changed -> telemetry Always-mode target + HUD visibility + debug observation
```

The exact hook surface should be recorded in the PR behavior inventory before moving the
large switch in `HandleGameDetectionTransition`.

Required tests:

- `GameDetectionCoordinatorTests`
- `ProductionGameDetectionScenarioTests`
- `GameRenderVerifierTests`
- `ProductionGameWindowSourceTests`
- `ProductionProcessLifetimeTests`
- `SteamRunningAppIdSourceTests`
- `SteamRunningAppTriggerTests`
- `MicrosoftGameTriggerTests`
- `GenericForegroundTriggerTests`
- `ProductionTargetPolicyTests`
- `ForegroundTrackerTests`
- full suite

Required behavior review matrix is in §11.

### R5 — Re-evaluate suspend/resume after the controllers exist

Risk: **medium-high if moved; low if left in `App`**.

At this point re-read `App.cpp` before deciding whether a new controller is justified.

Preferred default:

- keep suspend/resume as top-level `App` orchestration;
- use the R1 pure policy helpers for decisions;
- call clear methods on `HudController`, telemetry controller and game-session controller.

Only create a `RuntimeLifecycleController`/`SuspendResumeCoordinator` if `App` still contains
a large amount of stateful retry machinery that is hard to understand after R2-R4.

Do not create a controller solely to make `App.cpp` smaller.

If extracted, it should own only lifecycle state/timer attempt state and issue high-level
operations. It must not absorb HUD presentation internals or game-detection policy.

### R6 — Optional `DebugObservationController`

Risk: **low-medium**.

Move debug source lifecycle out of `App` only after the production path is stable.

Keep the production/shared PresentMon API2 provider injected by reference.

Do not turn these observers into detection authority and do not couple them to
`GameSessionController` internals. They may receive foreground PID/window notifications
from `App`.

### R7 — Final `App` shell cleanup

Risk: **medium** because shutdown/startup ordering is visible here.

After R2-R6:

- centralize duplicated runtime stop steps used by `Exit()` and destructor;
- simplify `Run()` to source/controller startup and wiring;
- simplify `ProcessMessages()` to controller dispatch + Settings dialog dispatch;
- remove obsolete forwarding methods that no UI caller needs;
- keep the public `App` facade required by `SettingsWindow` and `TrayIcon`;
- update `APP_REFACTOR_BEHAVIOR_INVENTORY_TEMPLATE.md` if its caller checklist is stale;
- update this document's progress section.

Do not force a separate application-shell class unless `App` still has another real
ownership problem.

### R8 — Optional build-file organization

`CMakeLists.txt` is now large primarily because every test target is declared explicitly.
This is a maintainability issue but not a runtime architecture issue.

After the code refactor, optionally move test declarations to something like:

```text
cmake/ClawHUDTests.cmake
```

and/or keep source lists grouped in small CMake includes.

Do not mix this with R2-R4; it makes runtime diffs harder to audit.

---

## 10. Dependency/order summary

```text
R0 naming/dead surface
 |
R1 suspend/resume pure policy
 |
R2 ProductionTelemetryController
 |
R3 HudController
 |
R4 GameSessionController
 |
R5 suspend/resume re-evaluation
 |
R6 debug observation (optional)
 |
R7 final App shell cleanup
 |
R8 CMake cleanup (optional)
```

R2 and R3 are conceptually independent but the recommended order is telemetry first because
it removes EC/FPS/graphics state from `App` before the HUD visibility split. R4 should come
after both; otherwise game-session extraction needs a wide callback interface back into the
old monolithic `App`.

---

## 11. Hardware / behavior smoke matrix for medium/high-risk PRs

Unit tests protect pure policy and generation/state invariants, but R2-R4 move imperative
Win32 lifecycle glue. For those PRs, use this practical smoke matrix on supported MSI Claw
hardware when available.

### Startup

- HUD persisted ON -> presentation initializes and follows saved visibility mode.
- HUD persisted OFF -> no accidental HUD creation/sampling.
- startup with debug logging ON/OFF behaves as before.
- unsupported-hardware gate remains unchanged.
- single-instance behavior remains unchanged.

### HUD controls

- enable/disable HUD.
- F8 toggle.
- Always vs In-Game Only.
- left/center/right alignment.
- Full Width vs Content Width.
- font switch.
- HUD size +/-.
- background opacity setting behavior unchanged.
- presentation recreation failures still roll settings back rather than leaving mixed state.

### Telemetry

- CPU usage/temp/TDP.
- GPU usage/clock.
- RAM/VRAM.
- fan RPM.
- battery percentage / remaining time on DC.
- AC transition clears battery-discharge history as expected.
- transient telemetry miss does not erase unrelated HUD segments immediately.
- FPS does not leak between different foreground PIDs.

### Generic Win32 game

```text
foreground candidate
-> Verifying
-> API2 first displayed frame
-> Ready
-> foreground commit
-> HUD/telemetry target
```

Then:

- Alt+Tab out.
- return to game.
- exit game.

### Steam game

- `RunningAppID` arms before game window.
- game CREATE/SHOW or foreground evidence seeds candidate.
- renderer confirmation and commit.
- Alt+Tab does not destroy committed target.
- Steam AppID clear while committed does not prematurely release live game.
- game exit releases target.

### Microsoft/Xbox game

- Microsoft identity evidence can seed/replace a generic helper candidate according to
  existing policy.
- Microsoft-backed candidate remains protected from unrelated foreground helper windows.
- commit/Alt+Tab/exit behavior remains unchanged.

### Suspend/resume

- suspend while HUD visible.
- resume with game still alive.
- resume while a non-game window is temporarily foreground.
- HUD restores only after fresh render/presentation readiness.
- bounded recovery ends cleanly rather than spinning indefinitely.

### VRR/presentation safety

The refactor must not change the contract even if no dedicated diagnostic is currently in
the app. At minimum verify:

- all HUD presentation contract/lifecycle tests pass;
- click-through and no-activation remain normal on hardware;
- HUD remains topmost;
- normal game presentation/VRR behavior shows no regression during the HUD controller move.

Any VRR/presentation issue is a blocker for the responsible PR; do not solve it by changing
the production presentation contract.

---

## 12. Per-PR engineering rules

### 12.1 Prefer moves over redesign

For each controller extraction:

1. establish ownership seam;
2. move state;
3. move methods with minimal edits;
4. preserve existing call order;
5. add thin forwarding methods while callers migrate;
6. only then remove obsolete App surface.

Do not combine feature changes with controller extraction.

### 12.2 Behavior inventory is mandatory

Use `docs/APP_REFACTOR_BEHAVIOR_INVENTORY_TEMPLATE.md` for R2-R4 and any later PR that moves
imperative lifecycle code.

Inventory exact:

- early-return guards;
- timer Set/Kill ordering;
- Show/Hide/Recreate ordering;
- reset ordering;
- PID/generation guards;
- PostMessage ownership/delete rules;
- start/stop order of worker sources;
- persistence timing;
- error logging/latching behavior.

### 12.3 Do not over-abstract for tests

Do not add interface hierarchies around every Win32 API or `HudPresentation` just to mock it.
Extract pure decisions where they naturally exist and keep imperative glue covered by:

- existing lower-level tests;
- scenario tests;
- behavior inventories;
- full build/CTest;
- targeted hardware smoke for the risky moves.

### 12.4 Concrete dependencies are preferred

Use concrete references where there is one production implementation:

```text
PresentMonTelemetryProvider&
```

A narrow `std::function` callback is acceptable for a genuine cross-controller notification.
Do not create `IWhateverService` solely because a class moved out of `App`.

### 12.5 No event bus

The application is not large enough to justify a generic event bus. Win32 already supplies
message dispatch for cross-thread sources. Keep top-level interactions explicit and traceable.

### 12.6 Do not duplicate process/session authority

There must be one obvious owner for each state:

```text
HUD enabled/options/manual visibility -> HudController
telemetry retained values/FPS/EC/battery/graphics -> ProductionTelemetryController
game candidate/generation/commit/Steam session -> GameSessionController
PresentMon session/process refcounts -> PresentMonTelemetryProvider
suspend/resume top-level state -> App unless R5 proves extraction useful
```

If two controllers both start maintaining their own copy of the committed PID, foreground
state, or HUD enabled state as independent authority, stop and redesign the seam. Read-only
cached target inputs are fine; duplicated authority is not.

---

## 13. Message/timer ownership target

Current `TrayIcon` remains the hidden HWND host for application messages/timers. That is
fine and should not be replaced with another hidden window just for controller purity.

Target dispatch:

```text
TrayIcon::WindowProc
  WM_HOTKEY        -> App facade
  WM_POWERBROADCAST-> App suspend/resume
  WM_TIMER         -> App::HandleTimer(id)
  tray click/menu  -> App facade

App::ProcessMessages
  settings destruction
  -> GameSessionController::HandleMessage(MSG) for game-session WM_APP messages
  -> Settings dialog dispatch
  -> normal Translate/Dispatch
```

The controllers may continue using the tray HWND as their Win32 dispatch target. Ownership
means the controller owns the message ID/payload semantics and cleanup, not necessarily the
physical HWND.

---

## 14. Startup/shutdown ownership target

### Startup order to preserve unless a separate functional PR proves otherwise

High-level current order is approximately:

```text
single-instance gate
update check
supported-hardware gate
startup registration
tray/message HWND
production game-window source
Steam RunningAppID watcher + initial state
optional debug observation sources
HUD hotkey
PresentMonTelemetryProvider initialize
ForegroundTracker
persisted HUD restore / game re-evaluation
TweakStartupCoordinator
message pump
```

Do not casually reorder this while moving classes. Some sources require the tray HWND and
some PresentMon consumers require the provider session.

If a controller extraction wants a different order, document why and treat it as a behavior
change rather than hiding it inside the move.

### Shutdown

Every extracted controller `Stop/Shutdown` must be idempotent. The final `App` shutdown path
should make the ordering explicit rather than depending on member destruction side effects.

Particularly preserve:

- timers killed before resources they may call are destroyed;
- worker callbacks stopped/disarmed before the tray/message HWND is destroyed;
- queued heap payload messages discarded before final window destruction;
- verifier/process/source callbacks cannot post into a destroyed HWND;
- HUD presentation is shut down without changing presentation semantics;
- mutex release remains at application lifetime end.

---

## 15. Areas intentionally not included in the main refactor

### Diagnostics / future `ClawHUD.Diag.exe`

Not part of this plan. Archived diagnostics are reference-only under `archive/diagnostics/`.
A future standalone diagnostic console can be designed separately and must not drive the
production controller boundaries.

### HUD rendering/style redesign

Not part of this plan. Font, layout, colors, spacing, opacity semantics and renderer feature
work must be separate functional PRs.

### Game-detection policy redesign

Not part of this plan. If real logs show a detection bug, fix it separately and update
`GAME_DETECTION_PRODUCTION_DESIGN.md`; do not mix it into R4.

### PresentMon API2 redesign

Not part of this plan. The current shared-provider model is the architecture to preserve.

### EC helper protocol / shared helper architecture

Not part of this App refactor. Any future cross-application shared EC helper work should be
handled as its own project/architecture change.

### Settings UI redesign

Not part of this plan. `SettingsWindow` remains an `App` facade client.

---

## 16. Current cleanup opportunities discovered during review

These are worth remembering but should be assigned to the appropriate phase rather than
mixed randomly into controller PRs.

### High-value before extraction — completed in R0

- ~~normalize `MockHud` production naming~~ — done: `hudEnabled_`, `EnsureHud`,
  `StopHud`, `RefreshHud`, `HudVisible`, `HudEnabled`.
- ~~normalize sampling names that still say `Ec`~~ — done:
  `StartProductionSampling`, `StopProductionSampling`, `productionSamplingActive_`.
- ~~normalize stale `PresentMon` naming around `GameRenderVerifier`~~ — done:
  `ResumeRecoveryCanRetainVerifier`, `CommittedTargetRelease{Plan,Ops}::stopRenderVerification`.
- ~~remove the now-unused diagnostic argument from `ShouldSampleProductionTelemetry`~~ —
  done: signature is now `(resolvedShow, suspended)`.
- ~~confirm and remove dead `TrackMockGameWindow` surface~~ — done: no callers, deleted.

One name deliberately retained: the HUD window class string
`L"ClawHUD.MockHudSurface"` and window title `L"ClawHUD Mock HUD"` in
`HudPresentation.cpp` are runtime values inside the VRR-critical presentation
backend, not C++ symbols. Changing a `RegisterClassW` name is a behavioral change,
so R0 left them untouched.

### Good later cleanup

- `HudSettingsStore` now persists HUD, startup, debug and tweak settings; a later rename to
  `AppSettingsStore` may be clearer, but a rename alone is not worth blocking the runtime
  extraction.
- `SettingsWindow`'s Diagnostics naming now mostly means debug/log controls. Rename-only UI
  cleanup can wait.
- centralize duplicated `Exit`/destructor stop sequence after controller shutdown APIs are
  stable.
- optionally split test boilerplate out of the large root `CMakeLists.txt` after runtime
  architecture stabilizes.

### Do not "clean up" during this effort

- `HudPresentation` window/presentation path;
- API2 process tracking/session mechanics;
- game-detection trigger/state policy;
- EC decoding/protocol semantics;
- renderer styling/layout decisions.

---

## 17. Completion criteria for the whole refactor

The refactor is complete when all of the following are true:

1. `App` no longer owns the detailed EC/FPS/battery/graphics telemetry state.
2. `App` no longer owns `HudPresentation` or the detailed HUD recreate/show/hide state.
3. `App` no longer owns the production game-detection coordinator/source/verifier state.
4. one shared `PresentMonTelemetryProvider` remains the API2 authority.
5. `SettingsWindow` and `TrayIcon` still interact through a stable `App` facade.
6. suspend/resume behavior is explicit and understandable, whether it remains in `App` or is
   later given a small coordinator.
7. the message pump delegates game-session messages instead of decoding all payloads itself.
8. `TrayIcon` does not need to know each telemetry timer's implementation method.
9. all 46-baseline production tests (plus new tests added by the refactor) pass.
10. production game-detection scenario tests remain behaviorally unchanged.
11. the HUD presentation/VRR contract remains unchanged.
12. no legacy `PresentMon.exe` or in-app diagnostic dependency is reintroduced.
13. no new generic event bus / DI framework / unnecessary interface hierarchy was added.
14. each state variable has one clear production owner.
15. the resulting code is easier to extend without editing a single multi-domain `App.cpp`
    for every feature.

---

## 18. Progress log

### Historical pure-logic phase

Merged before this re-baseline:

- #167: original refactor plan / behavior inventory.
- #168: `Win32Format` / `ProcessLiveness` extraction.
- #169: `HudSettingsStore` extraction.
- #170: `HudTelemetryAggregator` extraction.
- #171: `ResolveHudVisible` extraction.

Those extractions remain valid and are retained in the new architecture.

### Architecture-changing cleanup immediately before this plan

- #175: `GameRenderVerifier` -> shared PresentMon API2; debug `PresentActivitySource` ->
  API2; `PresentMon.exe` build/runtime/package dependency removed.
- #176: in-app API2 diagnostic and `GameDetectionProbe` archived/removed; active
  `ClawHUD.exe` returned to a production-only baseline.
- #177: Settings **Diagnostics tab** deleted entirely; debug logging moved to a
  developer-only `[Developer] DebugLog` key in `settings.ini` (read once at
  startup, never written). Settings now has 3 tabs (General/HUD, Tweaks, About).

### Refactor phase progress

- **R0 (naming / dead-surface normalization)** — branch
  `refactor/r0-naming-cleanup`. Mechanical rename only, no behavior change.
  - Renames: `mockHudEnabled_`->`hudEnabled_`, `EnsureMockHud`->`EnsureHud`,
    `StopMockHud`->`StopHud`, `RefreshMockHud`->`RefreshHud`,
    `MockHudVisible`->`HudVisible`, `MockHudEnabled`->`HudEnabled`,
    `StartProductionEcSampling`->`StartProductionSampling`,
    `StopProductionEcSampling`->`StopProductionSampling`,
    `ecHudSamplingActive_`->`productionSamplingActive_`,
    `ResumeRecoveryCanRetainPresentMon`->`ResumeRecoveryCanRetainVerifier`,
    `CommittedTargetRelease{Plan,Ops}::stopPresentMon`->`stopRenderVerification`.
  - Dead surface removed: `App::TrackMockGameWindow` (no callers).
  - Obsolete parameter removed: `ShouldSampleProductionTelemetry` lost its
    permanently-false diagnostic bool → `(resolvedShow, suspended)`.
  - `App` facade methods unchanged in shape; only names changed. No controller
    extracted. `HudPresentation` / presentation backend: zero diff.
  - Deviation: the `HudPresentation.cpp` window-class string
    `L"ClawHUD.MockHudSurface"` was intentionally not renamed (runtime value in
    the VRR-critical backend — see §16).
  - CTest: 46/46 pass locally (VS 2022 BuildTools, Ninja, Release). No hardware
    smoke (diff is purely mechanical).

### Next work

Continue with **R1** from §9. After each merged PR, update this progress log with:

```text
PR number
main commit
what state moved
what App methods remain as facade
full CTest result
hardware smoke result if applicable
any deliberate deviation from this plan
```

Before starting R2, R3, or R4 in a new conversation, re-read the corresponding section of
this document and the behavior-inventory template rather than reconstructing decisions from
chat history.
