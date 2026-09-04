# Field Hotfix Work Order — Start-with-Windows Re-enable, Settings Lifetime, and HUD Size Stepper

Date: 2026-09-04  
Repository: `onehoon/ClawHUD`  
Baseline: `main` at `42868cbfc6b70f04e77e3819fb55d142625b05cf` (`Replace Startup-folder shortcut with a Task Scheduler startup task (#233)`)  
Field evidence: `C:\GoogleDrive\ClawHUD\logs\0904-2\clawhud.log`  
Observed build: `0.1.95`  
Revision: reviewer clarifications added for startup mismatch API, Task Scheduler validation scope, and runtime-PID watcher race/failure semantics.

## 1. Objective

Fix three field regressions found on the current production-style build and polish the HUD-size stepper without touching the HUD presentation / VRR-critical path.

This work order covers:

1. `Start with Windows` works on the initial installed state, but after the user turns it OFF, turning it ON again fails and the Settings toggle returns to OFF.
2. Tray `Exit` shuts down `ClawHUD.exe`, but an already-open `ClawHUD.Settings.exe` remains alive by itself.
3. While any discrete Settings mutation is in flight, the HUD-size `− / +` buttons visibly flash because their default WPF disabled visual is repeatedly entered/exited.
4. Replace the plain HUD-size `− / +` buttons with a compact Windows-11/Fluent-style rounded icon-button treatment while preserving the existing size-step behavior.

The implementation should be one focused field-hotfix PR unless the final diff becomes substantially larger than expected.

---

## 2. Non-negotiable constraints

### 2.1 HUD presentation / VRR contract: zero behavior change

Do **not** modify, replace, weaken, or work around any of the following:

- HUD `windowExStyle`
- `WS_EX_TRANSPARENT`
- `WS_EX_NOACTIVATE`
- `WS_EX_TOPMOST`
- existing `WS_EX_LAYERED` behavior
- `WM_NCHITTEST -> HTTRANSPARENT`
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`
- `ProductionHudPresentationContract()`
- independent-flip requirement
- Presentation API / DirectComposition production presentation path
- premultiplied-alpha presentation contract

No renderer, HUD-surface, flip-model, alpha-composition, z-order, hit-test, or Presentation API change belongs in this PR.

### 2.2 Do not change product behavior unrelated to the bugs

Keep all of the following unchanged:

- current `StartWithWindows` preference/default policy;
- the Task Scheduler root task name `ClawHUD`;
- the task ClawHUD creates remains a current-user logon-triggered task;
- `TASK_LOGON_INTERACTIVE_TOKEN`;
- `TASK_RUNLEVEL_LUA` / least privilege;
- no execution-time limit (`PT0S` semantics);
- battery start/stop settings currently owned by ClawHUD;
- VeloPack stable root-stub targeting when installed;
- portable/dev fallback to the running executable;
- one bounded self-elevated helper mutation path;
- Managed-mode startup-registration ownership rules;
- Control protocol v1 wire format and operation numbers;
- Settings single-instance activation contract;
- Settings window 600 × 600 DIP fixed geometry;
- opacity preview/commit behavior;
- no polling for frontend/runtime lifetime.

Do not solve any issue by process-name enumeration, broad process killing, a new generic command bus, or a periodic timer.

### 2.3 Task-definition validation scope for this hotfix

The **creation** contract remains the same intended ClawHUD task: a current-user logon-triggered task with the existing exec action and the existing security/battery/time-limit settings.

However, the current `StartupTaskSnapshot` / `ReadStartupTaskSnapshot()` / `IsStartupTaskCompliant()` model does **not** constitute a complete structural validator for every entry in the Task Scheduler trigger/action collections. The current implementation reads and compares the properties it already models; it does not comprehensively enforce collection cardinality or reject every possible extra trigger/action.

For this field hotfix, “strict compliance” means **strict for the currently modeled fields listed in Part A**, not strict validation of the entire `ITaskDefinition` object graph.

Unless the new field diagnostics prove that the reproduced OFF → ON failure is caused by task-definition structure, the following are explicitly **out of scope**:

- enumerating every trigger in the task;
- requiring an exact trigger collection count;
- rejecting additional unrelated trigger entries;
- enumerating every action in the task;
- requiring an exact action collection count;
- rejecting additional unrelated action entries;
- introducing a general-purpose structural Task Scheduler validator;
- broad repair/migration logic for arbitrary manually edited task definitions.

Do not claim test coverage for those structural properties in this PR.

If on-device diagnostics show that a missing/wrong-type/duplicate trigger or action is the **actual** reproduced failure, stop and extend the scope only as much as needed to represent and test that concrete defect. Do not silently broaden the hotfix preemptively.

---

## 3. Source baseline confirmed on current `main`

The current code paths relevant to this hotfix are:

### Native runtime / startup task

- `src/ClawHUD/App.cpp`
  - `App::SetStartWithWindows(bool)`
  - `App::ApplyStartupRegistration()`
  - `App::OpenSettings()`
  - `App::Exit()`
- `src/ClawHUD/StartupTaskRegistration.h`
- `src/ClawHUD/StartupTaskRegistration.cpp`
- `src/ClawHUD/StartupExecutablePath.cpp`
- `src/ClawHUD/SettingsFrontendLauncher.h`
- `src/ClawHUD/SettingsFrontendLauncher.cpp`

### WPF Settings frontend

- `src/ClawHUD.Settings/App.xaml`
- `src/ClawHUD.Settings/App.xaml.cs`
- `src/ClawHUD.Settings/MainWindow.xaml`
- `src/ClawHUD.Settings/MainWindow.xaml.cs`
- `src/ClawHUD.Settings/ViewModels/MainViewModel.cs`
- `src/ClawHUD.Settings/Styles/SettingsStyles.xaml`
- `src/ClawHUD.Settings/Services/SettingsInstanceCoordinator.cs`
- `src/ClawHUD.Settings/Services/RuntimeLoss.cs`

### Existing tests

- `tests/StartupTaskRegistrationTests.cpp`
- `tests/SettingsFrontendLauncherTests.cpp`
- `tests/ClawHUD.Settings.Tests/MainViewModelTests.cs`
- `tests/ClawHUD.Settings.Tests/MainWindowStartupTests.cs`
- `tests/ClawHUD.Settings.Tests/SettingsInstanceCoordinatorTests.cs`
- existing native presentation / VRR regression tests

---

# Part A — Fix Start-with-Windows OFF → ON re-enable failure

## 4. Exact field reproduction context

User-confirmed sequence:

```text
initial installed state
  Start with Windows = ON
  auto-start works

user testing
  ON -> OFF
  OFF succeeds

then
  OFF -> ON
  ON fails
  Settings toggle returns to OFF
```

The `0904-2` log confirms the failure side:

```text
2026-09-04 23:23:21.777 Runtime settings ... StartWithWindows=0
2026-09-04 23:23:22.415 Startup task synchronize: startup task removed
...
2026-09-04 23:23:40.409 Startup task synchronize: task registration could not be verified
2026-09-04 23:23:40.409 Startup registration failed
...
2026-09-04 23:23:43.003 Startup task synchronize: task registration could not be verified
2026-09-04 23:23:43.004 Startup registration failed
```

Do **not** reinterpret this as “Task Scheduler startup never works.” The initial ON state worked. The regression to fix is specifically the OFF → ON re-registration / readback-verification path introduced by the Task Scheduler migration.

## 5. Current code behavior

`SynchronizeStartupTask(true, ...)` currently does roughly:

```text
read existing task
resolve desired executable
if already compliant -> success, no UAC
otherwise
  run one elevated --ensure-startup-task child
  child RegisterTaskDefinition(...)
  child exits 0 on registration success
parent
  read task for up to 2 seconds
  require IsStartupTaskCompliant(snapshot, desired)
  if false -> "task registration could not be verified"
```

`App::SetStartWithWindows(bool)` then rolls the in-memory preference back to the previous value if `ApplyStartupRegistration()` reports failure.

This explains the UI symptom correctly: the WPF frontend is displaying the authoritative returned snapshot. It is **not** the source of the OFF rollback.

## 6. Do not guess the mismatching field

The current production log only records:

```text
task registration could not be verified
```

It does not say which readback property differs.

Before changing compliance semantics broadly, make the verification failure diagnosable.

### 6.1 Required public diagnostic contract

Define the mismatch contract in the existing public native header:

```text
src/ClawHUD/StartupTaskRegistration.h
```

Use a bit-maskable enum and a structured evaluation result. Exact naming may vary slightly, but the public contract must be equivalent to:

```cpp
enum class StartupTaskMismatch : std::uint32_t
{
    None                         = 0,
    TaskMissing                  = 1u << 0,
    TaskDisabled                 = 1u << 1,
    ExecPath                     = 1u << 2,
    Arguments                    = 1u << 3,
    WorkingDirectory             = 1u << 4,
    PrincipalUser                = 1u << 5,
    LogonTriggerUser             = 1u << 6,
    InteractiveTokenLogonType    = 1u << 7,
    LeastPrivilegeRunLevel       = 1u << 8,
    DisallowStartIfOnBatteries   = 1u << 9,
    StopIfGoingOnBatteries       = 1u << 10,
    ExecutionTimeLimit           = 1u << 11,
};

struct StartupTaskComplianceResult
{
    StartupTaskMismatch mismatches{StartupTaskMismatch::None};

    bool IsCompliant() const noexcept;
};

StartupTaskComplianceResult EvaluateStartupTaskCompliance(
    const StartupTaskSnapshot& snapshot,
    const DesiredStartupTask& desired) noexcept;
```

`IsStartupTaskCompliant(snapshot, desired)` may remain as the compatibility/convenience wrapper used by existing callers/tests:

```cpp
return EvaluateStartupTaskCompliance(snapshot, desired).IsCompliant();
```

Do not create two separate sources of truth for compliance.

### 6.2 Required field mapping

The evaluator must map the currently modeled fields exactly as follows:

| Snapshot / condition | Mismatch flag |
|---|---|
| `present == false` | `TaskMissing` |
| `enabled == false` | `TaskDisabled` |
| resolved executable differs | `ExecPath` |
| arguments are not empty | `Arguments` |
| working directory differs | `WorkingDirectory` |
| principal identity not equivalent to desired user | `PrincipalUser` |
| logon-trigger identity not equivalent to desired user | `LogonTriggerUser` |
| interactive-token logon contract not met | `InteractiveTokenLogonType` |
| least-privilege run level not met | `LeastPrivilegeRunLevel` |
| battery-start setting differs | `DisallowStartIfOnBatteries` |
| battery-stop setting differs | `StopIfGoingOnBatteries` |
| no-limit value differs from `PT0S` contract | `ExecutionTimeLimit` |

When `TaskMissing` is present, do not manufacture misleading secondary mismatch details from default-initialized snapshot fields. Missing task may be reported as the sole meaningful mismatch for that read.

This mismatch API intentionally covers the properties already modeled by `StartupTaskSnapshot`; it does **not** add trigger/action collection-count/type validation described as out of scope in §2.3.

### 6.3 Required final settle diagnostics

`SynchronizeStartupTask()` must retain the last readback snapshot/compliance result from the bounded settle loop.

If the elevated ensure helper reports success but the final bounded readback remains non-compliant, `StartupTaskResult.message` must include the concrete final mismatch list instead of only the generic text.

Required shape:

```text
task registration could not be verified; mismatches=PrincipalUser,ExecutionTimeLimit; ...
```

For string-valued mismatches, include expected/actual values where useful:

- executable path;
- arguments;
- working directory;
- principal user identifier;
- logon-trigger user identifier;
- execution-time-limit string.

Example only:

```text
task registration could not be verified;
mismatches=PrincipalUser;
principalUser expected="S-1-5-..." actual="MACHINE\\User"
```

The exact textual formatter is implementation-defined, but these rules are required:

- stable field names suitable for grep/log comparison;
- one final failure message, not repeated verbose dumps on every settle iteration;
- no diagnostic spam on successful startup;
- `StartupTaskResult.message` remains the message consumed by `App::ApplyStartupRegistration()` / the existing runtime logging path;
- evaluator itself should remain pure and should not log directly.

## 7. Fix user identity comparison semantically, not textually

Current `UserIdsEqual()` is only a case-insensitive string comparison.

That is too strict for a Windows security identity contract. The desired task uses a SID string, while Task Scheduler user identifiers are account identifiers and Windows APIs may represent the same account in another valid textual form.

Implement an identity-equivalence helper that compares the underlying SID when possible.

Recommended behavior:

```text
UserIdsEquivalent(a, b)
  if both strings are identical ignoring case -> true

  resolve a to SID
    - ConvertStringSidToSidW if it is already a SID string
    - otherwise LookupAccountNameW

  resolve b to SID the same way

  if both resolve -> EqualSid

  otherwise -> false
```

Use this for:

- `snapshot.principalUserId` vs `desired.userId`;
- `snapshot.logonTriggerUserId` vs `desired.userId`.

Do **not** weaken these checks to “non-empty” or “current-ish user.” They must still resolve to the exact intended user.

Also do not treat an empty logon-trigger `UserId` as equivalent to the desired current user: an empty logon-trigger user has different Task Scheduler semantics.

## 8. Keep all other currently modeled task properties strict unless field evidence proves a normalization issue

Do not solve this by simply removing compliance checks.

Keep the following exact product invariants **within the existing snapshot/compliance surface**:

- executable is the resolved ClawHUD startup target;
- no arguments;
- correct working directory;
- intended principal and logon-trigger user identity;
- interactive-token logon;
- least-privilege run level;
- allowed to start on battery;
- not stopped merely because the system moves to battery;
- no execution time limit.

`PT0S` remains the intended no-limit value.

Do not increase the 2-second settle window as the primary fix. The two field attempts both reached the same stable non-compliant result; this looks like a semantic/readback mismatch, not evidence that the only problem is eventual-consistency latency.

If actual mismatch diagnostics identify another Windows-normalized representation, normalize only that field in a semantics-preserving way and add a regression test for it.

Do not reinterpret this section as a requirement to add full trigger/action collection structural validation; §2.3 is authoritative for that scope.

## 9. Preserve the current bounded elevation model

Do not add:

- an always-elevated main process;
- a scheduled privileged helper service;
- infinite retry;
- repeated UAC prompts in the normal success path;
- arbitrary task-path / executable arguments accepted by the helper.

The expected successful OFF → ON flow remains:

```text
Settings click ON
  -> Control IPC
  -> native SetStartWithWindows(true)
  -> one bounded elevated ensure helper if required
  -> successful independent readback
  -> persisted true
  -> returned authoritative snapshot = ON
```

## 10. Failure-state consistency

Do not add complex task-transaction machinery merely for theoretical failures.

The priority in this PR is to remove the reproducible false-negative verification so the normal OFF → ON path succeeds.

However, preserve and improve diagnostics for the current edge case where the elevated helper reports registration success but the parent readback still cannot verify compliance. In that case:

- the preference must continue to roll back rather than pretending success;
- log the concrete mismatch details from §6;
- do not silently declare a non-compliant task valid;
- do not add repeated background repair loops.

If implementation finds a simple, bounded, same-elevation cleanup that can safely restore a previously-absent owned task on a **proven** failed registration, it is acceptable, but it is not required for this hotfix and must not introduce a second normal-path UAC or broad Task Scheduler mutation.

---

# Part B — Close Settings when the ClawHUD runtime exits

## 11. Current defect

`App::OpenSettings()` launches `ClawHUD.Settings.exe` as a separate process through `LaunchSettingsFrontend()` and intentionally does not track/wait for the child.

`App::Exit()` correctly shuts down the native runtime:

```text
StopRuntimeSources()
tray_.Destroy()
runtimeMessageWindow_.Destroy()
PostQuitMessage(0)
```

The field log confirms normal native shutdown:

```text
Control pipe server stopped
Production telemetry sampling stopped reason=app-shutdown
EC Helper disconnected
ClawHUD exiting
```

But `ClawHUD.Settings.exe` is an independent WPF process. Its current runtime-loss behavior is request-driven: it closes after an IPC operation proves the runtime is gone. If the user leaves the Settings window idle and chooses tray `Exit`, there is no further IPC request, so the Settings process can remain alive.

## 12. Required lifetime model

Bind the **primary Settings frontend process** to the exact ClawHUD runtime process that launched it.

Preferred design:

```text
ClawHUD.exe
  -> launches ClawHUD.Settings.exe --runtime-pid <current runtime PID>

Settings primary
  -> opens / observes that exact process
  -> event/task wait, no polling
  -> runtime exits normally or crashes
  -> marshal to WPF Dispatcher
  -> close MainWindow / shutdown if construction is not complete
  -> WPF/CLR process terminates
```

Use a process handle / `System.Diagnostics.Process` exit wait, not process-name polling.

A PID is acceptable because Settings must open the process while that runtime is alive; the resulting process handle refers to the exact process object. Do not add theoretical PID-reuse state machinery beyond normal process-handle semantics.

## 13. Native launcher changes

Extend `SettingsFrontendLauncher` so the launched frontend receives the current runtime PID.

Keep sibling-path resolution unchanged.

Recommended command line:

```text
ClawHUD.Settings.exe --runtime-pid <decimal PID>
```

Requirements:

- build the argument string in one pure/testable helper;
- keep `ShellExecuteExW` unelevated;
- keep immediate return / no synchronous child wait in the native runtime;
- do not make `ClawHUD.exe` own or kill the frontend process;
- do not enumerate `ClawHUD.Settings.exe` by name;
- keep missing-frontend error behavior unchanged.

The native runtime itself does **not** need to hold a child PID or process handle after launch.

## 14. WPF runtime-lifetime watcher

Add a small Settings-side service, for example:

```text
Services/RuntimeProcessLifetimeWatcher.cs
```

Responsibilities:

- parse/validate the supplied runtime PID through a small argument parser;
- acquire the exact process when required;
- observe exit asynchronously/event-driven;
- expose one exit callback/event or equivalent completion;
- be disposable/cancellable so WPF shutdown does not leak callbacks;
- never throw out of application shutdown paths.

Do not use a timer.

### 14.1 Argument classification must be explicit

The argument parser must distinguish these three states instead of returning only `PID?`:

```text
Absent
Malformed
Valid(pid)
```

Examples:

- no `--runtime-pid` -> `Absent`;
- non-numeric / zero / negative / overflow / missing value -> `Malformed`;
- positive decimal PID within `int` range -> `Valid(pid)`.

`Absent` and `Malformed` are the **only** states that use the existing manual/direct-launch fallback behavior.

Once `Valid(pid)` is supplied, Settings is a runtime-bound launch. Do not silently downgrade that launch into an unbound IPC-only lifetime model.

### 14.2 Valid PID = fail closed for lifetime binding

For `Valid(pid)`, apply these rules:

1. Try to acquire/observe the exact runtime process immediately during primary Settings startup.
2. If the process already exited before acquisition (for example `Process.GetProcessById(pid)` reports no such running process), treat runtime exit as already observed.
3. If the process exits after acquisition but before event/wait registration is fully armed, the implementation must still observe the exit. Use an API/pattern with race-safe exit semantics such as `WaitForExitAsync`, or subscribe and then immediately re-check `HasExited`.
4. If the process is already exited at any of those points, record the same pending-close/pending-shutdown state as a normal observed exit.
5. If a valid PID was supplied but watcher initialization fails for another reason (for example access denied / `Win32Exception` / inability to establish an exit observation), **fail closed**: log the watcher initialization failure and close/shutdown Settings rather than leaving an orphanable unbound frontend.
6. Do **not** use successful IPC as a lifetime fallback for a `Valid(pid)` watcher-initialization failure. IPC may still be used for normal Settings data/control operations; it is not a substitute lifetime authority in this path.

The important product rule is:

```text
valid runtime PID supplied
  -> either exact runtime exit is observable
  -> or Settings closes

never
  -> watcher setup fails
  -> Settings continues indefinitely as an unbound orphan candidate
```

### 14.3 App integration

In `App.xaml.cs`:

- preserve current `SettingsInstanceCoordinator` primary/relay behavior;
- only the primary Settings process needs the long-lived runtime watcher;
- relay processes should continue to signal the primary and exit without constructing a ViewModel/client/window;
- start the watcher early enough that runtime exit during Settings startup cannot leave a permanent orphan;
- if runtime exit is observed before `MainWindow` is fully constructed, record a pending-close/pending-shutdown state and terminate the Settings app as soon as the Dispatcher/application state allows;
- callbacks from a process/thread-pool thread must be marshalled onto `Application.Dispatcher` before touching WPF objects;
- ensure exit notification is handled at most once;
- dispose/cancel the watcher in `OnExit`;
- watcher disposal racing with runtime exit must not resurrect or touch a shutting-down App.

Do not change the single-instance mutex/event contract merely to implement this lifetime binding.

### 14.4 Manual/direct Settings launch fallback

If `ClawHUD.Settings.exe` is started with parser state `Absent` or `Malformed`, retain the current behavior:

- attempt the normal runtime IPC startup handshake;
- if runtime is unavailable/incompatible, close as today;
- do not crash solely because the private lifetime argument is absent or malformed.

This keeps developer/manual launch behavior usable while the tray path receives stronger lifetime semantics.

Do **not** apply this fallback when the argument parsed as `Valid(pid)` but exact process observation could not be established; that path is fail-closed per §14.2.

## 15. Required user-visible result

With Settings open from the runtime/tray path:

```text
tray -> Exit
```

must result in both processes terminating:

```text
ClawHUD.exe          -> exits through the existing normal shutdown path
ClawHUD.Settings.exe -> observes runtime exit and closes itself
```

The same must hold if the runtime exits in the small window between launching Settings and the watcher becoming fully initialized.

No `TerminateProcess` against Settings is needed.

---

# Part C — Remove HUD-size stepper flashing without weakening mutation exclusion

## 16. Current cause

`MainViewModel` intentionally disables all discrete settings while a discrete mutation / activation refresh / opacity interaction is busy:

```text
AreDiscreteSettingsControlsEnabled
  = snapshot exists
    && !mutationInFlight
    && !refreshInFlight
    && !opacityBusy
```

`MainWindow.xaml` applies that state to card containers.

The HUD-size buttons are also ordinary WPF `Button`s with the very small current `StepperButton` style:

```text
Width 34
Height 32
FontSize 16
Padding 0
```

Unlike the custom segment/switch templates, these buttons still use the default WPF Fluent Button template, so every mutation causes their effective `IsEnabled` state to enter and leave the default disabled visual. That is the visible `− / +` flash.

The interaction exclusion itself is correct and must remain.

## 17. Do not fix the flash by leaving controls interactive

Do not:

- remove `_mutationInFlight` / `_refreshInFlight` guards;
- accept concurrent discrete mutations;
- silently drop user clicks while visually pretending the control is usable;
- introduce a second mutation queue;
- special-case HUD size at the IPC layer.

The buttons must remain non-interactive while another discrete mutation is active.

The fix is visual/template-level, while preserving the existing ViewModel concurrency contract.

## 18. Replace the plain stepper with compact Fluent icon buttons

Replace `StepperButton` with a purpose-built HUD-size icon-button template.

Target appearance:

```text
HUD size                         ( − )   +2   ( + )
```

Requirements:

- approximately 32 × 32 DIP each;
- circular or near-circular rounded background (`CornerRadius` around half the size);
- use existing Fluent dynamic brushes already present in `SettingsStyles.xaml`;
- subtle default fill/stroke;
- hover and pressed feedback only when actually interactive;
- centered minus/plus icon geometry;
- avoid font-baseline-looking `−` / `+` text if practical: a simple `Path` geometry is preferred;
- no external icon package or new UI dependency;
- keep the overall 600 × 600 window and current card density;
- keep the current size range / step semantics (`-2 .. +2`, step 1);
- keep `Default`, `+1`, `+2`, `-1`, `-2` label semantics.

Give the center size label a small fixed width and centered text so switching between `Default` and numeric values does not move the right-side button.

## 19. Separate boundary visual state from transient busy state

A size button at the real range boundary should still look unavailable:

```text
HudSizeOffset == -2 -> decrease is dimmed/unavailable
HudSizeOffset == +2 -> increase is dimmed/unavailable
```

But a temporary unrelated mutation such as:

- Enable HUD;
- Display mode;
- font;
- alignment;
- background width;
- Intel VRR Range Fix;
- Start with Windows;

must **not** make both size buttons visibly flash to the default disabled appearance and back.

Implement a range-only visual signal, for example:

```text
CanDecreaseHudSizeByRange
CanIncreaseHudSizeByRange
```

or equivalent XAML data triggers based on `HudSizeOffset`.

The actual input enablement can continue to include the global busy state, but the custom stepper template should not change opacity/fill merely because the parent temporarily disables the entire discrete-control surface.

In other words:

```text
busy state
  -> input blocked
  -> stepper visual remains stable

actual -2/+2 boundary
  -> corresponding button visually dimmed
  -> input blocked
```

Do not globally remove disabled styling from unrelated controls.

---

# Part D — Tests

## 20. Native startup-task tests

Extend `StartupTaskRegistrationTests.cpp`.

Required coverage:

1. existing exact compliant snapshot remains compliant;
2. every currently-tested modeled-field mismatch remains a mismatch;
3. `EvaluateStartupTaskCompliance()` identifies the exact expected `StartupTaskMismatch` flag for every single-field mutation;
4. multiple modeled mismatches produce the combined bit mask;
5. `IsStartupTaskCompliant()` remains a thin true/false view over the same evaluator;
6. case-insensitive identical SID strings remain equivalent;
7. a SID string and another Windows textual identity resolving to that same SID are treated as the same user;
8. different resolved SIDs are not equivalent;
9. empty / unresolvable identity is not accepted as the intended current user;
10. execution-time-limit, run-level, logon-type and battery checks remain strict;
11. helper command parsing behavior is unchanged.

For the SID/account-name equivalence test, prefer deriving the current test process user SID + account name at runtime so CI does not depend on a localized hard-coded account display name.

Where the formatter is factored as a testable helper, add a focused assertion that a representative combined result produces stable mismatch names in the final failure text.

Do **not** add mandatory tests for trigger/action collection count, duplicate entries, or full structural task-definition validation; those are out of scope per §2.3 unless field diagnostics prove they are part of the reproduced defect.

Do not mock away the existing real Task Scheduler on-device smoke requirement.

## 21. Native Settings launcher tests

Extend `SettingsFrontendLauncherTests.cpp`.

Cover at least:

- sibling Settings path unchanged;
- VeloPack `current` sibling behavior unchanged;
- paths with spaces unchanged;
- runtime PID launch argument generated exactly as expected;
- no `--managed` or unrelated runtime flags are added to Settings;
- no CWD / `%PATH%` dependence introduced.

Do not headlessly call the real Shell launch in CI.

## 22. WPF lifetime tests

Add focused tests for the new runtime-lifetime watcher / argument parser.

Required behavior:

- parser returns `Valid(pid)` for a valid positive decimal PID;
- parser distinguishes `Absent` from `Malformed`;
- non-numeric / zero / negative / overflow PID is `Malformed` without crashing App;
- `Absent` and `Malformed` select the existing manual/direct IPC fallback policy;
- valid PID with an already-exited process produces runtime-exited/pending-close behavior rather than manual fallback;
- runtime exit racing between process acquisition and wait/event registration is still observed exactly once;
- valid PID watcher initialization failure (for example injected access-denied/observer-open failure) produces fail-closed Settings shutdown and **does not** select IPC as a lifetime fallback;
- normal watcher exit notification fires once;
- disposal prevents a late callback from touching a shutting-down App;
- callback is safe to marshal to the Dispatcher;
- exit observed before `MainWindow` construction results in pending close/shutdown once safe;
- relay/single-instance behavior remains unchanged.

Prefer a small injectable process-observer/wait seam for unit tests rather than spawning flaky external processes solely for timing tests.

The test seam must be able to deterministically model:

```text
already exited
exit during watcher initialization
watcher initialization throws/fails
normal later exit
dispose racing with exit
```

## 23. WPF ViewModel / visual-state tests

Extend `MainViewModelTests.cs` and, where practical, `MainWindowStartupTests.cs`.

Required semantics:

- transient `IsMutationInFlight` still disables discrete input;
- range-only HUD-size availability remains stable while another mutation is in flight;
- decrease boundary at `-2` remains unavailable;
- increase boundary at `+2` remains unavailable;
- size stepping still sends exactly one mutation and applies only the returned authoritative snapshot;
- no geometry regression from 600 × 600 / `ResizeMode.NoResize`;
- opacity startup regression test remains green;
- stepper buttons use the new compact template / expected dimensions.

Do not attempt a fragile screenshot-pixel assertion for the flash in unit tests. The final visual acceptance is an on-device smoke test.

---

# Part E — On-device acceptance

## 24. Start-with-Windows regression test

Use the installed VeloPack layout on the target Claw device.

### Exact required sequence

Start from a working ON state, then:

```text
1. Open Settings
2. Start with Windows = OFF
3. approve elevation if requested
4. verify toggle stays OFF
5. verify root Task Scheduler task "ClawHUD" is absent

6. Start with Windows = ON
7. approve exactly the expected bounded elevation prompt
8. verify toggle stays ON
9. close/reopen Settings -> still ON
10. restart ClawHUD -> runtime log loads StartWithWindows=1
11. verify root task "ClawHUD" exists and is enabled
12. verify no "task registration could not be verified" warning
```

Repeat ON → OFF → ON at least twice more.

Also verify a compliant ON state on ordinary ClawHUD startup does not prompt for UAC or rewrite the task.

If verification still fails, the new log must identify the exact mismatching **modeled** property and include useful expected/actual values for string mismatches. Do not merge a build that merely changes the generic failure text while the OFF → ON path is still broken.

## 25. Settings lifetime test

At 1920 × 1200 / 150%:

```text
1. launch ClawHUD
2. open Settings from tray
3. leave Settings idle
4. tray -> Exit
```

Expected:

- HUD/runtime shuts down normally;
- `ClawHUD.exe` exits;
- open `ClawHUD.Settings.exe` closes immediately from runtime-process exit observation;
- no orphan Settings process remains;
- no WPF fatal log is produced.

Repeat with:

- Settings focused;
- Settings unfocused;
- Settings opened, then tray clicked again to exercise the relay/show-existing path;
- native runtime terminated unexpectedly in a lab run (Settings should also close; no polling required).

Also validate the startup race explicitly:

```text
launch Settings from ClawHUD
terminate ClawHUD immediately while Settings is starting
```

Expected: Settings either observes the already-exited runtime or records pending shutdown and exits; it must not remain open because the watcher missed the process between PID parsing and observer setup.

A synthetic/manual valid-PID watcher-initialization failure, if practical to expose in a diagnostic build/test seam, must fail closed rather than leave Settings running unbound.

## 26. HUD-size visual test

At the same 1920 × 1200 / 150% Settings layout:

With HUD size at `Default` or `0`, exercise each of the following:

- Enable HUD toggle;
- In-game only / Always;
- font toggle;
- alignment buttons;
- background width;
- Intel VRR Range Fix;
- Start with Windows.

Expected:

- HUD-size buttons do not flash/dim/repaint into the default WPF disabled appearance on each mutation;
- they remain non-interactive while the mutation is actually busy;
- hover/pressed treatment looks stable and Fluent;
- center label does not shift the right button when changing `Default` ↔ `+1/+2/-1/-2`;
- at `-2`, only decrease has the boundary-unavailable visual;
- at `+2`, only increase has the boundary-unavailable visual.

---

# Part F — Regression gates

## 27. Required automated validation

Before PR completion:

### WPF

```text
dotnet build <Settings/test projects> -c Release
dotnet test <Settings tests> -c Release
```

No warnings newly introduced by this PR.

### Native

Build Debug and Release using the repository's existing CMake flow.

Run the full normal native CTest suite using the repository's established exclusion for the offline-only `DiagWinEventTests` if still required by current main.

All pre-existing startup, lifecycle, game-detection, HUD, presentation, and VRR tests must remain green.

## 28. Explicit presentation regression requirement

Confirm zero intentional diff to the HUD presentation contract.

Existing tests/assertions for all of the following must remain intact and passing:

- click-through behavior;
- no activation;
- topmost behavior;
- transparent hit testing;
- independent flip;
- premultiplied alpha;
- `ProductionHudPresentationContract()`.

If any implementation approach appears to require touching those behaviors, stop and redesign the fix outside the presentation layer.

---

# Part G — PR review checklist

The PR is complete only if all of the following are true:

- [ ] Repro sequence **initial/working ON -> OFF -> ON** succeeds on the device.
- [ ] `StartWithWindows` no longer falls back to OFF from a false readback mismatch.
- [ ] `StartupTaskMismatch` / structured compliance result lives in `StartupTaskRegistration.h` (or an equivalently explicit public contract in that component).
- [ ] All currently modeled compliance fields map deterministically to mismatch flags.
- [ ] `SynchronizeStartupTask()` final verification failure includes the concrete mismatch field(s) in `StartupTaskResult.message`.
- [ ] Useful expected/actual values are included for relevant string-valued mismatches.
- [ ] User identity comparison is semantic SID equivalence, not blind string equality.
- [ ] The currently modeled Task Scheduler security / battery / no-limit / executable contract is not weakened.
- [ ] Full trigger/action collection structural validation was not accidentally added or claimed unless actual field evidence required it.
- [ ] Normal compliant startup still avoids unnecessary UAC.
- [ ] Tray `Exit` closes both native runtime and an already-open Settings frontend.
- [ ] Settings lifetime uses event/process-exit observation, not polling.
- [ ] A valid `--runtime-pid` that is already exited produces pending close/shutdown.
- [ ] A valid `--runtime-pid` whose watcher cannot initialize fails closed; IPC is not used as a lifetime fallback.
- [ ] Exit racing with watcher initialization cannot leave an orphan Settings process.
- [ ] Only absent/malformed runtime-PID arguments use the manual/direct-launch fallback.
- [ ] No process-name enumeration or broad `TerminateProcess` solution was added.
- [ ] WPF single-instance relay behavior remains intact.
- [ ] HUD-size `− / +` no longer visibly flashes during unrelated mutations.
- [ ] HUD-size input is still blocked while a mutation is busy.
- [ ] Real min/max boundaries remain visibly unavailable.
- [ ] New stepper is compact, rounded, Fluent-style, and dependency-free.
- [ ] 600 × 600 fixed Settings geometry is unchanged.
- [ ] Opacity interaction behavior is unchanged.
- [ ] Control protocol v1 is unchanged.
- [ ] Native and WPF tests are green.
- [ ] On-device smoke is documented in the PR.
- [ ] HUD presentation / VRR contract has zero intentional behavior change.

## Final implementation intent

This is a field hotfix, not another architecture refactor.

Prefer the smallest changes that restore the intended contracts:

```text
Task Scheduler
  same intended task creation behavior
  + one explicit modeled-field mismatch API
  + semantic user-identity readback comparison
  + useful final failure diagnostics
  + no unrelated full task-definition structural validator

Settings frontend
  same separate WPF process
  + lifetime bound to the exact launching runtime process
  + valid-PID startup races fail closed instead of orphaning

HUD-size control
  same mutation semantics
  + stable busy-state visual
  + better compact Fluent icon buttons
```

Do not broaden the PR beyond these field issues.