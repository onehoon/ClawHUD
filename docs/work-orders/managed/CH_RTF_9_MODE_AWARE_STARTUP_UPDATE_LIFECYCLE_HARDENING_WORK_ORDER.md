# CH-RTF-9 — Mode-Aware Startup, Update, and Lifecycle Hardening Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** ClawHUD Runtime / Frontend Separation  
> **Architecture source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md`  
> **PR plan source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_PR_PLAN_2026-09-02.md`  
> **Previous PRs:** #209 CH-RTF-1 through #216 CH-RTF-8  
> **Analyzed main HEAD:** `bbad4f6ec318da93e105eeca593d74349e33fc4f`  
> **Pinned VeloPack:** `1.2.0`  
> **Scope:** Harden the already-existing Standalone / Managed launch modes around startup registration, update apply/restart behavior, post-update PresentMon runtime bootstrap, and one-runtime lifecycle invariants  
> **Status:** Ready for implementation

---

## 1. Objective

CH-RTF-8 introduced the real process launch modes:

```text
ClawHUD.exe
    -> Standalone
    -> runtime + Control IPC + tray + legacy Settings

ClawHUD.exe --managed
    -> Managed
    -> same runtime + same Control IPC
    -> no tray
    -> no legacy Settings
```

The runtime itself is already shared correctly. CH-RTF-9 must now remove the remaining lifecycle behaviors that still assume every process is Standalone.

There are two concrete production issues to fix:

```text
1. App::Run() always reconciles the Standalone startup shortcut.
   A Managed process must not silently become the owner of that shortcut.

2. CheckForUpdates() always asks VeloPack to restart ClawHUD after applying.
   Managed restart currently has no restart arguments, therefore the restarted
   process would resolve to Standalone and break the Managed ownership contract.
```

The target lifecycle is:

```text
Standalone process
    -> owns/reconciles the normal ClawHUD startup shortcut
    -> VeloPack update may restart ClawHUD normally

Managed process
    -> does not reconcile the Standalone shortcut just because it was launched
    -> VeloPack update applies and exits WITHOUT restarting ClawHUD
    -> the surviving external owner is responsible for launching the updated
       ClawHUD.exe --managed again
```

This PR remains entirely on the ClawHUD side. It does not implement SteamAddon process ownership.

---

## 2. Current baseline after CH-RTF-8

### 2.1 Launch mode is already explicit and immutable

Current process state is represented by:

```cpp
enum class LaunchMode
{
    Standalone,
    Managed,
};
```

and `App` stores:

```cpp
const clawhud::LaunchMode launchMode_;
```

Do not add another lifecycle mode or persist this value.

### 2.2 Single instance is already cross-mode

The existing mutex remains:

```text
Local\ClawHUD.SingleInstance
```

Both modes use it.

Current intended behavior is already correct:

```text
Standalone running + second Managed launch
    -> second process exits

Managed running + second Standalone launch
    -> second process exits

Managed running + second Managed launch
    -> second process exits
```

Do not create mode-specific mutex names.

### 2.3 Startup registration is still unconditional

Current `App::Run()` executes:

```cpp
if (!ApplyStartupRegistration())
    ...
```

for both launch modes.

`ApplyStartupRegistration()` creates/removes the ordinary startup shortcut based on the shared persisted `startWithWindows_` preference.

The shortcut launches the executable normally and does not contain `--managed`.

### 2.4 Explicit Start-with-Windows mutation already exists

`IRuntimeControl::SetStartWithWindows(bool)` and protocol-v1 `SetStartWithWindows` are already externally available.

Current semantic behavior is:

```text
requested value
-> update startWithWindows_
-> ApplyStartupRegistration()
-> rollback in-memory value if registration fails
-> persist authoritative value
```

That remains a useful explicit frontend control and must not be removed merely because Managed mode now exists.

### 2.5 Update flow is currently mode-blind

Current `CheckForUpdates()` has both of these paths:

```cpp
manager.WaitExitThenApplyUpdates(*pending, true, true);
std::exit(0);
```

and:

```cpp
manager.DownloadUpdates(*update);
manager.WaitExitThenApplyUpdates(*update, true, true);
std::exit(0);
```

The third argument is `restart=true`.

The pinned VeloPack 1.2.0 C++ API is:

```cpp
void WaitExitThenApplyUpdates(
    const VelopackAsset& asset,
    bool silent = false,
    bool restart = true,
    std::vector<std::string> restartArgs = {});
```

VeloPack documents `restart=true` as restarting the application after apply, and `restartArgs` as the arguments passed to that restarted application.

Therefore the current Managed behavior is unsafe:

```text
ClawHUD.exe --managed
-> update available
-> restart=true, restartArgs={}
-> updater launches ClawHUD.exe with no --managed
-> new process resolves Standalone
-> tray appears / owner contract is lost
```

This is a real lifecycle regression and is the main CH-RTF-9 update fix.

### 2.6 PresentMon bootstrap currently depends on VeloPack restart hooks

`main.cpp` currently registers:

```text
OnFirstRun  -> EnsurePresentMonRuntime()
OnRestarted -> EnsurePresentMonRuntime()
```

VeloPack's documented `OnRestarted` hook runs when the app is restarted by VeloPack after an update.

Managed mode will intentionally use `restart=false`, so a later owner-driven normal `ClawHUD.exe --managed` launch is not a VeloPack restart and cannot rely on `OnRestarted` to run the PresentMon runtime check.

This must be addressed in the same PR so Managed update does not create a future PresentMon-runtime version/bootstrap hole.

---

## 3. Non-negotiable architecture rules

1. `ClawHUD.exe` remains permanently Standalone by default.
2. `--managed` remains the only way to enter Managed mode.
3. ClawHUD must not detect SteamAddon installation/running state.
4. Launch mode remains process-lifetime state and is never persisted.
5. There is still exactly one ClawHUD runtime implementation.
6. There is still exactly one ClawHUD runtime per user session.
7. Do not add parent-process monitoring, owner heartbeat, Job Object ownership, restart supervision, or crash-loop recovery to ClawHUD.
8. Do not introduce mode branches in renderer, HUD presentation, telemetry, game detection, EC, suspend/resume, tweaks, or Control IPC transport.
9. Do not change the Control pipe name or protocol version.
10. Do not create a generic lifecycle/state-machine framework for two simple mode decisions.
11. `RequestShutdown` continues to use the CH-RTF-7 response-before-exit path.
12. All HUD / VRR production presentation invariants remain untouched.

---

## 4. Startup-registration ownership

### 4.1 Automatic launch-time reconciliation

Change only the automatic `App::Run()` reconciliation policy.

Required behavior:

```text
Standalone
    -> preserve current ApplyStartupRegistration() call

Managed
    -> skip automatic ApplyStartupRegistration()
    -> do not create the Standalone shortcut
    -> do not delete the Standalone shortcut
    -> do not rewrite the shortcut
    -> do not change startWithWindows_
```

Conceptually:

```cpp
if (ShouldReconcileStartupRegistration(launchMode_))
{
    if (!ApplyStartupRegistration())
        Log(...);
}
```

A direct mode comparison is also acceptable if it remains clear and testable.

Important examples:

```text
stored preference = true
startup shortcut missing
launch --managed
    -> shortcut remains missing

stored preference = false
stale startup shortcut exists
launch --managed
    -> shortcut remains untouched

later normal Standalone launch
    -> existing Standalone reconciliation policy runs
```

Managed launch is not responsible for repairing Standalone shell state.

### 4.2 Explicit `SetStartWithWindows` remains functional

Do **not** mode-gate the explicit runtime-control mutation.

An external frontend controlling a Managed runtime may intentionally change the user's Standalone startup preference.

Required behavior remains:

```text
Managed IPC: SetStartWithWindows(true)
    -> create/update normal Standalone ClawHUD startup shortcut
    -> shortcut launches ClawHUD.exe without --managed
    -> persist startWithWindows=true

Managed IPC: SetStartWithWindows(false)
    -> remove normal Standalone startup shortcut
    -> persist startWithWindows=false
```

This is different from automatic launch-time reconciliation:

```text
Managed process started
    -> no implicit shortcut ownership

User explicitly changes Start with Windows
    -> honor the explicit setting request
```

Do not add a separate `ManagedStartWithWindows` setting.

Do not add `--managed` to the startup shortcut.

The existing setting remains explicitly **Standalone-oriented**.

---

## 5. Add a tiny mode-aware lifecycle policy seam

Prefer one small pure policy seam for the decisions that need deterministic tests.

Recommended shape, conceptually:

```cpp
constexpr bool ShouldReconcileStartupRegistration(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Standalone;
}

constexpr bool ShouldRestartAfterVelopackUpdate(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Standalone;
}
```

Possible locations:

```text
src/ClawHUD/LaunchMode.h
```

if kept very small, or:

```text
src/ClawHUD/RuntimeLifecyclePolicy.h
```

if separating policy reads more clearly.

Do not create:

```text
LifecycleManager
ModeStateMachine
UpdateCoordinator abstraction
strategy hierarchy
DI service
```

The policy is two mode decisions, not a framework.

---

## 6. Mode-aware VeloPack update behavior

### 6.1 Standalone

Preserve current behavior:

```text
pending downloaded update
    -> WaitExitThenApplyUpdates(..., silent=true, restart=true)
    -> current process exits
    -> updater applies package
    -> updater restarts ClawHUD normally
    -> restarted process is Standalone
```

and:

```text
new update found
    -> DownloadUpdates
    -> WaitExitThenApplyUpdates(..., silent=true, restart=true)
    -> same behavior
```

No `--managed` argument should be involved.

### 6.2 Managed

Required behavior for **both** pending-update and newly-downloaded-update paths:

```text
Managed runtime
    -> WaitExitThenApplyUpdates(..., silent=true, restart=false)
    -> current process exits
    -> updater applies package
    -> updater does NOT launch ClawHUD
```

Then, in the eventual SteamAddon ownership implementation:

```text
Addon survives
-> observes owned Managed runtime exit
-> waits for update/apply transition
-> launches installed ClawHUD.exe --managed
-> reconnects IPC
```

That owner-side behavior does not belong in this PR.

### 6.3 Do not restart Managed with `restartArgs={"--managed"}`

Although VeloPack supports restart arguments, do not solve Managed mode by doing:

```cpp
restart = true;
restartArgs = {"--managed"};
```

The settled ownership contract is that the external owner is responsible for launching/relaunching Managed ClawHUD.

A VeloPack-created replacement process would bypass that owner-controlled launch point and complicate future Job Object / ownership semantics.

Use:

```text
Managed -> restart=false
```

### 6.4 Keep current update error policy

Do not redesign update UX in this PR.

Preserve the current behavior where update exceptions are logged and ClawHUD continues with the installed version.

Keep:

```cpp
.SetAutoApplyOnStartup(false)
```

unchanged.

Do not add update retry loops, background workers, or new dialogs.

---

## 7. Decouple PresentMon runtime bootstrap from VeloPack-owned restart

### 7.1 Problem

Managed update now intentionally does:

```text
apply update
-> no VeloPack restart
-> later external owner performs a normal --managed launch
```

VeloPack's `OnRestarted` hook is specifically for a process restarted by VeloPack.

Therefore the PresentMon runtime readiness/bootstrap must not depend solely on that hook.

### 7.2 Required outcome

Every normal ClawHUD application launch must have a reliable opportunity to validate the pinned/required PresentMon runtime before `App::Run()` begins production initialization.

The simplest preferred implementation is:

```cpp
int WINAPI wWinMain(...)
{
    Velopack::VelopackApp::Build()
        .SetAutoApplyOnStartup(false)
        .OnBeforeUninstall(...)
        .Run();

    // Reached only for normal app execution after Velopack has handled any
    // fast-exit lifecycle hook.
    clawhud::EnsurePresentMonRuntime();

    ... resolve DPI / launch mode / App ...
}
```

and remove the redundant `EnsurePresentMonRuntime()` dependency from:

```text
OnFirstRun
OnRestarted
```

if the normal-launch call fully replaces those paths.

Why this is acceptable:

`EnsurePresentMonRuntime()` already begins with `IsPresentMonRuntimeReady()` and returns `AlreadyReady` without launching MSI when the service, registry path, middleware file/name, and API version are valid.

The expensive/elevated MSI path is reached only when runtime readiness is actually missing/incompatible.

This also makes recovery better if the runtime was removed or became incompatible independently of a ClawHUD update.

### 7.3 VeloPack Run stays first

VeloPack documentation requires `VelopackApp.Build().Run()` to be the first application lifecycle code because update/install/uninstall fast-exit invocations may terminate inside `Run()`.

Do not move normal ClawHUD initialization before it.

The intended order is:

```text
VelopackApp::Run
-> normal-launch PresentMon readiness/bootstrap
-> DPI context
-> command-line launch-mode resolution
-> App
```

If implementation proves a smaller equally correct mechanism, it is acceptable, but it must satisfy:

```text
Managed restart=false update
-> later ordinary --managed launch
-> PresentMon runtime compatibility is checked
```

Do not run MSI installation from a VeloPack short-timeout fast callback.

---

## 8. Single-instance lifecycle hardening

No new single-instance feature is required.

Preserve:

```text
Local\ClawHUD.SingleInstance
```

and existing behavior where the loser exits.

Required invariants:

```text
Standalone wins first
+ Managed launch
    -> Managed attempt exits
    -> existing Standalone remains Standalone

Managed wins first
+ Standalone launch
    -> Standalone attempt exits
    -> existing Managed remains Managed

Managed wins first
+ Managed launch
    -> second Managed attempt exits
```

Do not:

- forward activation to the first process;
- ask the first process to switch modes;
- create per-mode mutexes;
- automatically kill Standalone when `--managed` loses the race;
- automatically relaunch Managed from ClawHUD itself.

The future SteamAddon owner performs mode convergence through:

```text
GetRuntimeInfo
-> RequestShutdown if existing mode is Standalone
-> wait for process/IPC disappearance
-> launch --managed
```

### Near-simultaneous startup

If Windows launches ordinary ClawHUD from its startup shortcut while SteamAddon is also trying to launch Managed:

```text
mutex winner is temporary
```

The ClawHUD side does not need a race state machine.

The Addon side later reconciles to Managed when Integration is ON.

Do not add complexity here to predict which process wins scheduling.

---

## 9. Runtime info and shutdown behavior

### Runtime info

CH-RTF-8 already reports the real mode through protocol v1:

```text
GetRuntimeInfo.launchMode = Standalone | Managed
```

Preserve it.

No new fields or protocol version bump are required.

### Shutdown

Reuse the existing idempotent path:

```cpp
void App::Exit()
{
    if (exiting_) return;
    ...
}
```

Do not create:

```text
ExitManaged
ExitForUpdate
ExitForOwner
```

Update scheduling still happens before `std::exit(0)` in the existing early update path; do not mix the runtime-control RequestShutdown response lifecycle into VeloPack update apply.

---

## 10. Logging

Add enough mode-aware logging to make lifecycle validation unambiguous.

Recommended examples:

```text
Startup registration reconciliation skipped launchMode=Managed
Velopack pending update apply launchMode=Standalone restart=1
Velopack pending update apply launchMode=Managed restart=0
Velopack downloaded update apply launchMode=Managed restart=0
```

Keep logs concise.

Do not log IPC payload contents, user SID, or sensitive paths solely for this work.

---

## 11. Primary code areas

Expected production files:

```text
src/ClawHUD/main.cpp
src/ClawHUD/App.cpp
src/ClawHUD/App.h                 only if needed
src/ClawHUD/LaunchMode.h/.cpp    if lifecycle policy stays there
```

Possible tiny new policy file:

```text
src/ClawHUD/RuntimeLifecyclePolicy.h
```

Tests:

```text
tests/LaunchModeTests.cpp
```

or one equally small lifecycle-policy test target.

Do not touch HUD presentation code.

Do not touch PresentMon telemetry acquisition code; only bootstrap invocation timing may change.

---

## 12. Required automated tests

### 12.1 Startup reconciliation policy

At minimum:

```text
Standalone -> reconcile startup registration = true
Managed    -> reconcile startup registration = false
```

### 12.2 Update restart policy

At minimum:

```text
Standalone -> VeloPack restart after apply = true
Managed    -> VeloPack restart after apply = false
```

Test the same policy for both conceptual update sources:

```text
UpdatePendingRestart()
CheckForUpdates() + DownloadUpdates()
```

The production method should use one policy decision rather than accidentally handling the two branches differently.

### 12.3 Existing launch-mode tests

Keep all CH-RTF-8 parser tests passing:

```text
no args -> Standalone
exact --managed -> Managed
unknown args do not enable Managed
case-sensitive exact token
explicit wire mapping
```

### 12.4 Existing control tests

Keep CH-RTF-4 through CH-RTF-7 tests unchanged/passing:

```text
wire codec
main-thread dispatch
secure pipe
mutations
opacity preview/commit
RequestShutdown response-before-exit
```

### 12.5 PresentMon bootstrap unit coverage

Existing pure readiness / MSI-exit classification tests must remain passing.

If normal-launch bootstrap timing is factored through a small helper, test the helper rather than mocking the whole VeloPack runtime.

Do not introduce a large UpdateManager abstraction solely for unit tests.

---

## 13. Installed-build / manual validation matrix

Because real VeloPack update behavior and Windows shortcut effects require an installed build, perform or document the following validation where practical.

### 13.1 Startup shortcut ownership

```text
A. Standalone, StartWithWindows=true
   -> normal shortcut exists / is reconciled

B. launch --managed
   -> shortcut target/arguments are not rewritten by launch-time reconciliation

C. Managed IPC SetStartWithWindows(false)
   -> shortcut is removed intentionally

D. Managed IPC SetStartWithWindows(true)
   -> ordinary shortcut is created
   -> shortcut arguments do NOT contain --managed
```

### 13.2 Cross-mode single instance

```text
Managed running -> launch normal ClawHUD.exe
    -> second process exits
    -> no tray appears
    -> GetRuntimeInfo still reports Managed

Standalone running -> launch ClawHUD.exe --managed
    -> second process exits
    -> existing Standalone remains
```

### 13.3 Standalone update

With an installed test build and a newer package available:

```text
Standalone detects/downloads update
-> updater applies
-> ClawHUD restarts automatically
-> restarted process has tray
-> GetRuntimeInfo = Standalone
-> PresentMon runtime readiness checked
```

### 13.4 Managed update

With an installed test build and newer package available:

```text
ClawHUD.exe --managed
-> detects/downloads update
-> updater applies
-> ClawHUD does NOT automatically restart
```

Then manually simulate the future owner:

```text
launch updated ClawHUD.exe --managed
-> no tray
-> GetRuntimeInfo = Managed
-> updated version reported
-> PresentMon runtime readiness checked
```

If the required PresentMon runtime version/package is intentionally made incompatible/missing for validation, the normal post-update Managed launch must execute the existing bootstrap path rather than silently starting with an incompatible runtime.

---

## 14. Out of scope

Do not implement any SteamAddon-side behavior in this PR.

Specifically deferred:

```text
SteamAddon installation discovery
SteamAddon IPC client
Integration Enabled persistence
Standalone -> Managed owner transition
Job Object ownership / KILL_ON_JOB_CLOSE
Managed crash bounded restart
Addon update/restart ownership
Addon uninstall coordination
ClawHUD install/uninstall coordination from Addon
IPC reconnect policy
```

Also out of scope:

```text
new standalone frontend
WinUI3 migration
legacy Settings removal
Control protocol v2
StateChanged event bus
multiple pipe instances / concurrent clients
EC helper sharing
PresentMon architecture redesign
HUD renderer/presentation changes
game-detection changes
```

---

## 15. HUD / VRR safety contract — mandatory regression boundary

This PR must not modify, replace, weaken, or work around:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- current `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirement;
- production Presentation API / DirectComposition path;
- premultiplied-alpha presentation contract.

There is no legitimate reason for CH-RTF-9 to touch those files or contracts.

---

## 16. Completion criteria

CH-RTF-9 is complete when all of the following are true:

1. Standalone launch still reconciles the normal startup shortcut.
2. Managed launch no longer automatically creates/deletes/rewrites that shortcut.
3. Explicit `SetStartWithWindows` remains available in Managed and continues to control the normal Standalone shortcut.
4. The normal startup shortcut never contains `--managed`.
5. Standalone VeloPack update still applies and restarts automatically.
6. Managed VeloPack update applies with `restart=false` and does not auto-launch a replacement ClawHUD process.
7. Both pending-update and newly-downloaded-update paths use the same mode-aware restart rule.
8. No VeloPack `restartArgs={"--managed"}` workaround is introduced.
9. A later ordinary Managed launch after `restart=false` still performs PresentMon runtime compatibility/bootstrap validation.
10. `VelopackApp.Build().Run()` remains first in normal entry-point lifecycle ordering.
11. Cross-mode launches still share the one existing mutex and never create a second runtime.
12. No in-process mode conversion is introduced.
13. `GetRuntimeInfo` still reports the truthful current launch mode.
14. Existing RequestShutdown behavior remains unchanged.
15. Existing Control IPC security and wire protocol remain unchanged.
16. Existing Standalone tray/Settings behavior remains unchanged.
17. Managed still has no tray and no legacy Settings.
18. No renderer/telemetry/game-detection/EC/tweak mode fork is introduced.
19. Full normal CI / CTest suite passes.
20. No HUD/VRR production presentation contract source is modified.

---

## 17. Review focus for the resulting PR

Reviewers should treat the following as blocking defects:

- Managed launch still calls automatic startup registration reconciliation;
- Managed update uses `restart=true` without preserving ownership semantics;
- Managed update restarts with empty args and therefore returns as Standalone;
- Managed update works by VeloPack restarting `--managed` instead of allowing the external owner to relaunch;
- only one of the two update branches uses the mode-aware restart policy;
- post-update Managed relaunch can skip required PresentMon runtime compatibility/bootstrap;
- explicit `SetStartWithWindows` is incorrectly disabled just because runtime mode is Managed;
- startup shortcut is changed to launch `--managed`;
- per-mode single-instance mutexes are introduced;
- ClawHUD starts detecting or owning SteamAddon lifecycle;
- new mode branches appear inside HUD presentation, renderer, telemetry, game detection, or EC paths;
- any HUD/VRR safety contract regression.

Do not block this PR for hypothetical scheduler races that do not have a realistic lifecycle path. Cross-process convergence when Standalone and Managed launch nearly simultaneously remains an external-owner responsibility by design.
