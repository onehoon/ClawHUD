# CH-RTF-10 — Final Integration-Contract Regression and Cleanup Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** ClawHUD Runtime / Frontend Separation  
> **Architecture source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md`  
> **PR plan source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_PR_PLAN_2026-09-02.md`  
> **Previous PRs:** #209 through #217 / CH-RTF-1 through CH-RTF-9  
> **Analyzed main HEAD:** `752817aac9ba3bff164a6539e8868946e734f8b8`  
> **Scope:** Final ClawHUD-side stabilization, stale-contract cleanup, and regression verification after runtime/frontend separation  
> **Status:** Ready for implementation

---

## 1. Objective

CH-RTF-1 through CH-RTF-9 have completed the actual ClawHUD runtime/frontend separation architecture:

```text
ClawHUD.exe
    -> Standalone
       -> shared runtime
       -> Control IPC
       -> tray
       -> legacy Win32 Settings

ClawHUD.exe --managed
    -> Managed
       -> same shared runtime
       -> same Control IPC
       -> no tray
       -> no legacy Win32 Settings
```

The Control surface is also complete for protocol v1:

```text
GetRuntimeInfo
GetSettingsSnapshot
SetStartWithWindows
SetHudEnabled
SetHudVisibilityMode
SetHudSizeOffset
SetHudFont
SetHudAlignment
SetHudBackgroundMode
PreviewHudOpacity
CommitHudOpacity
SetIntelVrrRangeFixEnabled
RequestShutdown
```

CH-RTF-10 is **not another architecture PR**.

Its purpose is to finish the ClawHUD side by:

1. auditing the merged implementation against the final integration contract;
2. removing or correcting stale source comments / active handoff documentation that still describe pre-CH-RTF behavior;
3. fixing only concrete contract regressions discovered by that audit;
4. running the complete regression suite and the required real-mode manual validation matrix;
5. leaving a clean, stable handoff surface for the future SteamAddon integration PR series.

The desired result is:

```text
ClawHUD runtime/frontend separation is complete
    -> one runtime authority
    -> one stable protocol-v1 endpoint
    -> explicit Standalone / Managed composition
    -> no stale architectural claims in active source/documentation
    -> no new architecture introduced by cleanup
```

---

## 2. This PR is stabilization, not feature development

Do **not** add new product behavior merely because this is the final PR in the series.

Specifically, do not add:

```text
SteamAddon discovery
SteamAddon process ownership
Job Object ownership
parent-process monitoring
heartbeat / watchdog IPC
Managed crash-loop restart
new protocol operations
protocol-v2
state-changed subscriptions / event bus
new frontend technology
WinUI 3 frontend
WPF frontend
browser frontend
replacement Settings executable
shared EC helper
PresentMon architecture changes
new game-detection behavior
HUD renderer redesign
HUD presentation changes
```

If the audit finds no production defect in an area, leave that area alone.

Avoid broad formatting, renaming, include cleanup, helper extraction, or unrelated refactoring.

A small final PR with evidence-backed cleanup is preferred over using CH-RTF-10 as an excuse for general code modernization.

---

## 3. Final architecture contract to audit

The merged implementation must satisfy this exact ownership model.

### 3.1 Process composition

```text
Standalone
    RuntimeMessageWindow           ON
    HUD/runtime                    ON
    Control IPC                    ON
    Tray                           ON
    legacy Settings               available

Managed
    RuntimeMessageWindow           ON
    HUD/runtime                    ON
    Control IPC                    ON
    Tray                           OFF
    legacy Settings               unavailable
```

Only shell/lifecycle policy may differ by launch mode.

### 3.2 Shared runtime authority

Both modes must use the same instances / implementations of:

```text
HudController
HudPresentation
PresentMonTelemetryProvider
ProductionTelemetryController
GameSessionController
HudSettingsStore
TweakStartupCoordinator
EC path
RuntimeControlDispatchBridge
RuntimeControlPipeServer
```

Do not create or retain a Managed-specific alternate runtime path.

### 3.3 Runtime-control authority

The intended mutation chain is:

```text
external client
-> secure Named Pipe
-> codec / validated protocol-v1 request
-> RuntimeControlDispatchBridge
-> RuntimeMessageWindow wake
-> ClawHUD main thread
-> ExecuteRuntimeControlRequest
-> IRuntimeControl / App semantic authority
-> authoritative response
```

The pipe worker must not directly mutate runtime state.

### 3.4 Settings authority

Legacy `SettingsWindow` is still allowed and must remain functional in Standalone for now.

Its product control relationship remains:

```text
SettingsWindow
-> IRuntimeControl
-> App/runtime semantics
```

It must not regress back to direct concrete `App` product-setting calls.

Do not remove the legacy Settings implementation in CH-RTF-10.

Frontend replacement is a separate project.

---

## 4. Concrete stale source comments already found on current main

The audit has already identified source comments that are now factually stale after CH-RTF-7/8.

These should be corrected in this PR.

### 4.1 `App.h` — Control pipe is no longer read-only

Current `App.h` still says:

```cpp
// Secure read-only Named Pipe transport (GetRuntimeInfo /
// GetSettingsSnapshot only). Forwards read-only requests to the bridge;
```

That was true after CH-RTF-6 but became false in CH-RTF-7.

Protocol v1 now exposes all settings mutations plus `RequestShutdown`.

Replace the stale comment with wording describing the current responsibility, for example:

```cpp
// Secure local current-user/session Control Named Pipe transport. Decodes
// protocol-v1 requests and forwards every validated runtime operation to the
// main-thread dispatch bridge; owns transport/security only.
```

Do not change runtime behavior merely to match the comment.

### 4.2 `App.h` — Managed is no longer a "future no-tray mode"

Current `RuntimeMessageWindow` comment still contains:

```text
so a future no-tray mode keeps this infrastructure
```

Managed mode now exists in production.

Update the comment to describe the current invariant, for example:

```text
independent of TrayIcon so Managed mode keeps F8/power/timer/runtime message infrastructure without a tray
```

Again, comment-only unless an actual implementation defect is found.

### 4.3 Audit nearby series-era comments

Inspect at minimum:

```text
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/RuntimeMessageWindow.h/.cpp
src/ClawHUD/TrayIcon.h/.cpp
src/ClawHUD/RuntimeControl.h
src/ClawHUD/RuntimeControlWireMapping.h/.cpp
src/ClawHUD/RuntimeControlDispatchBridge.h/.cpp
src/ClawHUD/RuntimeControlPipeServer.h/.cpp
src/ClawHUD/RuntimeControlPipeSecurity.h/.cpp
src/ClawHUD/LaunchMode.h/.cpp
src/ClawHUD/RuntimeLifecyclePolicy.h
src/ClawHUD/SettingsWindow*.cpp/.h
```

Correct comments that incorrectly claim things such as:

```text
Managed does not exist yet
future no-tray mode
Control IPC is read-only
only GetRuntimeInfo / GetSettingsSnapshot are externally available
pipe mutation support is future work
launch mode is always Standalone
startup/update mode policy is still future work
```

Do **not** rewrite comments that are intentionally historical and clearly scoped to an earlier CH-RTF work order.

---

## 5. Audit for stale runtime-to-tray HWND coupling

Production runtime infrastructure must no longer depend on a tray-owned HWND.

Search current production source for any use equivalent to:

```cpp
tray_.Window()
```

The intended runtime HWND authority is:

```cpp
runtimeMessageWindow_.Window()
```

Verify that the following still target `RuntimeMessageWindow` in both modes:

```text
production telemetry binding
GameSessionController message binding
F8 RegisterHotKey / UnregisterHotKey
suspend/resume notifications
production timers
resume-recovery timer
runtime-control dispatch wake
runtime-control shutdown-ready wake
Settings-destroyed asynchronous notification
game-session WM_APP delivery
```

If `tray_.Window()` survives only inside historical documentation describing the pre-CH-RTF-1 state, do not rewrite all history solely to remove the string.

If it survives in current production runtime code, treat that as a blocking architectural regression and fix it.

Do not move any of this infrastructure back into `TrayIcon`.

---

## 6. Audit `SettingsWindow` separation

Verify current production source rather than assuming CH-RTF-3 remains intact.

Required relationship:

```text
SettingsWindow constructor
    -> IRuntimeControl&
    -> narrow onDestroyed callback
```

Required rules:

1. `SettingsWindow` product controls use `IRuntimeControl`.
2. Settings destruction/lifetime notification stays a separate narrow callback.
3. `SettingsWindow` does not import concrete `App` merely to mutate HUD/settings state.
4. Authoritative refresh still comes from `GetSettingsSnapshot()`.
5. F8 or other external state changes can still be reflected by the existing refresh path.
6. Managed mode cannot construct the legacy Settings window because `App::OpenSettings()` is mode-gated.

Do not delete `SettingsWindow` or convert it to IPC internally.

In-process Standalone Settings using `IRuntimeControl` is intentional.

---

## 7. Audit Control IPC boundaries

### 7.1 Pipe server must remain transport-only

`RuntimeControlPipeServer` may own:

```text
pipe endpoint creation
current-user/session security
PIPE_REJECT_REMOTE_CLIENTS
client session validation
bounded read/write
codec invocation
connection lifecycle
post-response shutdown-ready handoff
```

It must not directly own or call:

```text
HudController
HudPresentation
HudSettingsStore
SettingsWindow
GameSessionController
ProductionTelemetryController
PresentMonTelemetryProvider
App setting methods
App::Exit()
```

Every validated protocol operation must continue through `RuntimeControlDispatchBridge`.

### 7.2 Main-thread execution must remain intact

Verify the CH-RTF-5 guarantees still hold:

```text
background producer never executes IRuntimeControl directly
main-thread self-dispatch cannot deadlock
PostMessage failure releases waiter
Stop() releases pending waiters
new dispatch after Stop() fails deterministically
FIFO pending request behavior remains intact
```

Do not introduce a second dispatch mechanism during cleanup.

### 7.3 Protocol v1 must remain unchanged

CH-RTF-10 should not alter:

```text
magic
header size
protocol version
operation IDs
wire enum numeric values
payload layouts
pipe name
payload/frame size limits
status values
UTF-8 rules
opacity percent representation
```

If no protocol defect exists, `src/shared/ClawHudControlProtocol.h` and codec behavior should remain untouched.

---

## 8. Audit launch-mode branch placement

Mode-specific logic should remain concentrated at the shell/lifecycle boundary.

Expected legitimate `LaunchMode` decisions include:

```text
main.cpp argument parsing
App construction / stored immutable mode
tray creation
legacy Settings availability
GetRuntimeInfo metadata
launch-time startup-registration reconciliation
VeloPack restart-after-update policy
mode-aware diagnostic logging
```

Search for launch-mode branches in deeper runtime components.

The following are suspicious and should not exist without a concrete product reason:

```text
HudController
HudPresentation
HudRenderer
ProductionTelemetryController
PresentMonTelemetryProvider
GameSessionController
EC telemetry/helper client
TweakStartupCoordinator
suspend/resume policy
runtime-control codec
pipe security
```

Do not add a new abstraction merely to eliminate a small legitimate `launchMode_` comparison in `App`.

The goal is correct boundary placement, not zero references to `LaunchMode`.

---

## 9. Audit startup/update lifecycle contract

Preserve CH-RTF-9 exactly.

### Standalone

```text
launch
-> reconcile normal Standalone startup shortcut

VeloPack update
-> apply
-> restart=true
-> new ordinary Standalone process
```

### Managed

```text
launch --managed
-> do NOT implicitly create/delete/rewrite Standalone startup shortcut

explicit IPC SetStartWithWindows
-> still allowed to modify the user's normal Standalone shortcut preference

VeloPack update
-> apply
-> restart=false
-> ClawHUD exits
-> future external owner relaunches --managed
```

The startup shortcut must continue to launch ordinary `ClawHUD.exe` without `--managed`.

Do not add a Managed startup shortcut.

Do not add `restartArgs={"--managed"}`.

`EnsurePresentMonRuntime()` must continue to get a normal-launch opportunity after `VelopackApp::Run()` returns, including a future owner-driven Managed relaunch.

---

## 10. Documentation cleanup: distinguish active architecture from historical handoffs

Do not mass-edit or delete the CH-RTF-1 through CH-RTF-9 work orders. They intentionally describe the state at their implementation point and are useful development history.

However, active/top-level handoff documents must not mislead a future developer into implementing a superseded architecture.

### 10.1 `SETTINGS_WINUI3_MIGRATION_HANDOFF_2026-09-02.md`

This document currently presents the old selected direction as:

```text
ClawHUD.exe
├─ tray / message window
├─ runtime
└─ SettingsSession

ClawHUD.Settings.exe
    -> separate WinUI 3 process
```

The runtime/frontend separation program superseded that implementation sequence.

Do **not** delete the document; it preserves useful UI research and discussion.

Add a prominent notice near the top stating, in substance:

```text
SUPERSEDED IMPLEMENTATION SEQUENCE / HISTORICAL REFERENCE

The runtime/frontend separation architecture completed by CH-RTF-1..10 is now
canonical. Do not implement the WinUI 3 phases in this document directly.
Frontend technology remains deferred. Any future standalone frontend must use
the stable runtime-control boundary / Control IPC architecture rather than
reintroducing SettingsSession-era assumptions.
```

Preserve the rest of the historical content unless a tiny wording adjustment is necessary to avoid contradiction with the banner.

### 10.2 Current architecture/PR-plan docs

Review:

```text
docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md
docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_PR_PLAN_2026-09-02.md
```

Do not rewrite them wholesale.

If useful, add a compact completion/handoff note indicating that CH-RTF-1 through CH-RTF-10 are the canonical completed ClawHUD-side foundation and that subsequent work moves to the SteamAddon-side `SA-HUD-*` series.

Do not mark SteamAddon ownership as already implemented.

---

## 11. HUD / VRR production presentation contract — zero tolerance

This cleanup PR must preserve the production HUD presentation contract unchanged.

Do **not** modify, replace, weaken, or work around:

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
premultiplied-alpha presentation contract
```

No cleanup task in this work order requires changing any of those.

If an unexpected regression appears to require a presentation-contract change, stop and report the conflict for explicit design review.

Do not "simplify" HUD window or presentation code while doing final cleanup.

### Background opacity remains background-only

Do not alter opacity semantics.

```text
PreviewHudOpacity
    -> live background-only change
    -> no persistence

CommitHudOpacity
    -> background-only final change
    -> persistence
```

No window-wide or visual-wide opacity.

---

## 12. Required automated verification

Run the repository's normal Release CI-equivalent path:

```text
cmake -S . -B build \
  -G "Visual Studio 18 2026" \
  -A x64 \
  -T v145 \
  -DBUILD_TESTING=ON

cmake --build build --config Release --parallel 2

ctest --test-dir build -C Release --output-on-failure
```

Do not exclude the Control/Managed-related tests from the final validation.

At minimum ensure these focused targets remain registered and passing:

```text
ClawHUD.LaunchModeTests
ClawHUD.ControlProtocolTests
ClawHUD.RuntimeControlDispatchTests
ClawHUD.RuntimeControlPipeServerTests
ClawHUD.HudRendererTests
ClawHUD.HudSettingsStoreTests
```

Also run the rest of the normal CTest suite through the command above.

### Do not invent brittle architecture tests unnecessarily

Do not add a test executable merely to assert source-code strings such as:

```text
"tray_.Window() does not exist"
"read-only comment does not exist"
```

Use code review/search for source-boundary audits and behavioral/unit tests for runtime behavior.

Add a focused automated test only if the audit exposes an actual untested semantic regression worth preserving.

---

## 13. Required manual integration matrix

This is the final ClawHUD-side handoff, so perform a small real-process matrix on supported Claw hardware where possible.

### 13.1 Standalone

Launch:

```text
ClawHUD.exe
```

Verify:

```text
exactly one tray icon
legacy Settings opens
GetRuntimeInfo = Standalone
GetSettingsSnapshot succeeds
IPC mutations succeed and return authoritative state
F8 works
HUD visibility modes work
telemetry still updates
RequestShutdown returns response before process exits
```

### 13.2 Managed

Launch:

```text
ClawHUD.exe --managed
```

Verify:

```text
no tray icon
legacy Settings cannot be opened
GetRuntimeInfo = Managed
same GetSettingsSnapshot values
same IPC mutations
F8 still works
same HUD rendering/telemetry/game detection
tweaks still initialize
RequestShutdown returns response before exit
```

### 13.3 Cross-mode single instance

Verify:

```text
Standalone running + launch --managed
    -> no second runtime
    -> existing Standalone remains unchanged

Managed running + launch normal
    -> no second runtime
    -> existing Managed remains unchanged

Managed running + launch --managed again
    -> no second runtime
```

No automatic mode conversion should occur.

### 13.4 Shared persistence

Verify at least one setting in each direction:

```text
Standalone changes setting
-> exit
-> Managed restores same setting

Managed IPC changes setting
-> exit
-> Standalone restores same setting
```

### 13.5 HUD / VRR safety regression

Using the existing production/manual validation path, confirm both modes preserve:

```text
click-through
no activation
topmost behavior
transparent hit testing
independent flip
premultiplied alpha
production presentation contract
```

Use the same real game where practical when comparing Standalone and Managed so runtime equivalence is meaningful.

---

## 14. What to fix if the audit finds a real regression

Fix concrete violations inside the smallest responsible layer.

Examples:

```text
runtime timer still targets TrayIcon HWND
    -> move target back to RuntimeMessageWindow

pipe worker directly invokes a runtime mutation
    -> route it through RuntimeControlDispatchBridge

Managed accidentally creates legacy Settings
    -> reinforce shell-mode gate

Managed update restarts without --managed ownership
    -> restore CH-RTF-9 restart=false policy

mutation response returns requested instead of authoritative state
    -> restore fresh snapshot response
```

Do not respond to a small regression by introducing a framework.

---

## 15. Explicit out of scope

Do not implement any of the following in CH-RTF-10:

```text
SteamAddon SA-HUD-1..5 implementation
ClawHUD installation discovery from Addon
C# Control client
Addon HUD settings page
Addon-owned process transition
Job Object ownership
Managed crash restart supervision
new ClawHUD update UX
protocol notifications/events
WinUI 3 Settings implementation
replacement standalone frontend
legacy Settings removal
shared helper consolidation
PresentMon redesign
EC redesign
HUD styling/features
HUD presentation modifications
game-detection redesign
```

Those are separate follow-up projects.

---

## 16. Expected change profile

A healthy CH-RTF-10 PR should mostly contain:

```text
small source-comment corrections
small active-document status/supersession corrections
possibly one or two narrowly scoped production fixes only if the audit finds a real contract regression
focused tests only when needed for such a regression
```

It should **not** contain a broad App refactor or large architecture addition.

Do not chase a target LOC count; keep the diff as small as the evidence supports.

---

## 17. Completion checklist

Before opening the PR, confirm all of the following truthfully:

### Runtime/frontend contract

- [ ] Default launch is Standalone.
- [ ] `--managed` is explicit and not persisted.
- [ ] Standalone has tray + legacy Settings.
- [ ] Managed has no tray and cannot create legacy Settings.
- [ ] Both modes share the same runtime implementation.
- [ ] Both modes expose the same Control IPC endpoint.
- [ ] `GetRuntimeInfo.launchMode` is truthful.
- [ ] One cross-mode single-instance mutex remains authoritative.

### Runtime message ownership

- [ ] Production runtime message/timer/hotkey/power paths use `RuntimeMessageWindow`.
- [ ] No production runtime dependency was moved back to a tray-owned HWND.

### Runtime control / IPC

- [ ] Settings uses `IRuntimeControl`, not concrete App product mutations.
- [ ] Pipe worker remains transport-only.
- [ ] All runtime operations still execute through the main-thread dispatch bridge.
- [ ] Protocol v1 wire contract is unchanged.
- [ ] Settings mutations still return authoritative post-mutation snapshots.
- [ ] Opacity preview/commit distinction is preserved.
- [ ] `RequestShutdown` still responds before normal main-thread shutdown.

### Lifecycle

- [ ] Managed launch does not implicitly reconcile Standalone startup registration.
- [ ] Explicit `SetStartWithWindows` still controls the normal Standalone shortcut.
- [ ] Standalone VeloPack update uses restart=true.
- [ ] Managed VeloPack update uses restart=false.
- [ ] PresentMon runtime readiness is checked on ordinary owner-driven Managed relaunch.

### Documentation

- [ ] Stale current-source CH-RTF comments are corrected.
- [ ] Historical work orders remain historical rather than being rewritten as current state.
- [ ] `SETTINGS_WINUI3_MIGRATION_HANDOFF_2026-09-02.md` is clearly marked as a superseded implementation sequence / historical reference.
- [ ] Current runtime/frontend architecture remains the canonical handoff.

### HUD / VRR

- [ ] `ProductionHudPresentationContract()` values are unchanged.
- [ ] HUD `windowExStyle` is unchanged.
- [ ] `WS_EX_TRANSPARENT` is unchanged.
- [ ] `WS_EX_NOACTIVATE` is unchanged.
- [ ] `WS_EX_TOPMOST` is unchanged.
- [ ] existing `WS_EX_LAYERED` behavior is unchanged.
- [ ] `WM_NCHITTEST -> HTTRANSPARENT` is unchanged.
- [ ] `WM_MOUSEACTIVATE -> MA_NOACTIVATE` is unchanged.
- [ ] independent-flip requirement is unchanged.
- [ ] production Presentation API / DirectComposition path is unchanged.
- [ ] premultiplied-alpha contract is unchanged.
- [ ] Background Opacity remains background-only.

### Verification

- [ ] Release x64 build succeeds.
- [ ] Full normal Release CTest passes.
- [ ] Standalone manual smoke test passes.
- [ ] Managed manual smoke test passes.
- [ ] Cross-mode single-instance behavior passes.
- [ ] Shared persistence between modes passes.
- [ ] HUD/VRR presentation safety checks pass.

---

## 18. Handoff after CH-RTF-10

After this PR is merged, the ClawHUD side of runtime/frontend separation is considered complete enough for independent external integration work.

The next implementation sequence moves to SteamAddonforClaw:

```text
SA-HUD-1  Installation and compatibility discovery
SA-HUD-2  Control IPC client + read-only status/snapshot
SA-HUD-3  HUD settings mutation UI
SA-HUD-4  integration process ownership / Standalone -> Managed transition
SA-HUD-5  boot/crash/update/uninstall lifecycle hardening
```

Do not pre-implement those concerns in ClawHUD during CH-RTF-10.

The stable ClawHUD handoff surface is:

```text
installed ClawHUD.exe
explicit --managed launch mode
one per-session runtime authority
versioned local Control Named Pipe
GetRuntimeInfo
GetSettingsSnapshot
protocol-v1 settings mutations
RequestShutdown
```

That is the boundary the external owner should consume.
