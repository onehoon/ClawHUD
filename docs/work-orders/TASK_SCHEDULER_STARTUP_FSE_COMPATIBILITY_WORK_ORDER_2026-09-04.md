# Work Order — Replace Startup-Folder Shortcut with Task Scheduler for FSE-Compatible ClawHUD Startup

**Date:** 2026-09-04  
**Status:** Ready for implementation  
**Priority:** P1 / real supported-lifecycle startup failure under Windows Full Screen Experience  
**Reviewed baseline:** `main` at `c449c3c07d729d36ffac32ae1b637098918a5e6b`  
**Scope:** ClawHUD Start-with-Windows registration only  
**Expected PR count:** 1 focused PR

---

## 1. Objective

Replace ClawHUD's current per-user Startup-folder shortcut implementation with one ClawHUD-owned Task Scheduler logon task.

The concrete field problem is Windows Gaming Full Screen Experience / Xbox mode startup:

```text
current ClawHUD
-> StartWithWindows=true
-> %APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\ClawHUD.lnk
-> Windows enters FSE directly after sign-in
-> ClawHUD does not start
```

On the same MSI Claw device and FSE boot path, SteamAddonforClaw's current-user Task Scheduler logon task did start successfully and entered its normal background runtime before the user ever switched to Desktop.

The target ClawHUD behavior is therefore:

```text
Desktop sign-in
-> Task Scheduler current-user logon trigger
-> ClawHUD Standalone starts

FSE / Xbox-mode sign-in
-> the same Task Scheduler current-user logon trigger
-> ClawHUD Standalone starts
```

Do **not** add FSE detection, FSE-specific startup branches, polling, a Windows Gaming API dependency, a service, or a second persistent startup authority.

The product should have exactly one Start-with-Windows implementation:

> **one ClawHUD-owned Task Scheduler task.**

---

## 2. Field evidence and reason for the change

### 2.1 Actual FSE device result

On 2026-09-04 the same MSI Claw device was booted directly into Windows Full Screen Experience using AnyFSE / Steam Big Picture as the Gaming Home path.

Observed result:

```text
SteamAddonforClaw
-> existing Task Scheduler logon task
-> --background runtime launched normally in FSE
-> controller ownership / SteamDeck presentation became active

ClawHUD
-> existing Startup-folder ClawHUD.lnk
-> did not start in FSE
```

This is not a theoretical compatibility concern. It is a reproduced startup failure in the intended handheld lifecycle.

### 2.2 Why the current mechanism is the wrong boundary

Current ClawHUD documentation and implementation use:

```text
FOLDERID_Startup
-> ClawHUD.lnk
```

Windows FSE intentionally reduces/suppresses normal startup-app activity while entering the gaming shell. The observed device behavior is consistent with that policy.

The existing Addon task proves that a current-user Task Scheduler logon trigger is a viable startup mechanism on the same device/FSE path.

Therefore fix the startup mechanism itself. Do not build a separate FSE bootstrapper around the current shortcut.

---

## 3. Current ClawHUD baseline that must be preserved

### 3.1 `App::Run()` lifecycle

Current important ordering is:

```text
AcquireSingleInstance
-> CheckForUpdates
-> supported-hardware gate
-> PresentMon runtime prerequisite gate
-> Standalone startup-registration reconciliation
-> RuntimeMessageWindow
-> Standalone tray
-> production telemetry / game detection / HUD runtime
```

Do not move the startup-registration change across unrelated lifecycle gates in this PR.

### 3.2 Standalone / Managed ownership is already correct

`RuntimeLifecyclePolicy.h` currently defines:

```cpp
ShouldReconcileStartupRegistration(Standalone) == true
ShouldReconcileStartupRegistration(Managed)    == false
```

Keep this policy unchanged.

Managed launch must not silently become the owner of Standalone automatic-start reconciliation merely because a Managed runtime exists.

The existing explicit runtime-control operation:

```text
SetStartWithWindows(bool)
```

may continue to mutate the user preference regardless of launch mode, exactly as it does today.

### 3.3 Existing setting rollback semantics are correct

Current `App::SetStartWithWindows(bool)` behavior is:

```text
save previous value
-> update in-memory desired value
-> ApplyStartupRegistration()
-> failure: restore previous value and do not persist
-> success: persist new setting
```

Preserve this contract.

Do not change the Control IPC operation number, wire schema, WPF Settings UI contract, or settings.ini key for this work.

### 3.4 VeloPack stable executable resolution is already correct

Keep:

```text
StartupExecutablePath.h
StartupExecutablePath.cpp
ResolveStartupExecutable(...)
```

Current installed behavior correctly resolves:

```text
<Root>\current\ClawHUD.exe
-> <Root>\ClawHUD.exe
```

when the normal VeloPack layout is proven.

Portable/dev/unexpected layouts continue to fall back to the running executable.

The new Scheduled Task must use this existing resolved startup executable.

Do not point the task permanently at:

```text
<Root>\current\ClawHUD.exe
```

because VeloPack replaces `current` across updates.

---

## 4. Explicit product boundaries

This PR must stay small.

### Required

1. Replace Startup-folder shortcut creation/removal with one Task Scheduler task.
2. Keep ClawHUD runtime non-elevated.
3. Elevate only the short task create/update/delete operation.
4. Use the existing VeloPack stable root stub as the installed task target.
5. Keep Standalone/Managed policy unchanged.
6. Keep `StartWithWindows` as a user preference.
7. Read back and verify the exact owned task after privileged mutation.
8. Keep the task valid on battery and without an execution-time limit.
9. Remove obsolete shortcut-creation production code and shortcut-only tests/docs.
10. Update uninstall cleanup to remove the owned task.

### Explicitly out of scope

Do **not** add:

- FSE detection or `IsGamingFullScreenExperienceActive()` logic;
- Gaming Home registration;
- AnyFSE/SteamFSE integration;
- a Windows service;
- a permanent elevated ClawHUD runtime;
- `Run with highest privileges` for the scheduled ClawHUD process;
- a generic Task Scheduler framework;
- a startup watchdog;
- a retry service;
- task enumeration/cleanup outside the fixed ClawHUD-owned task;
- a new startup preference/state store;
- support for Fast User Switching, RDP, or multi-session;
- legacy `.lnk` migration/compatibility code.

### No legacy startup migration

This is a single-user development installation. Do **not** implement automatic migration from `ClawHUD.lnk`.

Specifically, do not add code that:

```text
detects old ClawHUD.lnk
backs it up
keeps both startup roots temporarily
creates the task and then conditionally deletes the link
stores a migration-complete flag
repairs old releases
```

The existing machine's legacy link will be removed manually before device validation, or Start-with-Windows will be turned off once on the old build before installing the new build.

After this PR, production ClawHUD code should treat Task Scheduler as the only Start-with-Windows authority.

---

## 5. Target Task Scheduler contract

Create exactly one task in the Task Scheduler root folder.

Recommended fixed task name:

```text
ClawHUD
```

Recommended description:

```text
Starts ClawHUD after Windows logon.
```

### 5.1 Principal

The task must target the original current interactive user:

```text
UserId   = current interactive user
LogonType = TASK_LOGON_INTERACTIVE_TOKEN
RunLevel  = TASK_RUNLEVEL_LUA / least privilege
```

The task registration operation may be elevated, but **the task itself must launch ClawHUD at normal user privilege**.

Do not use:

```text
TASK_RUNLEVEL_HIGHEST
SYSTEM
service account
password logon
```

### 5.2 Trigger

Use one logon trigger:

```text
TASK_TRIGGER_LOGON
UserId = current interactive user
```

No boot trigger and no startup delay are required.

### 5.3 Action

Use one exec action:

```text
Path             = ResolveStartupExecutable(currentProcessExecutable).path
Arguments        = empty
WorkingDirectory = parent directory of resolved executable
```

Do **not** pass:

```text
--managed
```

Start-with-Windows remains a Standalone launch.

### 5.4 Persistent handheld settings

The task must remain runnable on battery and must not expire while ClawHUD is intentionally running:

```text
DisallowStartIfOnBatteries = false
StopIfGoingOnBatteries     = false
ExecutionTimeLimit         = PT0S
```

Do not add a short execution-time limit.

### 5.5 Exact compliance snapshot

Normal ClawHUD must be able to read the fixed task and distinguish:

```text
task absent
exactly compliant
materially drifted
read failed
```

At minimum verify:

```text
enabled
exec path
arguments are empty
working directory
logon-trigger user
principal user
TASK_LOGON_INTERACTIVE_TOKEN
TASK_RUNLEVEL_LUA
DisallowStartIfOnBatteries=false
StopIfGoingOnBatteries=false
ExecutionTimeLimit=PT0S
```

A read exception is **not** equivalent to task absence.

Do not rewrite an already-compliant task.

This avoids unnecessary UAC prompts on every normal launch.

---

## 6. Recommended implementation shape

Keep one narrow owner:

```text
src/ClawHUD/StartupTaskRegistration.h
src/ClawHUD/StartupTaskRegistration.cpp
```

Do not create a generic `TaskSchedulerManager`, `StartupAuthorityService`, `StartupRepairService`, or multi-backend abstraction.

The new unit should own only the fixed ClawHUD startup task.

A suitable narrow API can be equivalent to:

```cpp
namespace clawhud
{
struct StartupTaskResult
{
    bool success{};
    std::wstring message;
};

StartupTaskResult SynchronizeStartupTask(
    bool enabled,
    const std::filesystem::path& processExecutable);

std::optional<int> TryRunStartupTaskHelperCommand(
    std::span<const std::wstring_view> args);
}
```

The exact public shape may differ. Prefer the smallest API that keeps Task Scheduler COM and elevation details out of `App.cpp`.

### 6.1 Keep policy and side effects together only where useful

For testability it is useful to keep the task snapshot/compliance comparison pure.

For example:

```text
StartupTaskSnapshot
DesiredStartupTask
IsStartupTaskCompliant(snapshot, desired)
```

Do not introduce an interface hierarchy solely to mock COM.

Production COM integration can remain concrete and be covered by on-device smoke testing while the compliance matrix is unit tested as pure code.

---

## 7. Privileged mutation model

### 7.1 Normal runtime remains non-admin

Normal `ClawHUD.exe` remains the same standard-user process used today.

Do not add an application manifest requesting administrator privilege.

Existing privileged boundaries such as EC helper / MSI setup remain unrelated and unchanged.

### 7.2 Use one short self-elevated child

Actual device history from SteamAddonforClaw already demonstrated that creating the required current-user Task Scheduler entry can return `E_ACCESSDENIED` from the normal runtime.

Do not deliberately perform a known-denied normal write first.

Use the existing product pattern:

```text
normal ClawHUD
-> read current owned task
-> task exact: success, no UAC
-> task missing/drifted: start one elevated self-child
-> child mutates only the fixed ClawHUD task
-> child exits
-> normal parent verifies by independent readback
```

Recommended internal helper commands:

```text
--ensure-startup-task <original-user>
--remove-startup-task <original-user>
```

The exact spelling may differ, but keep them private implementation commands.

Use `ShellExecuteExW` / `runas` and wait synchronously with a finite timeout.

Recommended maximum child lifetime:

```text
60 seconds
```

If the UAC prompt is cancelled:

```text
helper result = cancelled/failure
-> parent reports registration failure
-> SetStartWithWindows rolls back the setting
```

Do not leave a detached elevated process.

Do not create a service.

### 7.3 Pass the original interactive user explicitly

The elevated process must not assume its own token identity is the user whose logon trigger should be created.

The normal parent must capture the intended interactive user identity and pass it explicitly to the elevated child.

A correctly quoted command-line argument is sufficient for this single-user product scope.

Do not add session enumeration, RDP/FUS handling, or a separate user-authority subsystem.

The helper must validate that the user argument is present/non-empty before mutating the task.

### 7.4 Elevated helper target

The parent should launch the helper through the stable ClawHUD executable resolved by the existing `ResolveStartupExecutable()` policy.

The elevated child should recompute its own fixed task target from the current executable layout rather than accepting an arbitrary executable path from the command line.

This keeps the privileged command surface narrow:

```text
fixed task name
fixed ClawHUD executable resolution
caller-supplied intended user only
```

---

## 8. `main.cpp` helper entrypoint ordering

Keep VeloPack lifecycle registration first.

Current invariant:

```cpp
Velopack::VelopackApp::Build()
    .SetAutoApplyOnStartup(false)
    ...
    .Run();
```

must remain the first application-level lifecycle code.

After VeloPack returns, parse command-line arguments and dispatch private startup-task helper commands **before constructing `App`**.

Conceptual shape:

```cpp
Velopack::VelopackApp::Build()
    ...
    .Run();

const auto args = ParseArguments();

if (auto helperExit = clawhud::TryRunStartupTaskHelperCommand(args))
    return *helperExit;

const auto launchMode = clawhud::ResolveLaunchMode(args);
App app(instance, launchMode);
return app.Run();
```

A helper process must not initialize:

```text
App
update check
hardware gate
PresentMon runtime
tray
HUD
telemetry
EC helper
Control IPC
```

It performs one fixed task mutation and exits.

---

## 9. `App` integration

### 9.1 Replace the implementation behind `ApplyStartupRegistration()`

Keep the existing high-level App call site and Standalone/Managed decision.

Change `ApplyStartupRegistration()` from shortcut creation/removal to the new task synchronizer.

Conceptually:

```cpp
bool App::ApplyStartupRegistration() const
{
    return clawhud::SynchronizeStartupTask(
        startWithWindows_, executablePath_).success;
}
```

The exact return type may differ.

### 9.2 Startup reconciliation

On Standalone startup:

```text
StartWithWindows=true
-> read task
-> exact compliant task: no-op, no UAC
-> absent/drifted: one elevated ensure
-> bounded parent readback
-> failure: log and continue the already-running ClawHUD runtime
```

ClawHUD startup registration is not a mandatory controller-authority prerequisite like SteamAddonforClaw Full1902.

Therefore a repair failure must **not** terminate an otherwise valid manually launched ClawHUD runtime.

Preserve the current behavior: log registration failure and continue.

### 9.3 User mutation

When `SetStartWithWindows(true)` is invoked:

```text
exact task already exists
-> success
-> persist true

missing/drifted
-> one elevated ensure
-> bounded readback proves exact task
-> success -> persist true
-> failure/cancel -> restore previous setting
```

When `SetStartWithWindows(false)` is invoked:

```text
task already absent
-> success
-> persist false

task exists
-> one elevated remove
-> bounded readback proves absence
-> success -> persist false
-> failure/cancel -> restore previous setting
```

No legacy `.lnk` work occurs in either branch.

---

## 10. Readback verification

Do not trust only the elevated child's exit code.

The normal parent must independently prove the final state.

SteamAddonforClaw hardware work already observed a short Task Scheduler read-after-write visibility lag, so use a small **read-only bounded settle** after privileged mutation.

Recommended policy:

```text
settle window   ~ 2 seconds
read interval   ~ 150 ms
writes          exactly one
UAC/elevation   exactly one
```

For ensure:

```text
read until exact task contract is visible
or timeout -> failure
```

For remove:

```text
read until a successful read proves task absent
or timeout -> failure
```

A transient read failure must not be treated as verified absence.

Do not add:

```text
repeated task writes
repeated UAC prompts
background repair loop
watchdog
unbounded retry
```

---

## 11. Remove the old Startup-folder implementation

After the Task Scheduler path is implemented, remove production shortcut creation/removal code.

Delete or simplify obsolete code associated only with:

```text
FOLDERID_Startup
ClawHUD.lnk
IShellLinkW
IPersistFile
CLSID_ShellLink
shortcut Save()
startup-folder creation
```

`App.cpp` should no longer contain the Start-with-Windows COM shortcut implementation.

### 11.1 Keep `StartupExecutablePath.*`

Do **not** delete `StartupExecutablePath.*`.

It is still the correct authority for choosing the installed VeloPack root stub vs portable/dev current executable.

### 11.2 No compatibility cleanup

Do not leave hidden production code that still probes/deletes `ClawHUD.lnk` "just in case".

This work order explicitly chooses no legacy migration support.

Before on-device validation, manually remove the old link from the test machine.

---

## 12. Uninstall cleanup

Current `UninstallCleanup` is shortcut-specific. Convert it to the new owned-task cleanup boundary.

`OnBeforeUninstall` must ensure that the fixed `ClawHUD` task does not remain orphaned and point at a removed executable.

Recommended flow:

```text
OnBeforeUninstall
-> read fixed ClawHUD task
-> absent: done
-> present: invoke the same bounded elevated remove operation
-> best-effort parent readback
```

Reuse the same narrow task-removal implementation used by `SetStartWithWindows(false)`.

Do not enumerate/delete unrelated scheduled tasks.

Do not retain the old shortcut cleanup merely for legacy compatibility.

If the VeloPack uninstall hook cannot surface a cancelled UAC result to UI, cleanup may log/best-effort fail, but the implementation must not crash or block uninstall indefinitely.

Keep `OnBeforeUninstall` noexcept-safe.

---

## 13. Recommended files

### New

```text
src/ClawHUD/StartupTaskRegistration.h
src/ClawHUD/StartupTaskRegistration.cpp

tests/StartupTaskRegistrationTests.cpp
```

### Modify

```text
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/main.cpp
src/ClawHUD/UninstallCleanup.h
src/ClawHUD/UninstallCleanup.cpp
CMakeLists.txt
cmake/ClawHUDTests.cmake
docs/MAIN-APP-SHELL.md
```

### Keep

```text
src/ClawHUD/StartupExecutablePath.h
src/ClawHUD/StartupExecutablePath.cpp
tests/StartupExecutablePathTests.cpp
```

### Remove/replace legacy-only tests

The current `UninstallCleanupTests.cpp` is specifically built around:

```text
ClawHUD.lnk is removed
OtherApp.lnk is preserved
```

Replace that shortcut-specific assertion with owned-task cleanup policy coverage appropriate to the new narrow seams.

If pure task deletion cannot be unit tested without a real scheduler, keep unit tests on policy/compliance and use on-device smoke for actual COM deletion. Do not create a large mocking framework just to preserve equivalent test shape.

---

## 14. Build / link requirements

Use the native Windows Task Scheduler COM API.

Expected SDK pieces include:

```text
<taskschd.h>
Task Scheduler COM interfaces
```

Add only the libraries actually required by the final implementation, typically including the Task Scheduler library if the chosen COM calls need it.

Do not shell out to `schtasks.exe` for the normal implementation when the existing native process can own the fixed COM contract directly.

Keep existing `shell32` use for `ShellExecuteExW(..., L"runas", ...)` and existing product shell features.

---

## 15. Unit-test requirements

Add focused tests for the pure task contract / argument policy.

At minimum cover:

### Compliance

```text
exact task                                  -> compliant
missing/disabled task                       -> not compliant
wrong executable path                       -> not compliant
unexpected arguments                        -> not compliant
wrong working directory                     -> not compliant
wrong principal user                        -> not compliant
wrong logon trigger user                    -> not compliant
wrong logon type                            -> not compliant
highest-privilege run level                 -> not compliant
DisallowStartIfOnBatteries=true             -> not compliant
StopIfGoingOnBatteries=true                 -> not compliant
finite/non-PT0S ExecutionTimeLimit          -> not compliant
```

Use case-insensitive canonical Windows path comparison where appropriate.

### Helper command parsing

```text
no helper command                           -> normal launch continues
ensure + valid user                         -> helper operation selected
remove + valid user                         -> helper operation selected
ensure/remove without user                  -> fail helper, no App construction
unknown private command                     -> normal argument handling unaffected
```

### Standalone/Managed policy

Existing `RuntimeLifecyclePolicy` tests must continue to prove:

```text
Standalone -> reconciles startup registration
Managed    -> does not reconcile automatically
```

Do not change those expectations.

### Existing settings rollback

Existing runtime-control / Settings tests that cover `SetStartWithWindows` rollback must remain green.

No protocol version change should be required.

---

## 16. On-device acceptance test

This PR is not complete until the real MSI Claw startup path is tested.

### Preparation

Because legacy migration is intentionally unsupported:

```text
1. remove old Startup\ClawHUD.lnk manually
   OR disable Start with Windows once using the old build
2. install/run the new build
```

### A. Initial task creation

```text
StartWithWindows=true
-> manually start ClawHUD
-> UAC appears once when the task is missing
-> task "ClawHUD" is created
-> ClawHUD remains non-elevated
-> readback/log reports task compliant
```

Restart ClawHUD normally:

```text
exact task already exists
-> no UAC
```

### B. Desktop cold boot

```text
normal Windows sign-in
-> ClawHUD task fires
-> ClawHUD Standalone starts
-> tray/runtime/HUD behavior remains normal
```

### C. FSE cold boot — critical acceptance case

Enable Windows Full Screen Experience / Xbox mode and boot directly into the configured Gaming Home without entering Desktop first.

Required:

```text
FSE sign-in
-> ClawHUD process starts from the scheduled task
-> ClawHUD runtime log is created
-> process remains standard-user / non-elevated
-> production runtime reaches normal steady state
```

This is the primary regression test for the work order.

### D. Start-with-Windows OFF

```text
Settings -> Start ClawHUD with Windows = OFF
-> UAC once for task removal
-> parent verifies task absent
-> setting persists false
-> reboot Desktop -> ClawHUD does not auto-start
-> reboot FSE     -> ClawHUD does not auto-start
```

### E. UAC cancellation

For both enable and disable mutations:

```text
cancel UAC
-> mutation reports failure
-> setting rolls back to previous value
-> running ClawHUD process remains alive
```

### F. Battery

With the task enabled, confirm the task still starts on battery power.

### G. Update

Apply a normal VeloPack update while StartWithWindows remains enabled.

Confirm:

```text
scheduled task path remains <Root>\ClawHUD.exe
-> no task rewrite needed
-> no new UAC caused only by version/current-directory replacement
-> next login starts the updated build
```

### H. Uninstall

Uninstall ClawHUD.

Confirm:

```text
fixed ClawHUD scheduled task no longer exists
no unrelated scheduled task is modified
```

---

## 17. Regression boundaries

Do not modify or regress:

- HUD presentation / renderer behavior;
- VRR behavior;
- game detection / foreground tracking;
- PresentMon runtime/bootstrap architecture;
- EC helper privilege/lifetime;
- Settings frontend process ownership;
- Control IPC protocol or operation ids;
- Standalone/Managed runtime composition;
- VeloPack update/restart policy;
- tray behavior;
- F8 hotkey behavior;
- suspend/resume recovery.

This PR is startup registration only.

---

## 18. Required verification

Before opening the PR:

1. Debug build succeeds.
2. Release build succeeds.
3. Existing native CTest suite is green (baseline at reviewed main: 55/55 from PR #232 integration result).
4. New StartupTaskRegistration tests are green.
5. Existing `StartupExecutablePathTests` remain green.
6. Existing runtime-control/settings tests remain green.
7. WPF Settings tests remain green if the build/test workflow runs them separately.
8. No warning/error is introduced by removed ShellLink includes or new Task Scheduler COM linkage.
9. On-device task creation/removal is validated.
10. FSE cold-boot acceptance is validated before declaring the issue fixed.

---

## 19. Completion criteria

The PR is complete when all of the following are true:

```text
[ ] ClawHUD no longer creates a Startup-folder .lnk
[ ] ClawHUD owns exactly one scheduled task named "ClawHUD"
[ ] task uses current-user logon trigger
[ ] task uses InteractiveToken
[ ] task uses least privilege
[ ] task executable is the existing resolved VeloPack root stub when installed
[ ] task arguments are empty / no --managed
[ ] task is allowed on battery
[ ] task has no execution-time limit
[ ] already-compliant task causes no UAC
[ ] missing/drifted task uses one bounded elevated self-child
[ ] elevated child mutates only the fixed ClawHUD task
[ ] parent independently verifies post-write state
[ ] StartWithWindows mutation rollback semantics remain correct
[ ] Managed runtime still does not auto-reconcile Standalone startup ownership
[ ] normal ClawHUD runtime remains non-elevated
[ ] old shortcut creation/removal production code is gone
[ ] no legacy .lnk migration code exists
[ ] uninstall removes the owned task
[ ] Desktop cold boot starts ClawHUD
[ ] FSE cold boot starts ClawHUD before any Desktop transition
[ ] StartWithWindows OFF prevents both Desktop and FSE auto-start
[ ] existing update / HUD / telemetry / settings behavior is unchanged
```

---

## 20. Implementation principle

Do not solve this with more startup architecture than the product needs.

The desired end state is deliberately simple:

```text
one persisted user preference
        ↓
one fixed scheduled task
        ↓
one Standalone ClawHUD process
```

Elevation is only a short mutation boundary around that fixed task.

The reason for the change is a real reproduced handheld lifecycle failure, not a theoretical race. Preserve that focus and do not broaden this PR into generic startup-management or FSE infrastructure.
