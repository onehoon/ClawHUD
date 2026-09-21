# Work Order — CH-I2 Managed Startup Outcome and PresentMon Bootstrap Contract

**Date:** 2026-09-21  
**Status:** Ready for implementation  
**Target repository:** `onehoon/ClawHUD`  
**Target branch:** `integration/steamaddon`  
**Reviewed baseline:** `integration/steamaddon` at `ee47c22732939ae3bc18acd94977b24415aa382e`  
**Baseline note:** includes CH-I1 / PR #245  
**Expected PR count:** 1 focused PR  
**PR base:** `integration/steamaddon`  
**PR head:** implementation branch created from `integration/steamaddon`

---

# CRITICAL BRANCH POLICY

This work is part of the SteamAddon integration line and does **not** target `main`.

```text
Work only from integration/steamaddon.
Open the implementation PR against integration/steamaddon.
DO NOT target main.
DO NOT merge this PR directly into main.
DO NOT rebase onto a newer main unless explicitly requested.
```

If GitHub or local tooling proposes:

```text
base = main
```

stop and correct it to:

```text
base = integration/steamaddon
```

before opening the PR.

---

## 1. Objective

Make `ClawHUD.exe --managed` a reliable companion process that SteamAddon can start and diagnose without ClawHUD presenting its own startup-failure UI.

CH-I1 established:

```text
Managed == SteamAddon-owned ClawHUD runtime
Managed self-update = OFF
Managed Start-with-Windows ownership = OFF
Tray / standalone shell = OFF
F8 = removed
```

CH-I2 must now establish the startup contract required by the future SteamAddon process owner:

```text
SteamAddon launches ClawHUD.exe --managed

success path:
-> ClawHUD completes prerequisites
-> Control IPC starts
-> SteamAddon connects
-> GetRuntimeInfo reports Managed / Ready

failure before IPC Ready:
-> ClawHUD logs the exact reason
-> no ClawHUD MessageBox
-> process exits with a deterministic non-zero Managed startup code
-> SteamAddon can translate that code into its own UI/status

normal managed shutdown after IPC Ready:
-> RequestShutdown
-> clean exit code 0
```

Do not implement the SteamAddon-side launcher/client in this PR.

---

## 2. Why this PR is required

Current `integration/steamaddon` still has several standalone-era startup behaviors that are unsuitable for a bundled Managed companion.

### 2.1 PresentMon bootstrap failure is interactive

Current startup:

```cpp
if (!HandlePresentMonRuntimeBootstrapResult(
        clawhud::EnsurePresentMonRuntime()))
    return 0;
```

and `HandlePresentMonRuntimeBootstrapResult()` shows a ClawHUD-owned `MessageBoxW` for failure/informational results.

That is appropriate for Standalone.

It is not appropriate for Managed because SteamAddon will own the product UI.

### 2.2 PresentMon startup failures all collapse to exit code 0

Examples currently include:

```text
InstalledRebootRequired
ElevationCancelled
MsiMissing
InstallTimedOut
InstallFailed
ValidationFailed
```

All eventually produce:

```text
App::Run() -> return 0
```

A parent process cannot distinguish:

```text
clean shutdown
vs
UAC cancelled
vs
runtime MSI missing
vs
runtime install failed
vs
reboot required
```

without scraping logs.

Do not make SteamAddon parse ClawHUD log text.

### 2.3 Hardware-gate failures are also standalone-interactive

Current unsupported/indeterminate hardware startup displays ClawHUD MessageBoxes and exits 0.

Managed should report those conditions to its owner without creating a second UI authority.

### 2.4 Control IPC failure is currently non-fatal

Current code:

```cpp
if (!runtimeControlPipeServer_.Start(...))
    RuntimeLogger::Log(...,
        L"Control pipe server did not start; continuing without external Control IPC");

return ProcessMessages();
```

This remains tolerable for Standalone because the core HUD can still function independently.

For Managed it is invalid:

```text
Managed process alive
+ no Control IPC
= SteamAddon has no control/health/readiness channel
```

Managed must fail startup instead of becoming an uncontrollable orphan.

This is a realistic product failure, not a theoretical race.

---

## 3. Product lifecycle contract

The overall product rule remains:

```text
HUD OFF
-> SteamAddon normally does not keep ClawHUD Managed running

HUD ON
-> SteamAddon launches ClawHUD Managed
-> ClawHUD owns HUD prerequisites/runtime
-> SteamAddon owns process/package/UI lifecycle
```

Intel VRR Fix remains part of the ClawHUD runtime.

There is no VRR rollback concept to add.

PresentMon remains a HUD prerequisite owned by ClawHUD.

---

## 4. PresentMon ownership remains in ClawHUD

Do **not** move PresentMon installation/update ownership into SteamAddon in this PR.

Keep the existing proven behavior:

```text
Managed launch
-> IsPresentMonRuntimeReady()
-> compatible runtime at required floor or newer
   -> reuse
-> missing / incompatible / too old
   -> run bundled ClawHUD.PresentMonRuntime.msi elevated
-> verify readiness
-> continue or exit
```

Preserve:

```text
PRESENTMON_VERSION pin
version-floor semantics
newer compatible runtime reuse
no downgrade
service validation
registry validation
middleware validation
API/ABI validation
bounded msiexec wait
no TerminateProcess on timeout
post-install readback
```

UAC itself is expected Windows UI and remains allowed.

The change is only:

```text
Managed bootstrap result
-> no ClawHUD-owned error/warning MessageBox
-> deterministic exit code
```

Standalone keeps its current user-facing bootstrap messages.

---

## 5. Add a narrow Managed startup exit-code contract

Create one small header dedicated to the Managed process startup contract.

Preferred shape:

```text
src/ClawHUD/ManagedStartupExitCode.h
```

Do not introduce a generic process-state framework.

Use explicit stable integer values because SteamAddon will later depend on them.

Recommended contract:

```cpp
#pragma once

namespace clawhud
{
enum class ManagedStartupExitCode : int
{
    AlreadyRunning = 20,
    UnsupportedHardware = 21,
    HardwareIndeterminate = 22,

    PresentMonRebootRequired = 30,
    PresentMonElevationCancelled = 31,
    PresentMonMsiMissing = 32,
    PresentMonInstallTimedOut = 33,
    PresentMonInstallFailed = 34,
    PresentMonValidationFailed = 35,

    RuntimeInitializationFailed = 40,
    ControlIpcUnavailable = 41,
};

constexpr int ToProcessExitCode(ManagedStartupExitCode code) noexcept
{
    return static_cast<int>(code);
}
}
```

The exact header name may follow repository style, but keep this contract narrow.

Do not use:

```text
HRESULT
Win32 GetLastError values
MSI native exit codes
PresentMon enum ordinals
ControlStatus enum ordinals
```

as the public process-exit contract.

Those are implementation details and may change.

### Stable values

Once CH-I2 lands, these explicit values are an integration contract for SteamAddon.

Tests must lock them.

---

## 6. Map PresentMon bootstrap results explicitly

Add one pure mapping from `PresentMonRuntimeBootstrapResult` to the Managed process exit code.

Recommended location:

```text
PresentMonRuntimeStartupPolicy.h
```

or another existing narrow policy header.

Conceptual behavior:

```text
AlreadyReady
-> continue

Installed
-> continue

InstalledRebootRequired
-> ManagedStartupExitCode::PresentMonRebootRequired

ElevationCancelled
-> ManagedStartupExitCode::PresentMonElevationCancelled

MsiMissing
-> ManagedStartupExitCode::PresentMonMsiMissing

InstallTimedOut
-> ManagedStartupExitCode::PresentMonInstallTimedOut

InstallFailed
-> ManagedStartupExitCode::PresentMonInstallFailed

ValidationFailed
-> ManagedStartupExitCode::PresentMonValidationFailed
```

Do not collapse all PresentMon failures into one generic code.

These are meaningful real-world user recovery cases.

---

## 7. Make PresentMon failure UI launch-mode aware

Current `HandlePresentMonRuntimeBootstrapResult()` mixes:

```text
classification
logging
MessageBox ownership
boolean continue/exit
```

Refactor only enough to support the two launch modes.

Preferred behavior:

### Standalone

```text
PresentMon failure
-> preserve current ClawHUD warning/error MessageBox wording
-> exit afterward
```

Do not regress the current Standalone UX.

### Managed

```text
PresentMon failure
-> log exact bootstrap result
-> DO NOT show MessageBoxW
-> return matching ManagedStartupExitCode
```

A simple implementation shape is acceptable, for example:

```cpp
std::optional<int> App::HandlePresentMonRuntimeBootstrapResult(
    PresentMonRuntimeBootstrapResult result)
{
    if (result is continue)
        return std::nullopt;

    Log(...);

    if (launchMode_ == LaunchMode::Managed)
        return ManagedPresentMonExitCode(result);

    ShowExistingStandaloneMessage(...);
    return 0;
}
```

Then:

```cpp
if (auto exitCode = HandlePresentMonRuntimeBootstrapResult(
        clawhud::EnsurePresentMonRuntime()))
    return *exitCode;
```

The exact return type may differ.

Keep it simple.

Do not create a UI service, notification framework, or generic error presenter just for this split.

---

## 8. Make hardware-gate UI launch-mode aware

Current code displays:

```text
Unsupported:
"This device is not supported by ClawHUD."

Indeterminate:
"This device could not be identified..."
```

Target:

### Standalone

Preserve current MessageBox behavior.

### Managed

```text
Unsupported
-> log
-> no MessageBox
-> exit 21

Indeterminate
-> log
-> no MessageBox
-> exit 22
```

Do not remove the hardware gate.

Do not allow Managed to run on an unsupported/unknown board merely because SteamAddon launched it.

---

## 9. Single-instance startup result

Current:

```cpp
if (!AcquireSingleInstance())
    return 0;
```

For Standalone, preserve current behavior.

For Managed:

```text
existing ClawHUD instance detected
-> log
-> no UI
-> exit ManagedStartupExitCode::AlreadyRunning (20)
```

Do not attempt to kill or replace the existing process in this PR.

Do not implement Standalone-to-Managed migration here.

The later SteamAddon owner will decide whether to:

```text
connect/adopt existing runtime
request shutdown
or report a standalone conflict
```

based on IPC/runtime info.

CH-I2 only provides a truthful launch result.

---

## 10. Managed Control IPC is mandatory

This is a required lifecycle correction.

Current behavior:

```text
Control pipe Start() fails
-> log
-> continue runtime
```

Target:

### Standalone

Keep current non-fatal behavior unless existing product tests require otherwise.

### Managed

```text
Control pipe Start() fails
-> log
-> exit with ControlIpcUnavailable (41)
-> normal destructor/teardown path cleans already-started runtime sources
```

Do not leave a Managed process running without its control plane.

Example shape:

```cpp
const bool controlPipeStarted = runtimeControlPipeServer_.Start(...);

if (!controlPipeStarted)
{
    RuntimeLogger::Log(...);

    if (launchMode_ == LaunchMode::Managed)
        return ToProcessExitCode(
            ManagedStartupExitCode::ControlIpcUnavailable);
}
```

Do not add a retry loop or watchdog around pipe creation in this PR.

A deterministic startup failure is preferable to an ownerless Managed runtime.

---

## 11. Other pre-IPC initialization failures

The following existing failures occur before Control IPC becomes usable:

```text
RuntimeMessageWindow::Create() failure
StartForegroundTracking() failure
other current App::Run() fatal initialization failures
```

For Managed, map such existing generic fatal initialization failures to:

```text
ManagedStartupExitCode::RuntimeInitializationFailed (40)
```

where doing so is straightforward.

Standalone may preserve its existing return code behavior.

Do not enumerate every possible internal error into a new public code.

Only the actionable cases above need dedicated codes.

Unknown/unexpected failures may remain a generic non-zero process failure.

---

## 12. Define the readiness boundary clearly

Do not add a second readiness protocol.

The existing IPC is already sufficient.

Managed startup is considered successful when:

```text
RuntimeControlPipeServer::Start() succeeds
and
SteamAddon can issue GetRuntimeInfo
and
GetRuntimeInfo returns:
  launchMode = Managed
  runtimeState = Ready
```

No heartbeat is required.

No ready-file is required.

No registry marker is required.

No startup event object is required.

No extra named pipe is required.

SteamAddon will later use a bounded connect/retry around the existing Control IPC.

That Addon behavior is outside CH-I2.

---

## 13. Normal Managed shutdown must remain exit 0

After the runtime reached IPC Ready:

```text
SteamAddon sends RequestShutdown
-> response delivered
-> App::Exit()
-> PostQuitMessage(0)
-> ProcessMessages() returns 0
-> process exits 0
```

Preserve this.

The Managed startup error codes are for failures before usable runtime ownership is established.

Do not turn normal RequestShutdown into a special non-zero code.

---

## 14. Logging contract

Every Managed startup failure should produce one concise authoritative log entry before exit.

Recommended pattern:

```text
Managed startup failed reason=already-running exitCode=20
Managed startup failed reason=unsupported-hardware exitCode=21
Managed startup failed reason=hardware-indeterminate exitCode=22
Managed startup failed reason=presentmon-reboot-required exitCode=30
Managed startup failed reason=presentmon-elevation-cancelled exitCode=31
Managed startup failed reason=presentmon-msi-missing exitCode=32
Managed startup failed reason=presentmon-install-timeout exitCode=33
Managed startup failed reason=presentmon-install-failed exitCode=34
Managed startup failed reason=presentmon-validation-failed exitCode=35
Managed startup failed reason=runtime-initialization-failed exitCode=40
Managed startup failed reason=control-ipc-unavailable exitCode=41
```

Do not duplicate the full PresentMon bootstrap diagnostic stream.

Existing detailed bootstrap logs remain authoritative for deep diagnosis.

Do not make SteamAddon parse these strings.

The exit code is the machine contract.

---

## 15. Do not redesign HUD-off lifecycle in this PR

The product-level future rule remains:

```text
HUD OFF
-> normally no long-lived Managed ClawHUD process

HUD ON
-> Managed process runs
```

Do not add:

```text
idle runtime mode
configuration-only runtime
VRR-only runtime
PresentMon-less managed state machine
automatic process self-exit when HudEnabled becomes false
owner heartbeat
parent PID monitoring
```

Those are not required for CH-I2.

The future SteamAddon process owner will explicitly start and stop the companion.

---

## 16. Intel VRR Fix — preserve unchanged

Do not modify:

```text
TweakStartupCoordinator
IntelVrrRangeTweak
IntelArcSyncClient
AffectedPanelDetector
IntelVrrResultStore
SetIntelVrrRangeFixEnabled
existing retry policy
existing profile validation
existing no-rollback semantics
```

CH-I2 changes only startup reporting/UI ownership.

---

## 17. NON-NEGOTIABLE presentation / TopMost / VRR safety boundary

This PR is not a presentation refactor.

Do not modify:

```text
src/ClawHUD/HudPresentation*
src/ClawHUD/HudRenderer*
src/ClawHUD/HudWindowGeometry*
src/ClawHUD/HudPresentationContract*
src/ClawHUD/HudPresentationLifecycle*
```

Do not change:

```text
D3D11
DXGI
Presentation API
DirectComposition
D2D/DWrite
displayable buffer configuration
independent-flip contract
premultiplied alpha
TopMost / Z-order behavior
WS_EX_TOPMOST
window styles
SetWindowPos behavior
Show/Hide sequencing
presentation create/destroy ordering
source rect
transform
present cadence
resume presentation recovery
```

Preferred acceptance criterion:

```text
zero diff in presentation / renderer / geometry / contract files
```

If any such file appears in the PR diff, stop and re-evaluate.

---

## 18. Full 1902 isolation

SteamAddon controller ownership remains independent.

Do not couple this work to:

```text
PID1901 / PID1902
DirectInput
HidHide
VIIPER
Xbox360 / SteamDeck presentation
Center M authority
controller fail-close
controller suspend/resume
controller restart recovery
```

A ClawHUD Managed startup failure must remain a HUD feature failure only.

It must not affect Full 1902 controller authority.

---

## 19. Expected implementation scope

Likely production files:

```text
src/ClawHUD/ManagedStartupExitCode.h              # new, narrow contract
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/PresentMonRuntimeStartupPolicy.h
src/ClawHUD/RuntimeLifecyclePolicy.h              # only if a tiny mode policy belongs here
src/ClawHUD/RuntimeControlPipeServer.h             # comments only if needed
```

Potential tests:

```text
tests/PresentMonRuntimeStartupPolicyTests.cpp
tests/LaunchModeTests.cpp
tests/ManagedStartupExitCodeTests.cpp              # optional; avoid if existing tests suffice
```

CMake test registration may need a small update if a new test executable is added.

Do not touch CMake packaging behavior yet beyond test registration.

---

## 20. Required tests

### 20.1 Stable exit-code values

Lock the public Managed integration values:

```text
AlreadyRunning = 20
UnsupportedHardware = 21
HardwareIndeterminate = 22
PresentMonRebootRequired = 30
PresentMonElevationCancelled = 31
PresentMonMsiMissing = 32
PresentMonInstallTimedOut = 33
PresentMonInstallFailed = 34
PresentMonValidationFailed = 35
RuntimeInitializationFailed = 40
ControlIpcUnavailable = 41
```

Use compile-time assertions where practical.

### 20.2 PresentMon result mapping

Test every `PresentMonRuntimeBootstrapResult`.

Required:

```text
AlreadyReady -> continue
Installed -> continue
InstalledRebootRequired -> 30
ElevationCancelled -> 31
MsiMissing -> 32
InstallTimedOut -> 33
InstallFailed -> 34
ValidationFailed -> 35
```

No result may fall through silently.

### 20.3 UI ownership policy

Add a tiny pure policy if useful and test:

```text
Standalone startup failure UI -> allowed
Managed startup failure UI -> forbidden
```

Do not mock MessageBoxW.

Do not create a general UI abstraction solely for tests.

### 20.4 Control IPC mandatory policy

Test:

```text
Standalone + pipe start failure -> existing non-fatal policy
Managed + pipe start failure -> fatal Managed startup failure
```

Keep this as a pure launch-mode policy if possible.

Do not simulate complex pipe failures solely to get coverage.

### 20.5 Existing regression suite

Run:

```text
Debug build
Release build
native CTest suite
Settings/WPF tests where normal CI includes them
```

No new warnings.

All existing PresentMon bootstrap tests must remain green.

All presentation tests must remain unchanged and green.

---

## 21. Manual smoke matrix

### A. Standalone normal startup

```text
ClawHUD.exe
-> current tray behavior
-> current standalone update behavior
-> current PresentMon behavior
-> current user-facing startup error MessageBoxes
```

No user-visible regression.

### B. Managed with PresentMon already ready

```text
ClawHUD.exe --managed
-> no tray
-> no self-update
-> no startup-task mutation
-> no ClawHUD startup MessageBox
-> Control IPC starts
-> GetRuntimeInfo = Managed / Ready
```

### C. Managed with PresentMon missing/old

```text
launch --managed
-> PresentMon MSI elevation may appear
-> install succeeds
-> readiness verifies
-> Control IPC Ready
```

No ClawHUD error MessageBox on the successful path.

### D. Managed UAC cancellation

```text
launch --managed
-> PresentMon elevation prompt
-> user cancels
-> no ClawHUD MessageBox
-> process exits 31
-> detailed log contains ElevationCancelled
```

### E. Managed reboot-required simulation / validated path

If practical in tests or a controlled environment:

```text
PresentMon bootstrap result = InstalledRebootRequired
-> no Managed MessageBox
-> process-result mapping = 30
```

A pure mapping test is sufficient if forcing MSI 3010 on hardware is impractical.

### F. Managed Control IPC startup failure

Do not create pathological system interference just to reproduce this manually.

Code/policy tests are sufficient.

If naturally reproduced:

```text
pipe Start() fails
-> Managed process does not remain alive
-> exits 41
```

---

## 22. Explicitly out of scope

Do not implement:

```text
SteamAddon process launcher
SteamAddon IPC client
SteamAddon Overlay tab
SteamAddon UI error strings
ClawHUD bundled runtime payload
source pinning
artifact download
package copy
PresentMon ownership transfer
new PresentMon installer
new updater
owner PID
heartbeat
watchdog
automatic process restart
standalone-to-managed takeover
automatic standalone uninstall
new single-instance namespace
configuration-only runtime
HUD presentation changes
TopMost changes
VRR presentation changes
Intel VRR Fix algorithm changes
VRR rollback
Full 1902 controller changes
```

---

## 23. Overengineering guard

The goal is a small parent/child startup contract.

Do not add:

```text
startup state machine
generic result framework
RPC status bus
event broker
second pipe
shared memory
registry status
JSON status file
named ready event
watchdog
supervisor
retry epochs
barriers
generic service host
```

The required contract is only:

```text
before IPC Ready:
  failure -> deterministic process exit code

after IPC Ready:
  Control IPC is authoritative
```

That is sufficient for the supported product lifecycle.

---

## 24. PR review checklist

```text
[ ] PR base is integration/steamaddon, NOT main
[ ] implementation branch starts from current integration/steamaddon
[ ] Managed startup failures do not show ClawHUD MessageBox
[ ] Standalone startup MessageBoxes are preserved
[ ] PresentMon installation remains owned by ClawHUD
[ ] PresentMon version-floor / compatibility logic unchanged
[ ] UAC cancellation maps to exit 31
[ ] PresentMon reboot-required maps to exit 30
[ ] each PresentMon failure has the specified stable code
[ ] Managed unsupported hardware maps to exit 21
[ ] Managed indeterminate hardware maps to exit 22
[ ] Managed already-running maps to exit 20
[ ] Managed Control IPC failure is fatal and maps to exit 41
[ ] Standalone Control IPC failure behavior is not unnecessarily changed
[ ] normal Managed RequestShutdown still exits 0
[ ] no log-text parsing contract introduced
[ ] no heartbeat/watchdog added
[ ] Intel VRR Fix implementation unchanged
[ ] HudPresentation* unchanged
[ ] HudRenderer* unchanged
[ ] TopMost behavior unchanged
[ ] VRR-safe presentation unchanged
[ ] Full 1902/controller code untouched
[ ] Debug/Release build green
[ ] CTest green
```

---

## 25. Completion result

After CH-I2, ClawHUD provides a clean Managed startup boundary:

```text
SteamAddon starts ClawHUD.exe --managed
        |
        +-- prerequisite/runtime startup succeeds
        |     -> Control IPC Ready
        |     -> GetRuntimeInfo Managed / Ready
        |     -> normal RequestShutdown exits 0
        |
        +-- startup fails before IPC Ready
              -> no ClawHUD-owned startup error UI
              -> exact log reason
              -> deterministic non-zero exit code
```

This is the minimum reliable contract needed before SteamAddon becomes the process/package owner.

The next ClawHUD integration step after CH-I2 should be the **Addon runtime payload / packaging target** work, still on `integration/steamaddon`, not `main`.
