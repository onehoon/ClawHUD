# Work Order — Harden Logon Startup Against Early Shell / Network Readiness

**Date:** 2026-09-14  
**Status:** Ready for implementation  
**Priority:** P1 / reproduced Start-with-Windows failure on normal Windows desktop logon  
**Reviewed baseline:** `main` at `2edc8942c75c5bcee2dec84c3b9977ba2483f99a`  
**Field build:** `0.1.103`  
**Scope:** Task Scheduler logon timing, tray startup resilience, and VeloPack/WinHTTP diagnostics only  
**Expected PR count:** 1 focused PR; keep the production diff small and preferably below ~500 LOC excluding tests/docs

---

## 1. Objective

Fix the reproduced case where Windows Task Scheduler reports the ClawHUD logon task as successfully completed (`0x0`) but ClawHUD does not remain running after a normal Windows sign-in.

The field failure is not FSE-specific. It reproduced on a normal Windows desktop boot.

The immediate user-visible failure is:

```text
Windows sign-in
-> ClawHUD Task Scheduler logon trigger fires
-> ClawHUD starts normally
-> update check runs
-> hardware gate succeeds
-> PresentMon runtime is ready
-> initial tray icon registration fails
-> TrayIcon::Create() returns false
-> App::Run() treats tray failure as fatal
-> ClawHUD exits
```

The fix must preserve the Task Scheduler startup architecture introduced by PR #233. Do **not** revert to the Startup-folder shortcut.

The target product behavior is:

```text
Windows sign-in
-> ClawHUD-owned current-user Task Scheduler task starts after a short bounded delay
-> ClawHUD runtime starts
-> temporary shell / notification-area unavailability does not kill the runtime
-> tray icon is retried for a bounded period and restored on TaskbarCreated
-> update networking remains bounded and non-fatal
-> any WinHTTP failure is logged with the real stage/error code
```

---

## 2. Confirmed field evidence

The 2026-09-14 normal Windows boot log is decisive:

```text
2026-09-14 21:42:30.281 [INFO] ClawHUD started version=0.1.103 pid=6424 launchMode=Standalone
2026-09-14 21:42:30.565 [INFO] Velopack: checking stable release feed source=github-release-bounded
2026-09-14 21:42:30.628 [WARN] Velopack: update source release-feed unavailable; continuing installed version (update request failed within timeout)
2026-09-14 21:42:30.644 [INFO] Velopack: update source unavailable; continuing installed version
2026-09-14 21:42:30.702 [INFO] Hardware supported board=MS-1T91
2026-09-14 21:42:30.911 [INFO] [PresentMonRuntime] installedVersion=2.5.1 requiredVersion=2.5.1 action=reuse
2026-09-14 21:42:31.042 [INFO] [PresentMonRuntime] state=ready action=none
2026-09-14 21:42:31.088 [ERROR] Tray initialization failed
2026-09-14 21:42:31.180 [INFO] ClawHUD exiting
```

This proves:

1. The scheduled executable actually launched.
2. The ClawHUD single-process startup path was entered.
3. Hardware identification succeeded.
4. PresentMon runtime readiness succeeded.
5. The app exited because `TrayIcon::Create()` failed.
6. The update request failure was non-fatal and occurred only ~63 ms after the request began, so the current text `failed within timeout` does **not** prove an actual timeout.

Task Scheduler `0x0` is therefore not evidence that the long-lived ClawHUD runtime survived. It only shows that the scheduled action/launcher completed successfully.

---

## 3. Regression timeline / root cause

### 3.1 The tray fatal behavior is old, not newly introduced

The original tray implementation from PR #2 already used this contract:

```cpp
created_ = AddIcon();
if (!created_) Destroy();
return created_;
```

and the application treated failure as fatal:

```cpp
if (!tray_.Create(instance_))
    return 1;
```

That behavior remained through the later runtime-message-window refactor.

Therefore the tray implementation contains a **latent startup-readiness assumption**:

> `Shell_NotifyIconW(NIM_ADD)` must succeed on the first attempt or the entire application is considered unusable.

That assumption was mostly hidden while ClawHUD started from the normal Startup-folder / Explorer startup path.

### 3.2 PR #233 changed the timing boundary

PR #233 / commit `42868cbfc6b70f04e77e3819fb55d142625b05cf` replaced the Startup-folder shortcut with a current-user Task Scheduler `TASK_TRIGGER_LOGON` task.

The current trigger has:

```text
TASK_TRIGGER_LOGON
UserId = current user
Delay  = not configured
```

The prior work order explicitly stated:

```text
No boot trigger and no startup delay are required.
```

The 2026-09-14 field result disproves that assumption for the normal desktop boot lifecycle.

The scheduled task now starts ClawHUD substantially earlier in the interactive logon sequence than the old Explorer Startup-folder path. At that point:

- the shell notification area may not yet accept `NIM_ADD`;
- the network stack / DNS / automatic proxy state may not yet be ready for an immediate GitHub request.

This is the direct regression trigger.

### 3.3 PR #221 changed the update networking implementation

PR #221 / commit `35299f514270233a7c75e4babfa2705a36d555a1` replaced VeloPack 1.2.0 `GithubSource` with ClawHUD's bounded custom WinHTTP `ClawHudUpdateSource`.

That change was valid in purpose: the pinned VeloPack source had no configurable request timeout and could stall application startup indefinitely.

However the current error path collapses:

```cpp
WinHttpSendRequest(...)
WinHttpReceiveResponse(...)
```

into one generic exception string:

```text
update request failed within timeout
```

without recording `GetLastError()`.

The field request failed in ~63 ms, so the present log text is misleading. The exact network failure is currently unprovable from the log.

### 3.4 Root-cause classification

Treat the issues separately:

```text
Primary confirmed runtime failure
= initial Shell_NotifyIcon(NIM_ADD) failure is fatal

Regression trigger
= PR #233 zero-delay Task Scheduler logon launch starts earlier than the previous Startup-folder path

Secondary early-logon symptom
= bounded custom WinHTTP update request can execute before network readiness

Diagnostic defect
= WinHTTP send/receive failure loses the actual Windows error code and reports generic "within timeout"
```

Do not claim the exact WinHTTP cause until the real error code is captured on-device.

---

## 4. Required product behavior

### 4.1 Keep Task Scheduler as the only Start-with-Windows authority

Do not revert PR #233.

Keep:

```text
one root-folder task named "ClawHUD"
TASK_LOGON_INTERACTIVE_TOKEN
TASK_RUNLEVEL_LUA
current-user logon trigger
normal-user ClawHUD runtime
stable VeloPack root stub target
allowed on battery
PT0S execution-time limit
```

Do not restore a Startup-folder `.lnk` fallback.

### 4.2 Add a small fixed logon-trigger delay

Update the task contract to use:

```text
LogonTrigger.Delay = PT5S
```

Five seconds is deliberately short:

- it moves startup away from the exact sign-in boundary;
- it gives Explorer/network initialization a normal head start;
- it does not turn startup into a long boot delay;
- it preserves FSE startup semantics because the task still runs from the same current-user logon trigger.

The delay is **not** the primary reliability mechanism. It only reduces unnecessary early-readiness collisions.

The runtime must still survive shell-not-ready conditions even if five seconds is insufficient on a slower boot.

This work order supersedes only the trigger-delay statement in:

```text
docs/work-orders/TASK_SCHEDULER_STARTUP_FSE_COMPATIBILITY_WORK_ORDER_2026-09-04.md
```

All other ownership, privilege, VeloPack-target, battery, uninstall, and readback contracts remain intact.

### 4.3 Initial tray icon registration must not be an application-fatal dependency

A temporary failure of:

```cpp
Shell_NotifyIconW(NIM_ADD, ...)
```

must **not** destroy the tray host HWND and must **not** cause `App::Run()` to exit.

The tray hidden window itself is still required for Standalone tray behavior.

Therefore distinguish:

```text
fatal TrayIcon::Create infrastructure failure
- RegisterWindowMessageW("TaskbarCreated") fails
- hidden tray callback window cannot be created

non-fatal notification-area readiness failure
- Shell_NotifyIconW(NIM_ADD) returns FALSE
```

`TrayIcon::Create()` should return success after its HWND and notification data are initialized, even when the first `NIM_ADD` attempt fails and a retry has been armed.

### 4.4 Retry the tray add for a bounded period

Do not depend solely on `TaskbarCreated`.

A shell can already exist while the notification area is still transiently unavailable. In that case the tray window may miss the broadcast that would otherwise cause another add attempt.

Use a small bounded retry local to `TrayIcon`.

Recommended contract:

```text
retry interval = 1000 ms
maximum retries after initial failure = 15
```

Conceptual lifecycle:

```text
TrayIcon::Create()
-> create hidden callback HWND
-> prepare NOTIFYICONDATAW
-> TryAddIcon()
   -> success: created_=true, no retry timer
   -> failure: created_=false, arm 1s retry timer, return Create success

WM_TIMER(tray retry)
-> TryAddIcon()
-> success: KillTimer, created_=true
-> failure: increment retry count
-> max attempts reached: KillTimer, log warning, keep ClawHUD runtime alive

TaskbarCreated
-> created_=false
-> TryAddIcon()
-> if failure, re-arm bounded retry sequence
```

The retry belongs on the tray callback HWND, not the runtime/HUD message window.

Do not add a worker thread, polling service, watchdog process, or shell-enumeration loop.

### 4.5 ClawHUD remains usable even if the tray never appears

If all bounded tray retries fail:

```text
ClawHUD runtime stays alive
HUD / telemetry / game detection / PresentMon stay alive
F8 stays alive through RuntimeMessageWindow
Control IPC stays alive
tray-only Settings/Exit UX is unavailable until a later successful TaskbarCreated recovery
```

This is preferable to killing the whole runtime because a secondary shell surface is temporarily unavailable.

Do not make HUD presentation lifetime depend on notification-area availability.

---

## 5. Startup Task Scheduler implementation changes

### 5.1 Add trigger delay to the exact task snapshot

Extend `StartupTaskSnapshot` with the registered logon trigger delay, for example:

```cpp
std::wstring logonTriggerDelay;
```

When reading the `ILogonTrigger`, also call:

```cpp
ILogonTrigger::get_Delay(...)
```

The desired task contract is fixed:

```text
PT5S
```

It may be represented as a constant rather than stored redundantly in `DesiredStartupTask` if that keeps the API smaller.

### 5.2 Add delay mismatch coverage

Extend `StartupTaskMismatch` with one explicit flag, for example:

```cpp
LogonTriggerDelay = 1u << 12
```

A task created by the current PR #233 implementation has an empty/default delay and must therefore be considered drifted after this change.

The next Standalone startup with `StartWithWindows=true` should:

```text
read existing task
-> detect missing PT5S delay
-> perform exactly one elevated task rewrite
-> independently read back exact compliance
-> subsequent launches do not request UAC again
```

### 5.3 Set the delay during task registration

After creating and configuring `ILogonTrigger`:

```cpp
Bstr delay(L"PT5S");
if (FAILED(logonTrigger->put_Delay(delay.value)))
    return false;
```

Do not implement a runtime `Sleep(5000)` in `main.cpp` or `App::Run()`.

The delay belongs to the scheduler trigger so manual launches remain immediate.

---

## 6. Tray implementation changes

### 6.1 Do not destroy the tray host on initial `NIM_ADD` failure

Current behavior:

```cpp
created_ = AddIcon();
if (!created_) Destroy();
return created_;
```

Target behavior should be equivalent to:

```cpp
if (!AddIcon())
{
    StartAddRetry();
    // hidden tray callback window is valid; notification area is only
    // temporarily unavailable.
}
return true;
```

`AddIcon()` should continue to own only the `NIM_ADD` attempt and `created_` state.

### 6.2 Add narrow retry state only

Keep the state local to `TrayIcon`, for example:

```cpp
UINT trayRetryAttempts_{};
```

and private helpers equivalent to:

```cpp
void StartAddRetry();
void StopAddRetry();
void RetryAddIcon();
```

Do not introduce a generic retry framework.

Use a timer id private to the tray callback HWND.

### 6.3 Cleanup must always cancel the retry timer

`Destroy()` must:

```text
KillTimer if armed
NIM_DELETE only if created_ == true
destroy callback HWND
clear retry state
```

No timer callback may target a destroyed window.

### 6.4 `TaskbarCreated` remains authoritative recovery evidence

Keep the existing Explorer restart handling.

When `TaskbarCreated` arrives:

```text
created_ = false
attempt NIM_ADD immediately
success -> stop retry
failure -> restart the bounded retry window
```

Do not remove `TaskbarCreated` handling in favor of timers alone.

### 6.5 Logging

Add concise shell diagnostics sufficient to prove the lifecycle without flooding normal logs.

Recommended events:

```text
[Tray] initial NIM_ADD unavailable; retry scheduled
[Tray] NIM_ADD recovered attempt=<n>
[Tray] NIM_ADD retries exhausted; runtime continues without tray icon
[Tray] TaskbarCreated received; restoring notification icon
```

Do not log once per successful normal startup beyond the existing debug policy unless useful.

If `GetLastError()` after `Shell_NotifyIconW` is retained, treat it as supplemental diagnostic evidence only; do not assume all Shell_NotifyIcon failures provide a meaningful last-error value.

---

## 7. VeloPack / WinHTTP diagnostic hardening

### 7.1 Preserve the bounded custom update source

Do **not** revert PR #221 to VeloPack `GithubSource`.

Keep:

```text
ClawHudUpdateSource
WinHTTP
finite resolve/connect/send/receive timeout budget
1 MiB feed ceiling
HTTPS-only URL validation
VeloPack version comparison/download/apply ownership
update failure = non-fatal, continue installed version
```

The current problem is diagnostic accuracy, not the existence of bounded networking.

### 7.2 Split send and receive failures

Current code combines:

```cpp
if (!WinHttpSendRequest(...) ||
    !WinHttpReceiveResponse(...))
    throw std::runtime_error("update request failed within timeout");
```

Replace it with separate stages so `GetLastError()` is captured immediately:

```cpp
if (!WinHttpSendRequest(...))
{
    const DWORD error = GetLastError();
    throw MakeWinHttpFailure("send", error, elapsedMs);
}

if (!WinHttpReceiveResponse(...))
{
    const DWORD error = GetLastError();
    throw MakeWinHttpFailure("receive", error, elapsedMs);
}
```

The exact helper shape may differ.

### 7.3 Log truthful failure data

At minimum include:

```text
stage
numeric Win32/WinHTTP error code
elapsed milliseconds
```

Preferred example:

```text
Velopack: update source release-feed unavailable; continuing installed version (stage=receive error=12007 elapsedMs=61)
```

Optionally append a short `FormatMessageW` string if conversion remains simple and bounded.

Do not label every failure as a timeout.

Use `timeout` wording only when the actual error is the corresponding WinHTTP timeout error.

### 7.4 No early-logon network retry loop in this PR

Do not add repeated GitHub update attempts during startup.

The Task Scheduler `PT5S` delay will reduce immediate logon collisions, and update failure is already non-fatal.

If the device still shows a repeatable network-readiness failure after the real WinHTTP error is captured, handle that as a separate evidence-based change.

Do not add:

```text
InternetGetConnectedState polling
Network List Manager polling
DNS preflight loops
background update worker
arbitrary additional sleeps in App::Run()
```

---

## 8. `App::Run()` contract after the fix

Keep the current high-level startup order:

```text
AcquireSingleInstance
-> CheckForUpdates
-> CheckSupportedHardware
-> EnsurePresentMonRuntime
-> startup-task reconciliation
-> RuntimeMessageWindow
-> Standalone TrayIcon host
-> production telemetry / game detection / HUD
-> Control IPC
-> message loop
```

The only semantic change at the tray step is:

```text
TrayIcon host HWND creation failure
-> still fatal

notification icon NIM_ADD temporarily unavailable
-> TrayIcon::Create succeeds
-> App::Run continues
-> retry handled inside TrayIcon
```

Do not move update checking, hardware validation, or PresentMon initialization merely to work around the tray issue.

---

## 9. Tests

### 9.1 `StartupTaskRegistrationTests`

Extend the existing exact-compliance matrix.

Required cases:

```text
exact PT5S delay -> compliant
empty delay -> LogonTriggerDelay mismatch
PT1S -> mismatch
PT10S -> mismatch
all existing properties exact + only delay wrong -> only delay mismatch flag
```

Retain all existing tests for:

```text
enabled
exec path
arguments
working directory
principal user
logon-trigger user
interactive-token logon type
least privilege
battery policy
execution time limit
```

### 9.2 Tray retry policy

Do not mock the Windows shell or create a large shell abstraction solely for tests.

If a tiny pure retry-count helper is needed, test only that policy:

```text
initial failure arms retry
success stops retry
max retry count terminates retry
TaskbarCreated can restart a fresh bounded retry sequence
```

The actual `Shell_NotifyIconW` lifecycle remains a real-desktop smoke-test responsibility.

### 9.3 `ClawHudUpdateSourceTests`

Add pure/error-formatting coverage so the diagnostic cannot regress to generic timeout wording.

Required examples:

```text
send stage + error 12007 + elapsed -> output contains each field
receive stage + non-timeout error -> output does not claim timeout
actual timeout error -> timeout wording allowed
```

Do not add a live GitHub network dependency to CTest.

### 9.4 Existing regression suite

Run the full normal native Debug + Release test suite used by current main.

Do not weaken or remove any HUD presentation tests/assertions.

---

## 10. On-device acceptance matrix

This PR is specifically about a field-only lifecycle boundary, so real-device smoke is required before merge acceptance.

### A. Normal Windows cold boot — primary reproduction

Precondition:

```text
StartWithWindows=true
ClawHUD task updated to PT5S
```

Test:

```text
power off / cold boot
-> normal Windows sign-in
-> do not manually launch ClawHUD
```

Pass criteria:

```text
scheduled task launches ClawHUD
ClawHUD process remains alive beyond 30 seconds
tray icon appears
HUD/runtime remains functional
no "Tray initialization failed" followed by exit
```

Capture `%LOCALAPPDATA%\ClawHUD\logs\clawhud.log`.

### B. Normal Windows restart

Repeat via Restart, not only cold boot.

Pass criteria are the same.

### C. Existing zero-delay task migration

Install over a build that already owns the old no-delay `ClawHUD` task.

Pass criteria:

```text
first compliant Standalone launch detects delay drift
one UAC repair only
readback proves PT5S
next launch does not request UAC
```

### D. Network available

With normal internet connectivity:

```text
Velopack stable feed check succeeds or truthfully reports no update
```

No regression to update/download/apply behavior.

### E. Network unavailable at startup

Temporarily disconnect internet before launching.

Pass criteria:

```text
ClawHUD continues startup
update failure log includes real stage/error/elapsedMs
no generic false "within timeout" claim for an immediate non-timeout failure
tray/HUD/runtime still start
```

### F. Explorer restart

While ClawHUD is running:

```text
restart Explorer / shell
```

Pass criteria:

```text
TaskbarCreated path restores the tray icon
runtime does not restart or exit
```

### G. FSE regression preservation

Because Task Scheduler was originally introduced for FSE compatibility, perform at least one FSE / Gaming Home sign-in smoke before declaring the startup-task change complete.

Pass criteria:

```text
same ClawHUD task fires after PT5S
runtime remains non-elevated
ClawHUD still starts without requiring the Startup folder / Explorer StartupApps path
```

Do not change production behavior based on FSE detection.

---

## 11. Non-goals / prohibited changes

Do not use this startup fix to modify any HUD presentation contract.

The following are completely out of scope and must remain unchanged:

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
Presentation API / DirectComposition production path
premultiplied-alpha presentation contract
```

Also do not add:

- Startup-folder fallback;
- a Windows service;
- a watchdog process;
- `TASK_RUNLEVEL_HIGHEST`;
- a second startup authority;
- FSE detection;
- long arbitrary sleeps in ClawHUD startup;
- repeated update networking retries;
- generic retry/task-scheduler frameworks;
- shell process enumeration;
- unrelated Settings/UI changes;
- PresentMon runtime lifecycle changes;
- telemetry/game-detection changes.

---

## 12. Recommended implementation order

Implement in this sequence so each behavior remains reviewable:

```text
1. StartupTaskRegistration
   - add PT5S trigger delay read/write/compliance
   - extend exact-compliance tests

2. TrayIcon
   - separate hidden-window creation from NIM_ADD availability
   - make initial NIM_ADD failure non-fatal
   - add bounded tray-local retry
   - retain TaskbarCreated recovery
   - add concise lifecycle logs

3. ClawHudUpdateSource
   - split send/receive failures
   - capture GetLastError immediately
   - log stage/error/elapsed
   - remove false blanket timeout wording
   - extend pure tests

4. Full native Debug/Release tests

5. Real MSI Claw acceptance
   - normal desktop cold boot first
   - restart
   - Explorer restart
   - network-off diagnostic
   - FSE preservation smoke
```

Do not mix unrelated cleanup into this PR.

---

## 13. Expected final behavior

After this work, a normal cold boot should look conceptually like:

```text
Windows user logon
    |
    +-- Task Scheduler waits PT5S
    |
    +-- ClawHUD starts Standalone
          |
          +-- bounded update check
          |     +-- network ready -> normal VeloPack flow
          |     +-- network unavailable -> truthful error code, continue
          |
          +-- hardware supported
          +-- PresentMon runtime ready
          +-- startup task compliant
          +-- RuntimeMessageWindow ready
          |
          +-- TrayIcon hidden window ready
          |     +-- NIM_ADD succeeds -> normal tray
          |     +-- NIM_ADD fails -> runtime continues + bounded retry
          |                              |
          |                              +-- succeeds -> tray restored
          |                              +-- exhausted -> runtime still alive
          |
          +-- telemetry / game detection / HUD / Control IPC
          +-- normal message loop
```

The central invariant is:

> **A transient Windows shell notification-area readiness failure must never terminate the ClawHUD runtime.**

The Task Scheduler delay reduces the likelihood of the transient state; the non-fatal tray lifecycle removes it as a single point of process failure; improved WinHTTP diagnostics make any remaining early-logon update failure evidence-based rather than guessed.
