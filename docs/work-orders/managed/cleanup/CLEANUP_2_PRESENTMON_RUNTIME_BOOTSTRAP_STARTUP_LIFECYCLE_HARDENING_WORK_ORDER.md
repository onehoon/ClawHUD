# Cleanup 2 — PresentMon Runtime Bootstrap / Startup Lifecycle Hardening Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** Post-CH-RTF ClawHUD Standalone Cleanup  
> **Cleanup split:** 2 / 3  
> **Analyzed main HEAD:** `22e680cc7e2ad19ffbfef28b2fbec1a74e5f14ab`  
> **Previous cleanup:** Cleanup 1 / PR #219 merged  
> **Scope:** Put the required PresentMon API2 shared-runtime bootstrap at the correct startup boundary, make bootstrap results authoritative, and stop forcibly terminating Windows Installer on timeout  
> **Status:** Ready for implementation

---

## 1. Objective

ClawHUD depends on the installed PresentMon API2 shared runtime before the production telemetry provider can initialize.

The current runtime bootstrap itself already has useful validation and a narrow installation path, but it is executed at the wrong lifecycle point and its result is ignored.

Current normal-process startup is effectively:

```text
Velopack fast-exit lifecycle
-> EnsurePresentMonRuntime()
-> DPI / launch-mode resolve
-> construct App
-> AcquireSingleInstance()
-> CheckForUpdates()
-> CheckSupportedHardware()
-> startup registration
-> runtime message window / tray
-> PresentMon provider
-> HUD runtime
```

This creates concrete user-visible problems:

```text
Unsupported PC
-> ClawHUD may request PresentMon MSI elevation first
-> only afterwards says the device is unsupported

Second ClawHUD launch while one instance already owns the runtime
-> second process may validate / repair / elevate PresentMon first
-> only afterwards loses the single-instance mutex and exits

PresentMon MSI bootstrap failure
-> EnsurePresentMonRuntime() returns a detailed failure result
-> caller ignores it
-> ClawHUD continues toward a runtime whose required dependency is unavailable
```

There is also a Windows Installer safety problem in the current helper:

```cpp
WaitForSingleObject(msiexec, 5 minutes)
if timeout/failure:
    TerminateProcess(msiexec, ERROR_TIMEOUT)
```

ClawHUD must not forcibly terminate an in-progress Windows Installer transaction merely because its own wait budget expired.

The target normal startup becomes:

```text
Velopack fast-exit lifecycle
-> DPI / launch-mode resolve
-> construct App / initialize runtime logging
-> AcquireSingleInstance()
-> CheckForUpdates()
-> CheckSupportedHardware()
-> EnsurePresentMonRuntime()
     -> AlreadyReady / Installed       : continue
     -> reboot required                : inform user and exit
     -> elevation cancelled            : inform user and exit
     -> MSI missing/install/validation : inform user and exit
     -> installer wait timed out       : inform user and exit; DO NOT kill msiexec
-> startup registration
-> RuntimeMessageWindow / tray
-> PresentMon provider
-> HUD runtime
```

This cleanup is about prerequisite lifecycle only.

Do **not** redesign VeloPack networking, PresentMon package version/upgrade policy, Startup shortcut targeting, EC telemetry, game detection, or HUD presentation in this PR.

---

## 2. Current baseline

### 2.1 VeloPack fast-exit hook is correctly first

`main.cpp` currently begins with:

```cpp
Velopack::VelopackApp::Build()
    .SetAutoApplyOnStartup(false)
    .OnBeforeUninstall(...)
    .Run();
```

Keep this first.

VeloPack install/update/uninstall helper invocations may exit from inside `Run()`. No normal ClawHUD startup work should move before it.

### 2.2 PresentMon bootstrap is currently too early

Immediately after VeloPack `Run()`, `main.cpp` calls:

```cpp
clawhud::EnsurePresentMonRuntime();
```

Only afterwards does ClawHUD:

```text
resolve DPI
resolve launch mode
construct App
AcquireSingleInstance
CheckForUpdates
CheckSupportedHardware
```

This is the ordering defect this cleanup must fix.

### 2.3 `App::Run()` already has the correct early product gates

Current beginning:

```cpp
if (!AcquireSingleInstance()) return 0;
CheckForUpdates();
const auto hardware = CheckSupportedHardware();
... unsupported / indeterminate exit ...
```

Preserve this relative order.

In particular, do **not** move the update check after the hardware gate in Cleanup 2.

Reason:

```text
installed old ClawHUD version does not recognize a newly supported board
-> update may install a newer ClawHUD version that does support it
```

Keeping update before hardware preserves that recovery path.

Cleanup 3 will separately address whether the network update check belongs on the synchronous startup critical path.

### 2.4 Bootstrap result is currently discarded

`PresentMonRuntimeBootstrapResult` already includes:

```cpp
AlreadyReady
Installed
InstalledRebootRequired
NeedsInstall
MsiMissing
ElevationCancelled
InstallFailed
ValidationFailed
```

but `main.cpp` ignores the returned value entirely.

The result must become authoritative for whether normal runtime initialization is allowed to continue.

### 2.5 Current readiness criteria

The runtime is considered ready only when all current evidence passes:

```text
PresentMonSharedService is RUNNING
HKLM PresentMon sharedMiddlewarePath exists
middleware file exists
filename is PresentMonAPI2.dll
pmGetApiVersion succeeds
major/minor match the compiled API2 headers
```

Do not redesign these readiness criteria in Cleanup 2 unless implementation uncovers a concrete correctness defect required to complete the stated startup policy.

The future bundled-runtime version / upgrade policy belongs to Cleanup 3.

### 2.6 Current installation path

When not ready:

```text
<ClawHUD executable dir>\runtime\ClawHUD.PresentMonRuntime.msi
-> ShellExecuteExW("runas", "msiexec.exe", "/i ... /qn /norestart")
-> wait up to five minutes
-> classify MSI exit
-> re-run readiness validation
```

Keep:

```text
MSI installation is elevated
ClawHUD.exe itself remains unelevated
/qn /norestart
post-install readiness validation
```

Do not move PresentMon installation into ClawHUD.EcHelper or another privileged component.

---

## 3. Non-negotiable boundaries

1. VeloPack `VelopackApp::Build().Run()` remains the first normal application lifecycle code.
2. `ClawHUD.exe` remains unelevated.
3. PresentMon runtime installation continues through elevated `msiexec.exe` only when readiness requires it.
4. Single-instance acquisition must happen before any PresentMon install/repair/elevation request.
5. Hardware support gate must happen before any PresentMon install/repair/elevation request.
6. Current update-before-hardware ordering remains unchanged in Cleanup 2.
7. A failed required PresentMon bootstrap must stop production runtime initialization.
8. Do not initialize the tray, RuntimeMessageWindow, PresentMon provider, game sources, HUD presentation, EC sampling, tweaks, or Control IPC after a fatal prerequisite result.
9. Do not change the production PresentMon provider/session architecture.
10. Do not change the PresentMon API2 loader ABI/path semantics.
11. Do not change Standalone/Managed composition except that both use the same corrected prerequisite gate.
12. Do not change the Control IPC protocol.
13. Do not change HUD opacity/rendering/presentation behavior.
14. Preserve every existing HUD/VRR-critical presentation invariant unchanged.
15. Do not add a generic installer/prerequisite framework for this one dependency.

---

## 4. Move bootstrap ownership into the real startup gate

### 4.1 Remove the unconditional `main.cpp` bootstrap

`main.cpp` should no longer do:

```cpp
clawhud::EnsurePresentMonRuntime();
```

before `App` exists.

After VeloPack returns normally, continue with the existing DPI / launch-mode resolution and construct `App`.

This has two benefits:

```text
App constructor initializes RuntimeLogger before bootstrap
App::Run owns all normal-startup gates in one place
```

The existing `RuntimeLogger::Log()` is lazy-safe, but centralizing bootstrap after `App` construction makes the startup trace easier to reason about.

### 4.2 Required `App::Run()` order

After this cleanup, the early section should be conceptually:

```cpp
int App::Run()
{
    if (!AcquireSingleInstance())
        return 0;

    CheckForUpdates();

    const auto hardware = CheckSupportedHardware();
    if (hardware != HardwareSupport::Supported)
        ... existing unsupported/indeterminate behavior ...;

    const auto presentMonBootstrap = clawhud::EnsurePresentMonRuntime();
    if (!HandlePresentMonRuntimeBootstrapResult(presentMonBootstrap))
        return 0; // or a small explicit nonzero fatal code if project policy chooses it

    // existing startup-registration reconciliation
    // RuntimeMessageWindow
    // tray (Standalone only)
    // telemetry / game detection / PresentMon provider / HUD
}
```

Exact factoring may differ, but the lifecycle must be:

```text
mutex winner
-> update path completes/returns
-> supported hardware confirmed
-> PresentMon prerequisite gate
-> normal runtime side effects
```

### 4.3 Side effects that must not precede bootstrap

Before successful PresentMon bootstrap, do not newly introduce:

```text
Startup shortcut reconciliation
RuntimeMessageWindow creation
tray creation
window/Steam watchers
F8 hotkey
PresentMon provider initialization
HUD presentation
EC helper / UAC
VRR tweak startup
Control IPC server
```

Today startup registration already follows the hardware gate. Keep PresentMon bootstrap immediately before that boundary.

---

## 5. Make bootstrap results authoritative

Add one small pure policy seam so every result has an explicit product meaning.

Recommended conceptual types:

```cpp
enum class PresentMonRuntimeStartupAction
{
    Continue,
    ExitInformational,
    ExitFailure,
};

constexpr PresentMonRuntimeStartupAction
PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult result) noexcept;
```

Possible location:

```text
src/ClawHUD/PresentMonRuntimeBootstrap.h
```

or a small:

```text
src/ClawHUD/PresentMonRuntimeStartupPolicy.h
```

Do not create a generic prerequisite manager/state machine.

Required mapping:

```text
AlreadyReady
    -> Continue

Installed
    -> Continue

InstalledRebootRequired
    -> ExitInformational
    -> explain that Windows reported a reboot is required
    -> do not continue into the runtime in the same process

ElevationCancelled
    -> ExitInformational
    -> user declined the required prerequisite elevation
    -> do not continue into a partially functional HUD

MsiMissing
    -> ExitFailure

InstallTimedOut         [new result; see section 6]
    -> ExitFailure

InstallFailed
    -> ExitFailure

ValidationFailed
    -> ExitFailure
```

`NeedsInstall` is currently not produced anywhere in main.

Cleanup 2 may remove that dead enum value if no code/tests require it. Do not invent a new path solely to preserve it.

---

## 6. Stop killing `msiexec.exe` on wait timeout

### 6.1 Current unsafe behavior

Current `RunInstaller()` does:

```cpp
const DWORD wait = WaitForSingleObject(info.hProcess, 5 * 60 * 1000);
if (wait != WAIT_OBJECT_0)
{
    TerminateProcess(info.hProcess, ERROR_TIMEOUT);
    WaitForSingleObject(info.hProcess, 5000);
    CloseHandle(info.hProcess);
    SetLastError(ERROR_TIMEOUT);
    return false;
}
```

This must change.

ClawHUD does not own Windows Installer strongly enough to abort an MSI transaction with `TerminateProcess` merely because a five-minute wait budget expired.

### 6.2 Required timeout behavior

Keep a bounded wait so ClawHUD itself does not wait indefinitely.

If the wait times out:

```text
close ClawHUD's process handle
DO NOT terminate msiexec
return a distinct timeout result
ClawHUD shows a bounded message and exits
installer is allowed to finish/rollback under Windows Installer ownership
```

Recommended result addition:

```cpp
PresentMonRuntimeBootstrapResult::InstallTimedOut
```

The runtime log should make the distinction clear:

```text
[PresentMonRuntime] installer_wait=timeout action=leave-installer-running
```

Do not immediately launch a second MSI instance.

The next user launch will run readiness validation again:

```text
installer finished successfully meanwhile
    -> AlreadyReady

installer failed/rolled back
    -> not ready
    -> normal bootstrap policy may request installation again
```

### 6.3 WAIT_FAILED

Handle `WAIT_FAILED` separately from timeout where practical.

Conceptually:

```text
WAIT_TIMEOUT
    -> InstallTimedOut

WAIT_FAILED / GetExitCodeProcess failure / unexpected MSI exit
    -> InstallFailed
```

Always close ClawHUD-owned handles.

Do not terminate the installer on either case.

---

## 7. User-facing startup behavior

Use simple Win32 messages. Do not introduce a new UI framework or Settings dependency.

A small helper such as:

```cpp
bool App::HandlePresentMonRuntimeBootstrapResult(
    PresentMonRuntimeBootstrapResult result);
```

is acceptable.

The exact English copy may be polished during implementation, but preserve these semantics.

### 7.1 Already ready

```text
AlreadyReady
-> no dialog
-> continue
```

### 7.2 Installed successfully

```text
Installed
-> no success dialog required
-> continue
```

Avoid an extra modal confirmation after the user already approved UAC.

### 7.3 Reboot required

Show one informational/warning message, e.g.:

```text
ClawHUD installed the required PresentMon runtime, but Windows reports that a restart is required before it can be used safely.

Restart Windows, then launch ClawHUD again.
```

Then exit before normal runtime initialization.

Do not reboot Windows automatically.

### 7.4 Elevation cancelled

Show one concise message, e.g.:

```text
ClawHUD requires the PresentMon runtime for HUD telemetry.

Installation was cancelled, so ClawHUD will exit without starting the HUD.
```

Then exit.

Do not immediately ask for elevation again in the same launch.

### 7.5 MSI missing

This means the installed ClawHUD payload is incomplete/corrupt.

Suggested semantics:

```text
Required PresentMon runtime installer is missing from the ClawHUD installation.
Reinstall or update ClawHUD.
```

Exit.

### 7.6 Install timeout

Suggested semantics:

```text
PresentMon runtime installation is taking longer than expected.
ClawHUD will exit, but Windows Installer has not been forcibly stopped.

Try launching ClawHUD again after the installation finishes.
```

Exit.

Do not offer a retry button that could race the still-running MSI.

### 7.7 Install / validation failure

Suggested semantics:

```text
ClawHUD could not prepare the required PresentMon runtime.
ClawHUD will exit without starting the HUD.
```

Logs retain the detailed reason.

Do not dump HRESULTs/MSI codes into the normal user message.

---

## 8. Standalone and Managed behavior

The prerequisite is runtime-wide, not shell-specific.

Required behavior:

```text
Standalone
    -> same corrected PresentMon prerequisite gate

Managed
    -> same corrected PresentMon prerequisite gate
```

Do not add a Managed bypass.

Do not let Managed continue without PresentMon merely because an external frontend may exist later.

If Managed launch needs elevation on a machine without the runtime, the same required-runtime flow may surface UAC and the same bounded error message.

External-owner automation is outside this cleanup.

---

## 9. Update interactions

Cleanup 2 must preserve the CH-RTF-9 update policy.

### Standalone update

```text
Acquire mutex
-> CheckForUpdates
-> VeloPack applies update and restarts
-> new process acquires mutex
-> hardware gate
-> PresentMon bootstrap validates the runtime bundled/required by the new ClawHUD
-> continue only if ready
```

### Managed update

```text
Managed process
-> CheckForUpdates
-> VeloPack applies with restart=false
-> process exits
-> a later explicit --managed launch
-> hardware gate
-> PresentMon bootstrap
```

Do not change `ShouldRestartAfterVelopackUpdate()` here.

Do not redesign update networking here.

Do not add `--managed` restart arguments here.

---

## 10. Runtime bootstrap logging

Preserve the existing `[PresentMonRuntime]` prefix and make result transitions unambiguous.

Useful examples:

```text
[PresentMonRuntime] state=ready action=none
[PresentMonRuntime] state=missing action=install
[PresentMonRuntime] elevation=requested
[PresentMonRuntime] elevation=cancelled startup=abort
[PresentMonRuntime] installer_exit=0
[PresentMonRuntime] installer_exit=3010 startup=reboot-required
[PresentMonRuntime] installer_wait=timeout action=leave-installer-running startup=abort
[PresentMonRuntime] validation=failed startup=abort
```

The app-level log may add one final line such as:

```text
PresentMon runtime prerequisite failed result=ValidationFailed
```

Do not add verbose per-frame/per-sample logging.

---

## 11. Test requirements

The implementation must add/extend deterministic tests rather than relying only on real MSI/UAC runs.

### 11.1 Startup action mapping

Test every `PresentMonRuntimeBootstrapResult` value.

Required examples:

```text
AlreadyReady              -> Continue
Installed                 -> Continue
InstalledRebootRequired   -> ExitInformational
ElevationCancelled        -> ExitInformational
MsiMissing                -> ExitFailure
InstallTimedOut           -> ExitFailure
InstallFailed             -> ExitFailure
ValidationFailed          -> ExitFailure
```

If `NeedsInstall` is removed, tests should confirm no stale mapping remains.

### 11.2 Existing readiness tests remain

Preserve tests for:

```text
all readiness evidence true -> ready
incompatible middleware -> not ready
service unavailable -> not ready
registry path missing -> not ready
```

Do not weaken current validation to make tests easier.

### 11.3 MSI exit classification

Keep:

```text
0    -> SuccessCandidate
3010 -> RebootRequiredCandidate
1603 -> Failed
```

Retain the current policy for unexpected 1641 unless a separate product decision explicitly changes it.

### 11.4 Installer wait policy

Prefer extracting a tiny pure helper if needed for deterministic tests, conceptually:

```cpp
enum class InstallerWaitOutcome
{
    Completed,
    TimedOut,
    Failed,
};
```

Then test:

```text
WAIT_OBJECT_0 -> Completed
WAIT_TIMEOUT  -> TimedOut
WAIT_FAILED   -> Failed
```

The implementation test should make it mechanically clear that the timeout path contains **no `TerminateProcess` call**.

Do not build a fake Windows Installer framework solely for tests.

### 11.5 Startup ordering

Add a small pure ordering/policy seam only if needed to make the gate testable.

At minimum review/test evidence must prove:

```text
second instance loses mutex -> no PresentMon bootstrap
unsupported hardware -> no PresentMon bootstrap
indeterminate hardware -> no PresentMon bootstrap
supported hardware -> bootstrap gate reached before startup registration/runtime window/tray
fatal bootstrap result -> runtime initialization does not continue
```

If direct App integration tests are too heavy, a narrow extracted startup-decision helper is acceptable.

Do not introduce dependency injection across all of `App` merely to unit test four startup gates.

---

## 12. Manual validation matrix

Real Windows validation is required for the behaviors that cannot be proven by unit tests.

### A. Runtime already installed

```text
launch ClawHUD
-> no PresentMon UAC
-> normal tray/HUD startup
```

### B. First supported-device launch without runtime

```text
launch
-> update check completes/returns
-> supported hardware confirmed
-> one PresentMon UAC
-> approve
-> install succeeds
-> HUD starts
```

### C. Unsupported machine without runtime

```text
launch
-> NO PresentMon UAC
-> unsupported-device message
-> exit
```

### D. Second instance while first instance is already running

With PresentMon runtime intentionally unavailable/broken for the test:

```text
first instance owns mutex
second launch
-> second process exits at mutex gate
-> NO PresentMon UAC / repair attempt from second process
```

### E. UAC cancellation

```text
supported device + runtime missing
-> PresentMon UAC
-> Cancel
-> one concise prerequisite message
-> no tray/HUD/runtime sources
-> process exits
```

### F. Missing bundled MSI

```text
runtime not ready + runtime\ClawHUD.PresentMonRuntime.msi missing
-> no normal runtime startup
-> clear reinstall/update guidance
-> exit
```

### G. Reboot-required result

If practical to reproduce:

```text
MSI returns 3010
-> reboot-required message
-> no runtime initialization
-> no automatic reboot
```

A deterministic policy test is required even if real 3010 reproduction is difficult.

### H. Installer timeout / long-running simulation

Verify the failure path does not call `TerminateProcess` on the installer.

If a real long-running MSI is not practical, code/test inspection is acceptable for this specific point.

---

## 13. Expected code areas

Likely files:

```text
src/ClawHUD/main.cpp
src/ClawHUD/App.cpp
src/ClawHUD/App.h
src/ClawHUD/PresentMonRuntimeBootstrap.cpp
src/ClawHUD/PresentMonRuntimeBootstrap.h
src/ClawHUD/PresentMonRuntimeStartupPolicy.h      [optional small pure policy]
tests/PresentMonRuntimeBootstrapTests.cpp
cmake/ClawHUDTests.cmake                          [only if a new test target/file is needed]
```

Do not touch unrelated renderer/game-detection/EC files.

---

## 14. Explicit non-goals

Cleanup 2 does **not** implement:

- VeloPack asynchronous/background update checking;
- update HTTP timeout/network redesign;
- GitHub release retention changes;
- PresentMon MSI `MajorUpgrade` / package versioning changes;
- minimum bundled PresentMon runtime version policy;
- newer-runtime downgrade protection;
- Startup shortcut root-stub targeting;
- Start-with-Windows semantic changes;
- F8/master-HUD semantic changes;
- PresentMon shared-runtime uninstall ownership changes;
- code signing;
- EC helper/service changes;
- shared EC helper;
- SteamAddon ownership/integration;
- new frontend;
- Control IPC protocol changes;
- game-detection changes;
- HUD rendering/presentation changes;
- VRR contract changes.

Those packaging/update items are Cleanup 3 or separate release work.

---

## 15. HUD / VRR safety contract

This cleanup must not modify or work around any production HUD presentation invariant.

Do not change:

```text
HUD windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
existing WS_EX_LAYERED behavior
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
ProductionHudPresentationContract()
Independent Flip requirement
Presentation API / DirectComposition production path
premultiplied-alpha presentation contract
```

No opacity implementation work belongs in Cleanup 2.

All existing HUD presentation regression tests must remain green.

---

## 16. Completion criteria

Cleanup 2 is complete when all of the following are true:

```text
Velopack fast-exit Run remains first
single-instance gate happens before PresentMon bootstrap
update check remains before hardware gate
hardware support gate happens before PresentMon bootstrap
unsupported / indeterminate device never triggers PresentMon install UAC
second losing instance never triggers PresentMon install/repair UAC
PresentMon bootstrap result is no longer ignored
AlreadyReady / Installed continue
ElevationCancelled exits cleanly
MsiMissing / InstallFailed / ValidationFailed exit cleanly
3010 reboot-required exits with explicit reboot guidance
installer wait timeout exits ClawHUD without killing msiexec
fatal prerequisite outcome occurs before startup registration / runtime window / tray / HUD sources
Standalone and Managed share the same dependency gate
existing CH-RTF lifecycle behavior remains intact
Cleanup 1 EC behavior remains intact
full Release build + CTest pass
no HUD/VRR production presentation change
```

---

## 17. Handoff to Cleanup 3

After Cleanup 2, startup dependency handling should be product-safe:

```text
ClawHUD normal launch
-> one instance
-> current-version update decision
-> supported board
-> required PresentMon runtime ready
-> only then runtime/HUD startup
```

Cleanup 3 can then address the remaining packaging/update hardening separately:

```text
VeloPack update network call on startup critical path
PresentMon wrapper MSI upgrade/version policy
future bundled PresentMon runtime version transitions
Startup shortcut stable VeloPack launcher target
remaining release packaging/ownership policy
```

Do not pull those concerns back into Cleanup 2 unless they are strictly required to make the prerequisite gate correct.
