# Work Order — R6: Extract `DebugObservationController` and Make Debug Sources Truly Lazy

Status: implementation work order  
Prepared from `main` at `bf11b13aeea8da601a6318ec4a5d522608d42e23` after R5 / PR #189  
Scope: R6 of `docs/APP_REFACTOR_PLAN.md`

---

## 1. Decision

R6 should be implemented.

This is not only an `App.cpp` cleanup.

Current `App` owns four debug-only observation sources directly:

```text
WindowsGameIdentitySource
ProcessLifecycleSource
PresentActivitySource
WindowLifecycleSource
```

and owns the developer-only startup flag:

```text
debugLoggingEnabled_
```

Three of the four sources are effectively dormant until their explicit `Start()` call, but `WindowsGameIdentitySource` starts its `std::jthread` in its constructor:

```cpp
WindowsGameIdentitySource::WindowsGameIdentitySource()
    : worker_([this](std::stop_token stop) { WorkerMain(stop); })
{
}
```

Because `App` currently contains `WindowsGameIdentitySource` as an always-live value member, normal `DebugLog=OFF` startup still constructs that debug-only source and starts its worker thread.

R6 therefore has a concrete runtime goal in addition to an ownership cleanup:

> When `[Developer] DebugLog=0`, no debug observation controller and no debug observation source should be constructed at all.

In particular, the `WindowsGameIdentitySource` worker thread must not exist in normal debug-disabled tray/background operation.

---

## 2. Goal

Create:

```text
src/ClawHUD/GameDetection/DebugObservationController.h
src/ClawHUD/GameDetection/DebugObservationController.cpp
```

The controller owns the four existing debug observation sources and their lifecycle:

```text
DebugObservationController
├─ WindowsGameIdentitySource
├─ ProcessLifecycleSource
├─ WindowLifecycleSource
└─ PresentActivitySource
```

`App` conditionally owns the controller:

```cpp
std::unique_ptr<clawhud::DebugObservationController> debugObservation_;
```

Required normal state:

```text
DebugLog OFF
    -> debugObservation_ == nullptr
    -> WindowsGameIdentitySource is not constructed
    -> no WindowsGameIdentitySource worker thread
    -> ProcessLifecycleSource not constructed/started
    -> WindowLifecycleSource not constructed/started
    -> PresentActivitySource not constructed/started
```

Required debug state:

```text
DebugLog ON
    -> create DebugObservationController lazily during Run()
    -> start existing debug sources in the same effective order as today
    -> forward foreground window/PID notifications to the controller
    -> stop the sources in the same effective shutdown position as today
```

This is a debug-observation ownership/laziness refactor only.

Do not redesign the individual observer implementations.

---

## 3. Baseline files to read before editing

Read completely before implementation:

```text
docs/APP_REFACTOR_PLAN.md
docs/APP_REFACTOR_BEHAVIOR_INVENTORY_TEMPLATE.md
docs/HUD_PRESENTATION_REFACTOR_GUARDRAIL.md

src/ClawHUD/App.h
src/ClawHUD/App.cpp

src/ClawHUD/GameDetection/WindowsGameIdentitySource.h
src/ClawHUD/GameDetection/WindowsGameIdentitySource.cpp
src/ClawHUD/GameDetection/ProcessLifecycleSource.h
src/ClawHUD/GameDetection/ProcessLifecycleSource.cpp
src/ClawHUD/GameDetection/WindowLifecycleSource.h
src/ClawHUD/GameDetection/WindowLifecycleSource.cpp
src/ClawHUD/GameDetection/PresentActivitySource.h
src/ClawHUD/GameDetection/PresentActivitySource.cpp
```

Do not implement against an older pre-R5 App layout.

---

## 4. Production vs debug authority — hard boundary

The four sources moved in R6 are **debug observation only**.

They must never become production game-detection authority.

Preserve this separation:

```text
PRODUCTION
    GameSessionController
        -> GenericForegroundTrigger
        -> SteamRunningAppTrigger
        -> MicrosoftGameTrigger
        -> GameDetectionCoordinator
        -> GameRenderVerifier

DEBUG OBSERVATION ONLY
    DebugObservationController
        -> WindowsGameIdentitySource
        -> ProcessLifecycleSource
        -> WindowLifecycleSource
        -> PresentActivitySource
```

Do not feed debug observer output into:

```text
GameDetectionCoordinator
GenericForegroundTrigger
SteamRunningAppTrigger
MicrosoftGameTrigger
GameRenderVerifier
ProductionTelemetryController
HudController
```

Do not use debug evidence to admit, replace, ready, commit, or release a production game candidate.

---

## 5. Shared PresentMon API2 contract

`PresentActivitySource` must continue using the **one App-owned production/shared**:

```text
PresentMonTelemetryProvider
```

The new controller receives that provider by non-owning reference:

```cpp
explicit DebugObservationController(
    PresentMonTelemetryProvider& provider);
```

Recommended member:

```cpp
PresentMonTelemetryProvider& provider_;
```

`PresentActivitySource::Start(provider_)` continues to use that shared provider.

Do NOT construct inside R6:

```text
PresentMonTelemetryProvider
PresentMonApi2Client
new API2 session
PresentMon.exe
```

R6 must not modify provider process tracking/refcount/session behavior.

---

## 6. Keep `debugLoggingEnabled_` App-owned

Do **not** move the developer setting itself into the controller.

Keep:

```cpp
bool debugLoggingEnabled_{};
```

in `App`.

Reason:

```text
HudSettingsStore -> App reads [Developer] DebugLog once at startup
App -> RuntimeLogger::SetDebugLogging(...)
App -> decides whether the optional debug controller should exist
```

That is top-level composition/settings policy.

The controller should not read `settings.ini` or own `HudSettingsStore`.

The controller should not support runtime toggling unless a separate future feature explicitly adds it.

Current contract remains:

```text
[Developer] DebugLog is read once at startup
never written by the app
never toggled at runtime
```

---

## 7. Make the controller itself lazy

This is the most important R6 implementation rule.

Do NOT write:

```cpp
clawhud::DebugObservationController debugObservation_{presentMonTelemetryProvider_};
```

as an always-live App value member.

That would still construct `WindowsGameIdentitySource` and start its worker thread when DebugLog is OFF.

Use conditional ownership:

```cpp
std::unique_ptr<clawhud::DebugObservationController> debugObservation_;
```

When `debugLoggingEnabled_ == false`, the pointer must remain null for the entire run.

---

## 8. Preferred construction point

Do not construct `DebugObservationController` in the `App` constructor merely because the setting is already loaded there.

Preferred creation point is inside `App::Run()` at the current debug-source startup position, after:

```text
tray_.Create
ProductionTelemetryController::Bind
GameSessionController::BindMessageWindow
GameSessionController::StartWindowSource
GameSessionController::StartSteamWatcher
GameSessionController::InitializeSteamSession
```

and before the current debug-source start operations would occur.

Conceptually:

```cpp
if (debugLoggingEnabled_)
{
    debugObservation_ =
        std::make_unique<clawhud::DebugObservationController>(
            presentMonTelemetryProvider_);
    debugObservation_->Start();
}
```

This has two useful properties:

1. DebugLog OFF never constructs any debug observer object.
2. Unsupported-hardware / early startup exits also avoid constructing debug observer resources.

Do not move this creation earlier without a concrete reason.

---

## 9. Preserve current startup ordering

Current relevant order after R5 is:

```text
tray_.Create

productionTelemetry_.Bind

gameSession_.BindMessageWindow
GameSessionController::StartWindowSource
GameSessionController::StartSteamWatcher
GameSessionController::InitializeSteamSession

if DebugLog:
    ProcessLifecycleSource::Start
    WindowLifecycleSource::Start
    PresentActivitySource::Start(shared provider)

RegisterHotKey(F8)

PresentMonTelemetryProvider::Initialize

GameSessionController::StartForegroundTracking

persisted HUD restore / game reevaluation
```

R6 must preserve the effective order.

The new controller's `Start()` should run at the same point occupied by the old three explicit source startup calls.

Important:

`PresentActivitySource::Start(provider)` currently happens **before** `PresentMonTelemetryProvider::Initialize()`.

Do not use R6 as an opportunity to reorder that relationship.

If that order should ever be changed, it requires a separate behavior-focused review/PR.

---

## 10. `DebugObservationController` API

Keep the API very small.

Recommended shape:

```cpp
namespace clawhud
{
class DebugObservationController
{
public:
    explicit DebugObservationController(
        PresentMonTelemetryProvider& provider);
    ~DebugObservationController();

    DebugObservationController(const DebugObservationController&) = delete;
    DebugObservationController& operator=(
        const DebugObservationController&) = delete;

    void Start();
    void OnForegroundChanged(HWND window, DWORD processId) noexcept;
    void Stop() noexcept;

private:
    PresentMonTelemetryProvider& provider_;
    WindowsGameIdentitySource windowsGameIdentitySource_;
    ProcessLifecycleSource processLifecycleSource_;
    PresentActivitySource presentActivitySource_;
    WindowLifecycleSource windowLifecycleSource_;
};
}
```

Exact ordering of private declarations may follow dependency/destruction needs.

Do not add:

```text
IDebugObservationController
IDebugSource
DebugObserverFactory
service locator
generic observer bus
Publish/Subscribe
variant event system
```

There is one concrete debug observation composition.

---

## 11. Controller `Start()` behavior

Move the existing debug startup block from `App::Run()` as close to verbatim as possible.

Preserve independent best-effort startup:

```text
ProcessLifecycleSource::Start
    failure -> Warn
    continue

WindowLifecycleSource::Start
    failure -> Warn
    continue

PresentActivitySource::Start(shared provider)
```

Preserve existing warning text unless moving it requires only namespace/class-context changes.

Current failures are non-fatal.

Do not make `DebugObservationController::Start()` failure abort ClawHUD startup.

A `void Start()` API is acceptable and preferred if it simply retains the existing independent logging/continue behavior.

Do not invent an aggregate success/failure gate unless it provides a concrete use.

---

## 12. `WindowsGameIdentitySource` behavior

Do not modify its internal worker model as part of R6.

It currently starts its worker in its constructor.

That is acceptable **because the containing `DebugObservationController` will now only be constructed when DebugLog is enabled**.

Do not add `Start()` / `Stop()` to `WindowsGameIdentitySource` merely for architectural symmetry.

Do not redesign its queue, worker, COM probing, cache/dedup behavior, or logging.

The R6 resource improvement should come from conditional controller construction, not from rewriting the source itself.

---

## 13. Foreground observation move

Current `App::MakeGameSessionHooks()` foreground callback effectively performs:

```text
ProductionTelemetryController::OnForegroundProcessChanged(pid)
ReconcileHudVisibility()

if DebugLog:
    WindowsGameIdentitySource::QueueInspect(window, pid)
    PresentActivitySource::Watch(pid)
```

After R6 keep the production reactions in `App` and collapse only the debug tail:

```cpp
hooks.onForegroundChanged = [this](HWND window, DWORD processId)
{
    productionTelemetry_.OnForegroundProcessChanged(processId);
    ReconcileHudVisibility();

    if (debugObservation_)
        debugObservation_->OnForegroundChanged(window, processId);
};
```

`DebugObservationController::OnForegroundChanged()` should preserve exactly:

```text
WindowsGameIdentitySource::QueueInspect(window, processId)
PresentActivitySource::Watch(processId)
```

in that order.

Do not move the production telemetry call or HUD visibility reconcile into the debug controller.

Do not give `DebugObservationController` an `App&`, `HudController&`, `ProductionTelemetryController&`, or `GameSessionController&`.

---

## 14. `PresentActivitySource` remains debug-only

Preserve its current role:

```text
foreground/candidate PID observation for debug logging
shared PresentMon API2 lease
periodic debug frame logging
```

It must not become:

```text
FPS authority
renderer-proof authority
game detector
game candidate selector
HUD telemetry provider
```

Do not alter production `GameRenderVerifier` or production FPS paths in R6.

---

## 15. Stop behavior and ordering

Current `App::~App()` / `App::Exit()` stop the debug sources after:

```text
StopProductionSampling(...)
ProductionTelemetryController::StopGraphicsApiProbe
GameSessionController::StopSources
```

and then call, in order:

```text
WindowLifecycleSource::Stop
PresentActivitySource::Stop
ProcessLifecycleSource::Stop
```

Preserve that effective order inside:

```cpp
DebugObservationController::Stop()
```

Recommended:

```cpp
void DebugObservationController::Stop() noexcept
{
    windowLifecycleSource_.Stop();
    presentActivitySource_.Stop();
    processLifecycleSource_.Stop();
}
```

Do not add production source shutdown into this method.

Do not stop `GameSessionController`, telemetry, or HUD presentation from the debug controller.

---

## 16. Controller destruction

`DebugObservationController` destructor should ensure owned active sources are stopped safely.

A simple pattern is acceptable:

```cpp
DebugObservationController::~DebugObservationController()
{
    Stop();
}
```

provided the underlying source `Stop()` methods are already idempotent as relied upon by current App shutdown paths.

`WindowsGameIdentitySource` has no explicit `Stop()`; its existing destructor continues to request worker stop and its `std::jthread` member provides join-on-destruction semantics.

Do not change that source's destruction contract in R6.

---

## 17. App shutdown integration

Replace the three direct debug stop calls in both:

```text
App::~App()
App::Exit()
```

with:

```cpp
if (debugObservation_)
    debugObservation_->Stop();
```

at the same effective shutdown position.

Do not centralize the whole App shutdown sequence in R6.

That belongs to R7.

Do not mix `Exit()` / destructor deduplication into this PR.

Also do not manually reset the pointer in one path but not the other solely to make R6 look cleaner.

The controller object may remain alive until normal App object destruction after `Stop()`; the debug source shutdown behavior should remain equivalent.

---

## 18. App header cleanup

After R6, `App.h` should no longer include these four debug-source headers directly:

```text
GameDetection/WindowsGameIdentitySource.h
GameDetection/ProcessLifecycleSource.h
GameDetection/PresentActivitySource.h
GameDetection/WindowLifecycleSource.h
```

Prefer a forward declaration:

```cpp
namespace clawhud
{
class DebugObservationController;
}
```

and retain:

```cpp
std::unique_ptr<clawhud::DebugObservationController> debugObservation_;
```

Because `App::~App()` is already defined out-of-line in `App.cpp`, `unique_ptr` to an incomplete controller type is appropriate.

`App.cpp` should include:

```cpp
#include "GameDetection/DebugObservationController.h"
```

Remove obsolete debug-source member declarations from App.

---

## 19. Expected App ownership after R6

`App` should retain approximately:

```text
HudSettingsStore
TrayIcon
HudController
PresentMonTelemetryProvider
ProductionTelemetryController
GameSessionController
optional DebugObservationController (unique_ptr)
SettingsWindow (lazy unique_ptr)
TweakStartupCoordinator
start/debug/tweak top-level settings
suspend/resume top-level state
single-instance/update/hardware/message-loop shell
```

That is the intended pre-R7 composition root.

---

## 20. Tray-only / Settings UI contract — hard requirement

R6 must preserve the existing tray-only lazy Settings UI invariant.

Required:

```text
Windows startup
    -> tray/background runtime
    -> NO SettingsWindow construction
```

`SettingsWindow` remains:

```cpp
std::unique_ptr<SettingsWindow> settings_;
```

and is created only by explicit user action through `App::OpenSettings()`.

`DebugObservationController` must not:

```text
construct SettingsWindow
own SettingsWindow
open Settings
preload Settings controls/tabs
```

`App::SettingsDestroyed()` must continue to release Settings UI ownership.

---

## 21. Debug-disabled runtime-resource acceptance criterion

This is a concrete R6 acceptance criterion, not just an implementation detail.

For:

```text
[Developer]
DebugLog=0
```

there must be no active production object instance of:

```text
DebugObservationController
WindowsGameIdentitySource
ProcessLifecycleSource
WindowLifecycleSource
PresentActivitySource
```

created by `App`.

Therefore there must be no debug-only:

```text
WindowsGameIdentitySource jthread
ProcessLifecycle WMI subscription/worker
WindowLifecycle WinEvent debug hook/worker
PresentActivity polling worker
```

caused by those debug observer sources.

Do not create the controller and merely skip `Start()`; that still violates the WindowsGameIdentitySource worker requirement.

---

## 22. Debug-enabled behavior acceptance criterion

For:

```text
[Developer]
DebugLog=1
```

preserve current observer behavior:

```text
ProcessLifecycle source starts best-effort
WindowLifecycle source starts best-effort
PresentActivity source starts against shared provider
WindowsGameIdentity worker exists because controller exists
foreground changes queue Windows identity inspection
foreground changes update PresentActivity watch PID
existing debug logs remain available
```

Failure of one debug source must not disable production game detection or HUD startup.

---

## 23. Production game detection unchanged

R6 must not alter:

```text
Idle / Armed / Verifying / Ready / Committed
candidate precedence
generation isolation
Steam RunningAppID semantics
Microsoft production identity semantics
FirstDisplayedFrame renderer evidence
foreground commit rule
Alt+Tab retention
process-lifetime release
```

`MicrosoftGameTrigger` remains in `GameSessionController`.

Do not confuse it with the debug-only `WindowsGameIdentitySource`.

No production game-session code should depend on `debugObservation_`.

---

## 24. Suspend/resume unchanged

R5 is complete.

Do not modify:

```text
suspended_
resumeRecoveryActive_
resumeRecoveryAttempts_
HandleSystemSuspend
HandleSystemResume
TryResumeRecovery
CancelResumeRecovery
PauseProductionSamplingForSuspend
SuspendResumePolicy.h
```

Debug observers are not suspend/resume authorities.

Do not add debug observer lifecycle hooks into the resume state machine.

---

## 25. HUD Presentation / VRR hard gate

R6 should have no reason to touch the production HUD presentation path.

Do not modify, weaken, replace, or work around:

```text
HudPresentation windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
existing WS_EX_LAYERED behavior
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
ProductionHudPresentationContract()
Presentation API production path
DirectComposition production path
independent-flip requirement
premultiplied-alpha contract
```

Expected:

```text
HudPresentation.cpp                  ZERO DIFF
HudPresentation.h                    ZERO DIFF
HudPresentationContract.*            ZERO DIFF
HudPresentationLifecycle.*           ZERO DIFF
HudRenderer.*                        ZERO DIFF
HudController.*                      ZERO DIFF
```

If R6 appears to require any presentation change:

```text
STOP.
```

Fix the debug-controller boundary instead.

---

## 26. Background opacity

R6 contains no opacity work.

Do not touch window-wide or visual-wide opacity.

Background opacity remains background-only under the existing HUD contract.

---

## 27. Do not redesign observer implementations

Do not modify internal algorithms in:

```text
WindowsGameIdentitySource
ProcessLifecycleSource
WindowLifecycleSource
PresentActivitySource
```

unless a compile-only ownership adjustment is strictly necessary.

In particular, do not change:

```text
Windows identity probing semantics
WMI subscription model
WindowLifecycle queue/cache capacity
WinEvent selection/filtering
PresentActivity polling cadence
PresentActivity debug formatting
source logging formats
thread synchronization
```

R6 composes them; it does not rewrite them.

Normal expectation is zero behavioral diff in all four source implementation files.

---

## 28. No standalone diagnostic integration

Do not integrate archived/future standalone diagnostic designs into R6.

No:

```text
PDH Top GPU scan
broad diagnostic process discovery
new global polling
GameDetectionProbe revival
standalone Diag IPC
production-to-Diag coupling
```

The controller exists only to own current in-process debug logging sources when developer DebugLog is enabled.

---

## 29. No generic event bus or DI framework

Do not add:

```text
DebugEventBus
ObserverRegistry
IObservationSource
IDebugSource
service container
runtime service graph
```

`App` may call one concrete:

```text
DebugObservationController::OnForegroundChanged(HWND, DWORD)
```

That is sufficient.

---

## 30. CMake

Add:

```text
src/ClawHUD/GameDetection/DebugObservationController.cpp
```

to the ClawHUD production target.

Do not reorganize the large test CMake declarations in R6.

R8 remains the optional CMake cleanup phase.

---

## 31. Tests

No fake Win32/WMI/PresentMon universe should be created solely to unit-test this composition wrapper.

The important behavior is enforced structurally:

```text
DebugLog OFF -> unique_ptr never constructed
DebugLog ON  -> controller composes existing already-tested sources
```

Run at minimum the existing tests covering the component boundaries:

```text
WindowsGameIdentitySource tests
ProcessLifecycleSource tests
WindowLifecycleSource tests
PresentActivitySource / PresentMon debug-frame tests where present
PresentMonTelemetryProviderTests
ProductionGameDetectionScenarioTests
GameRenderVerifierTests
HudPresentationContractTests
HudPresentationLifecycleTests
SuspendResumeRecoveryTests
```

Then run the full active CTest suite.

Current baseline at work-order creation:

```text
46/46
```

Use the actual count at implementation time.

Do not delete or weaken existing tests.

A new dedicated controller unit-test target is optional, not required, unless implementation introduces new pure logic worth testing.

---

## 32. Build verification

Required for runtime changes:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Use the repository's established BuildTools/Ninja command when appropriate.

Required result:

```text
Release build succeeds
full active CTest passes
```

---

## 33. Hardware/runtime verification policy

Hardware smoke is useful but is **not a merge blocker by itself**.

If supported MSI Claw hardware is unavailable, record:

```text
Hardware smoke: deferred / not performed
```

Do not mark a clean R6 PR `수정필요` solely for deferred hardware validation.

Blocking issues remain actual code defects, test/build failures, ownership regressions, production detector regressions, presentation-contract violations, or lazy-UI regressions.

---

## 34. Useful later runtime smoke

When convenient on hardware, verify both settings:

### DebugLog OFF

```text
normal tray/background startup
HUD/game detection works normally
no debug observer logs
no debug-only WindowsGameIdentity worker created by App
Settings remains lazy
```

### DebugLog ON

```text
debug lifecycle logs still appear
foreground identity logs still appear
PresentActivity logs still appear
game detection remains production-authoritative
HUD behavior unchanged
```

This is follow-up validation, not an automatic merge gate.

---

## 35. Behavior inventory for the PR

The PR description must record at least these old -> new mappings:

### App startup debug block

Old:

```text
if DebugLog:
    processLifecycleSource_.Start()
    windowLifecycleSource_.Start()
    presentActivitySource_.Start(shared provider)
```

New:

```text
if DebugLog:
    construct DebugObservationController(shared provider)
    DebugObservationController::Start()
```

Record exact source startup order and warning behavior.

### Foreground callback

Old:

```text
if DebugLog:
    windowsGameIdentitySource_.QueueInspect(window, pid)
    presentActivitySource_.Watch(pid)
```

New:

```text
if debugObservation_:
    debugObservation_->OnForegroundChanged(window, pid)
```

Record internal operation order.

### Shutdown

Old:

```text
windowLifecycleSource_.Stop()
presentActivitySource_.Stop()
processLifecycleSource_.Stop()
```

New:

```text
debugObservation_->Stop()
```

Record internal stop order.

### Laziness change

Explicitly document the intentional runtime improvement:

```text
DebugLog OFF previously still constructed WindowsGameIdentitySource and started
its constructor-owned jthread because it was an App value member.

After R6 the entire DebugObservationController is absent, so the worker is not
created at all.
```

This is the one intentional behavior/resource change in R6.

---

## 36. Expected changed files

Approximately:

```text
CMakeLists.txt
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/GameDetection/DebugObservationController.h
src/ClawHUD/GameDetection/DebugObservationController.cpp
docs/APP_REFACTOR_PLAN.md
```

Potentially no changes should be needed in the four owned source implementations.

Unexpected changes to game detector, telemetry, HUD renderer/presentation, suspend/resume, settings UI, or observer algorithms require explicit justification and should generally be split out.

---

## 37. Development-stage cleanup policy

ClawHUD is unreleased.

After all call sites migrate, remove the old direct App members completely:

```text
windowsGameIdentitySource_
processLifecycleSource_
presentActivitySource_
windowLifecycleSource_
```

Do not leave compatibility aliases or duplicate ownership.

There must be one owner:

```text
DebugObservationController
```

when debug observation is enabled, and no owner/object at all when it is disabled.

---

## 38. Documentation

Update:

```text
docs/APP_REFACTOR_PLAN.md
```

with the R6 result.

Record:

```text
PR number
head / merge commit
DebugObservationController created
four debug sources moved out of App
conditional unique_ptr ownership
DebugLog OFF creates no debug observer sources
WindowsGameIdentitySource worker therefore absent when DebugLog OFF
shared PresentMon provider remains App-owned
startup/foreground/shutdown mapping
CTest result
hardware smoke performed/deferred
```

After successful R6 set:

```text
Next: R7 — Final App shell cleanup
```

---

## 39. Expected end state

```text
App
│
├─ HudSettingsStore
├─ TrayIcon
├─ HudController
├─ PresentMonTelemetryProvider
├─ ProductionTelemetryController
├─ GameSessionController
│
├─ optional unique_ptr<DebugObservationController>
│    ├─ WindowsGameIdentitySource
│    ├─ ProcessLifecycleSource
│    ├─ WindowLifecycleSource
│    └─ PresentActivitySource
│
├─ SettingsWindow (lazy unique_ptr)
├─ TweakStartupCoordinator
├─ suspend/resume top-level orchestration
└─ app shell / update / hardware / persistence / message loop
```

Normal production startup with DebugLog disabled:

```text
App
  -> no DebugObservationController
  -> no debug source objects
  -> no debug-only WindowsGameIdentity worker
```

---

## 40. Acceptance criteria

R6 is complete only when all are true:

1. `DebugObservationController.{h,cpp}` exists.
2. It owns `WindowsGameIdentitySource`.
3. It owns `ProcessLifecycleSource`.
4. It owns `WindowLifecycleSource`.
5. It owns `PresentActivitySource`.
6. App owns it through `std::unique_ptr`, not as an always-live value.
7. `debugLoggingEnabled_` remains App-owned.
8. DebugLog OFF leaves `debugObservation_ == nullptr`.
9. DebugLog OFF therefore does not construct `WindowsGameIdentitySource`.
10. DebugLog OFF therefore does not start its constructor-owned worker thread.
11. DebugLog OFF does not construct/start the other three debug sources through App.
12. DebugLog ON constructs the controller during `Run()` at the old debug startup position.
13. debug source startup order remains ProcessLifecycle -> WindowLifecycle -> PresentActivity.
14. debug source startup failures remain non-fatal and logged as before.
15. foreground observation order remains WindowsGameIdentity QueueInspect -> PresentActivity Watch.
16. production telemetry foreground update remains in App and occurs before the debug observation tail as today.
17. HUD visibility reconcile remains in App.
18. controller shutdown order remains WindowLifecycle -> PresentActivity -> ProcessLifecycle.
19. WindowsGameIdentitySource internal implementation is not redesigned.
20. debug observers remain observation-only and never influence production detection.
21. `MicrosoftGameTrigger` remains production-owned by GameSessionController.
22. one shared App-owned PresentMon provider remains.
23. no second API2 session/client exists.
24. PresentActivitySource continues using the shared provider.
25. App no longer directly owns the four debug source members.
26. App.h no longer directly includes the four debug source headers unless technically unavoidable.
27. no SettingsWindow eager creation is introduced.
28. tray-only lazy Settings UI contract remains unchanged.
29. suspend/resume R5 architecture remains unchanged.
30. game detection state/policy remains unchanged.
31. HudController/production presentation behavior remains unchanged.
32. `HudPresentation.*` contract files have zero behavioral diff.
33. no HUD window style / activation / hit-test / independent-flip / premultiplied-alpha change exists.
34. no opacity change exists.
35. no standalone diagnostic integration exists.
36. no generic event bus/DI/interface framework is introduced.
37. Release build succeeds.
38. full active CTest suite passes.
39. PR behavior inventory records the intentional DebugLog-OFF resource improvement.
40. hardware smoke may be deferred and is not itself a merge blocker.
41. `APP_REFACTOR_PLAN.md` is updated to point to R7.
