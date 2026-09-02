# CH-RTF-8 — Standalone / Managed Launch Composition Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** ClawHUD Runtime / Frontend Separation  
> **Architecture source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md`  
> **PR plan source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_PR_PLAN_2026-09-02.md`  
> **Previous PRs:** #209 CH-RTF-1, #210 CH-RTF-2, #211 CH-RTF-3, #212 CH-RTF-4, #213 CH-RTF-5, #214 CH-RTF-6, #215 CH-RTF-7  
> **Analyzed main HEAD:** `e32ba41b5fb55798775c11141afe338f989d284e`  
> **Scope:** Add explicit `Standalone` / `Managed` launch mode parsing and compose only the shell-level tray / legacy Settings surface by mode while keeping one identical runtime implementation  
> **Status:** Ready for implementation

---

## 1. Objective

Introduce the first real launch-mode distinction in ClawHUD:

```text
ClawHUD.exe
    -> Standalone

ClawHUD.exe --managed
    -> Managed
```

The mode changes **only shell composition**.

The production runtime remains one implementation in both modes:

```text
RuntimeMessageWindow
HudController / HudPresentation
PresentMonTelemetryProvider
ProductionTelemetryController
GameSessionController
HudSettingsStore
TweakStartupCoordinator
Control dispatch bridge
Control Named Pipe server
F8 hotkey
suspend/resume handling
production timers
```

The only intended user-visible composition difference in CH-RTF-8 is:

```text
Standalone
    -> runtime ON
    -> Control IPC ON
    -> tray ON
    -> legacy Win32 Settings available through tray

Managed
    -> same runtime ON
    -> same Control IPC ON
    -> tray OFF
    -> legacy Win32 Settings unavailable
```

This PR must **not** create a second runtime implementation, a separate renderer, or a special Managed telemetry/game-detection path.

---

## 2. Current production baseline after PR #215

### 2.1 `main.cpp` still ignores launch arguments

Current entry point:

```cpp
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    Velopack::VelopackApp::Build()
        ...
        .Run();

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return 1;

    App app(instance);
    return app.Run();
}
```

There is currently no launch-mode semantic type and no `--managed` parser.

### 2.2 `App::Run()` already has a tray-independent runtime

The important CH-RTF-1 through CH-RTF-7 work is complete.

`RuntimeMessageWindow` is created before runtime components are bound and owns the hidden HWND needed for:

```text
F8 WM_HOTKEY
WM_POWERBROADCAST
production WM_TIMER delivery
resume recovery timer
runtime-control dispatch wake
runtime-control shutdown-ready wake
game-session message target
```

Telemetry and game-session binding already use:

```cpp
runtimeMessageWindow_.Window()
```

rather than the tray HWND.

Therefore Managed mode does **not** need a replacement hidden window.

### 2.3 Tray is now shell-only

`TrayIcon` already receives only narrow shell actions:

```text
OpenSettings
Exit
```

`TrayIcon` no longer owns the runtime message infrastructure.

Current `App::Run()` still always does:

```cpp
if (!tray_.Create(instance_))
    return 1;
```

This is the primary composition point CH-RTF-8 must make conditional.

### 2.4 Legacy Settings is already a runtime-control client

`SettingsWindow` no longer depends on concrete `App` for product settings. It uses:

```cpp
IRuntimeControl&
```

plus a narrow destruction callback.

Current `App::OpenSettings()` lazily creates the Win32 Settings window.

In Managed mode, the Settings object must remain uncreated.

### 2.5 Control IPC is already complete

The process-lifetime secure Named Pipe endpoint is already available at:

```text
\\.\pipe\ClawHUD.Control.<sessionId>
```

CH-RTF-7 exposes:

```text
GetRuntimeInfo
GetSettingsSnapshot
all protocol-v1 settings mutations
RequestShutdown
```

All mutation execution already reaches `IRuntimeControl` on the main thread through `RuntimeControlDispatchBridge`.

Do not add a second Managed-only endpoint or a mode suffix to the pipe name.

### 2.6 Runtime metadata is waiting for CH-RTF-8

`RuntimeControlMetadata` already contains:

```cpp
control::WireLaunchMode launchMode{control::WireLaunchMode::Standalone};
```

and prior PRs deliberately always report Standalone until real launch composition exists.

CH-RTF-8 must make this value truthful.

---

## 3. Non-negotiable launch-mode rules

### 3.1 Default is always Standalone

The default process invocation is permanently:

```text
ClawHUD.exe -> Standalone
```

Managed mode exists **only** when explicitly requested with the supported command-line token:

```text
--managed
```

Do not infer Managed mode from:

```text
SteamAddon installation
SteamAddon process existence
parent process identity
Named Pipe clients
registry state
settings.ini
environment variables
installation path
startup shortcut
previous launch mode
```

There must be no `SteamAddonInstalled()` / `SteamAddonRunning()` detection in ClawHUD.

### 3.2 Mode is process-lifetime state only

Do not persist launch mode.

Do not add it to:

```text
settings.ini
HudSettingsStore
registry
startup shortcut metadata
```

A new process resolves its mode from its own command line every time.

### 3.3 One runtime implementation

Do not create:

```text
ManagedApp
StandaloneApp
ManagedHudController
ManagedTelemetryController
ManagedGameSessionController
ManagedPresentation
```

`App` remains the composition root for the one runtime.

### 3.4 One runtime per session remains unchanged

Keep the existing mutex:

```text
Local\ClawHUD.SingleInstance
```

Do not create one mutex per mode.

The required CH-RTF-8 behavior is:

```text
Standalone already running
    + ClawHUD.exe --managed
    -> second process fails existing single-instance acquisition and exits

Managed already running
    + ClawHUD.exe
    -> second process fails existing single-instance acquisition and exits

Managed already running
    + another ClawHUD.exe --managed
    -> second process exits
```

CH-RTF-8 does not automatically replace a running Standalone process with Managed.

The future external owner performs:

```text
inspect existing runtime mode through IPC
-> RequestShutdown Standalone if transition is required
-> wait for exit
-> launch ClawHUD.exe --managed
```

That owner behavior belongs to SteamAddon follow-up work, not this PR.

---

## 4. Add a small semantic launch-mode type

Add a process-internal semantic type under `src/ClawHUD`.

Recommended file:

```text
src/ClawHUD/LaunchMode.h
```

Recommended semantic shape:

```cpp
namespace clawhud
{
enum class LaunchMode
{
    Standalone,
    Managed,
};
}
```

This is **not** the wire enum.

Do not use `control::WireLaunchMode` as the application's composition state merely because the values already exist in the protocol.

The internal process mode and the protocol representation have different responsibilities.

An explicit mapping to `WireLaunchMode` may live near App/runtime metadata creation.

---

## 5. Add a small pure launch-mode resolver

Add a narrow pure helper so launch parsing can be tested without starting `App`, Velopack, the tray, or a real HWND.

Recommended file names:

```text
src/ClawHUD/LaunchMode.h
src/ClawHUD/LaunchMode.cpp
```

or an equivalent small pair.

Conceptually:

```cpp
LaunchMode ResolveLaunchMode(std::span<const std::wstring_view> arguments) noexcept;
```

Required rules:

```text
no --managed token
    -> Standalone

exact --managed token present
    -> Managed
```

The parser must not invent additional aliases in this PR.

Do not add:

```text
/managed
-managed
--background
--headless
--no-tray
--addon
```

unless a later product decision explicitly requires them.

Unknown arguments must not implicitly enable Managed mode.

Use the real Windows command-line tokenization path in `main.cpp` where practical, for example `CommandLineToArgvW`, and feed only the resulting argument tokens to the pure resolver.

Do not write a bespoke quote/escape parser.

### Expected pure tests

At minimum:

```text
[]                                  -> Standalone
["--managed"]                       -> Managed
["--unknown"]                       -> Standalone
["--unknown", "--managed"]          -> Managed
["--managed", "--unknown"]          -> Managed
```

If the implementation chooses exact case-sensitive matching, test it and document it. Do not add fuzzy matching.

---

## 6. Pass launch mode explicitly into `App`

Change App construction conceptually from:

```cpp
App app(instance);
```

to:

```cpp
App app(instance, launchMode);
```

Recommended constructor:

```cpp
App(HINSTANCE instance, clawhud::LaunchMode launchMode);
```

Store it as immutable process-lifetime composition state, conceptually:

```cpp
const clawhud::LaunchMode launchMode_;
```

or an equivalently non-mutated member.

Do not add a runtime setter such as:

```cpp
SetLaunchMode(...)
```

Mode changes require process replacement, not in-process shell mutation.

### Logging

Include the resolved mode in startup logging so lifecycle tests are diagnosable:

```text
launchMode=Standalone
```

or:

```text
launchMode=Managed
```

Do not log pipe payloads or sensitive user/session data just to support this.

---

## 7. Compose the tray only in Standalone mode

Current code always performs:

```cpp
tray_.Create(instance_)
```

Change the shell composition so:

```text
Standalone
    -> tray_.Create(instance_)

Managed
    -> do not call tray_.Create(...)
```

Recommended shape:

```cpp
if (launchMode_ == clawhud::LaunchMode::Standalone)
{
    if (!tray_.Create(instance_))
    {
        ...
        return 1;
    }
}
```

Do **not** move runtime initialization under this branch.

These must still run in Managed mode:

```text
RuntimeMessageWindow creation
productionTelemetry_.Bind
GameSessionController BindMessageWindow
window-source start
Steam watcher start
DebugObservationController when enabled
F8 RegisterHotKey
PresentMon provider initialize
foreground tracking
persisted HUD restore
TweakStartupCoordinator
RuntimeControlDispatchBridge
RuntimeControlPipeServer
message loop
```

### Do not over-refactor `TrayIcon`

It is acceptable for `App` to continue owning a `TrayIcon tray_` member even when Managed mode never calls `Create()`.

Do not convert it to:

```text
unique_ptr<TrayIcon>
optional<TrayIcon>
DI-resolved shell service
```

unless an actual implementation requirement appears.

The existing no-op-safe `Destroy()` path is sufficient if verified.

The objective is launch composition, not containerization of shell objects.

---

## 8. Legacy Settings must be unavailable in Managed mode

Managed mode has no standalone shell frontend.

The absence of the tray removes the normal Settings entry point, but enforce the invariant at `App::OpenSettings()` as well so a future accidental internal call cannot create the legacy window in Managed mode.

Conceptually:

```cpp
void App::OpenSettings()
{
    if (launchMode_ != clawhud::LaunchMode::Standalone)
        return;

    ... existing lazy Settings creation ...
}
```

A debug log for the ignored call is acceptable.

Do not show an error dialog in Managed mode merely because `OpenSettings()` was rejected.

Required invariant:

```text
Managed lifetime
    -> settings_ remains null
```

Do not delete `SettingsWindow` in this PR.

Standalone behavior must remain exactly as today:

```text
tray click/menu
-> OpenSettings
-> lazy SettingsWindow
-> IRuntimeControl
-> async WM_NCDESTROY handoff
```

---

## 9. `GetRuntimeInfo` must report the real launch mode

The Control pipe exists in both modes and is how a future external owner determines which runtime currently owns the session.

When building `RuntimeControlMetadata` for dispatch, explicitly map:

```text
LaunchMode::Standalone -> WireLaunchMode::Standalone
LaunchMode::Managed    -> WireLaunchMode::Managed
```

Do not use ordinal casts between unrelated enums.

Conceptually:

```cpp
metadata.launchMode = launchMode_ == clawhud::LaunchMode::Managed
    ? clawhud::control::WireLaunchMode::Managed
    : clawhud::control::WireLaunchMode::Standalone;
```

or a small explicit switch helper.

Required externally observable behavior:

```text
ClawHUD.exe
    -> GetRuntimeInfo.launchMode = Standalone

ClawHUD.exe --managed
    -> GetRuntimeInfo.launchMode = Managed
```

Keep:

```text
protocol version = 1..1
runtimeState = Ready
```

unchanged in this PR.

Do not add new wire fields or bump protocol version.

---

## 10. F8, power, timers, game detection, telemetry, HUD and tweaks are mode-independent

This is the central regression requirement.

Managed must continue to register F8 on:

```cpp
runtimeMessageWindow_.Window()
```

It must continue receiving:

```text
WM_POWERBROADCAST
WM_TIMER
runtime-control WM_APP messages
game-session WM_APP messages
```

Do not add branches such as:

```cpp
if (launchMode_ == Managed)
    SkipPresentMon();
```

or:

```cpp
if (launchMode_ == Managed)
    DisableGameDetection();
```

or any other runtime divergence.

A correct CH-RTF-8 diff should show mode branching concentrated near:

```text
main / launch parsing
App composition
tray creation
legacy Settings opening
runtime-info metadata
```

Mode branches deep inside renderer/telemetry/game-detection code are a design error.

---

## 11. Settings authority and persistence remain shared

Both modes use the same:

```text
HudSettingsStore
settings.ini
IRuntimeControl mutation semantics
```

Managed is not a temporary or stateless runtime.

For example:

```text
Standalone: set HUD size +1
-> exit
-> launch --managed
-> Managed restores +1
```

and:

```text
Managed IPC: set alignment Right
-> exit
-> normal launch
-> Standalone restores Right
```

Do not create:

```text
managed-settings.ini
mode-specific settings sections
mode-specific defaults
mode-specific HUD state
```

---

## 12. `Start with Windows` is not redesigned in CH-RTF-8

This is an explicit scope boundary.

Current `App::Run()` always performs startup-registration reconciliation through:

```cpp
ApplyStartupRegistration()
```

and current `SetStartWithWindows()` directly applies/removes the normal ClawHUD startup shortcut.

The PR plan deliberately assigns mode-aware startup ownership to **CH-RTF-9**.

Therefore CH-RTF-8 must not opportunistically redesign:

```text
ApplyStartupRegistration call timing
startup shortcut ownership
Managed startup reconciliation policy
SetStartWithWindows semantics
```

Also do not add `--managed` to the existing startup shortcut.

The existing shortcut continues to mean ordinary Standalone launch.

CH-RTF-9 will implement the settled rule:

```text
Standalone
    -> owns/reconciles Standalone startup registration

Managed
    -> must not rewrite Standalone startup registration merely because an external owner launched it
```

Keep that follow-up visible in comments/work-order handoff, but do not mix it into CH-RTF-8.

---

## 13. Velopack update lifecycle is not redesigned in CH-RTF-8

Current `CheckForUpdates()` may call:

```cpp
WaitExitThenApplyUpdates(..., true, true)
```

The actual mode-preserving update/restart behavior must be handled in CH-RTF-9 after validating the real Velopack API semantics.

Do not guess that Velopack automatically preserves `--managed`.

Do not add speculative update restart arguments in this PR.

Do not fork the updater into Standalone/Managed implementations here.

CH-RTF-9 owns the settled lifecycle:

```text
Standalone update
    -> preserve normal update/restart behavior

Managed update
    -> must not accidentally relaunch Standalone
    -> external owner ultimately relaunches --managed
```

---

## 14. Single-instance behavior is preserved, not expanded

Keep the current acquisition point and mutex behavior.

Do not add:

```text
mode negotiation over mutex
activation forwarding
secondary-instance pipe commands
automatic Standalone -> Managed conversion
automatic Managed -> Standalone conversion
```

The currently running runtime remains authoritative until explicitly shut down.

This is important for the later SteamAddon reconciliation sequence.

The owner can determine current mode through `GetRuntimeInfo` and request graceful shutdown through the CH-RTF-7 pipe.

---

## 15. Production HUD / VRR safety contract — unchanged and mandatory

This launch-composition PR must not modify, replace, weaken, or work around any of:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- existing `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirement;
- existing Presentation API / DirectComposition production presentation path;
- premultiplied-alpha presentation contract.

Do not change the renderer or presentation path merely because Managed has no tray.

`Background Opacity` remains background-only.

Existing tests/assertions for:

```text
click-through
no activation
topmost
transparent hit testing
independent flip
premultiplied alpha
production presentation contract
```

must remain intact.

---

## 16. Expected primary code areas

Likely files:

```text
src/ClawHUD/main.cpp
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/LaunchMode.h             [new]
src/ClawHUD/LaunchMode.cpp           [new, if needed]
CMakeLists.txt
cmake/ClawHUDTests.cmake
```

A focused test file is recommended:

```text
tests/LaunchModeTests.cpp
```

Do not touch HUD presentation files unless required only for a build include dependency; functional changes there are out of scope.

---

## 17. Tests — required

### 17.1 Pure launch parser tests

At minimum verify:

```text
no arguments -> Standalone
--managed -> Managed
unknown argument only -> Standalone
unknown + --managed -> Managed
--managed + unknown -> Managed
```

Also verify the parser does not depend on:

```text
settings
registry
running processes
filesystem state
```

### 17.2 Composition policy tests

If a small pure helper is introduced for shell composition, verify:

```text
Standalone -> tray enabled, standalone Settings enabled
Managed    -> tray disabled, standalone Settings disabled
```

Do not create a broad service-container test framework solely for this.

### 17.3 Runtime metadata mapping

Add or extend a focused pure test proving:

```text
Standalone semantic mode -> WireLaunchMode::Standalone
Managed semantic mode    -> WireLaunchMode::Managed
```

No ordinal cast assumption.

### 17.4 Existing Control tests

Existing CH-RTF-4 through CH-RTF-7 tests must continue to pass:

```text
codec
runtime-control mapping
dispatch bridge
secure pipe server
external mutations
RequestShutdown response-before-exit
```

Do not weaken prior tests to accommodate launch mode.

### 17.5 Full existing suite

Run the normal repository test suite in the same manner as current CI.

Build Debug and Release if that remains the established local verification flow.

---

## 18. Manual / process validation matrix — required

Because tray presence and one-process behavior are OS shell/process behaviors, validate them with the real executable in addition to pure tests.

### 18.1 Standalone

Launch:

```text
ClawHUD.exe
```

Verify:

```text
exactly one ClawHUD tray icon appears
tray Settings opens legacy Win32 Settings
tray Exit follows normal shutdown
GetRuntimeInfo reports Standalone
Control IPC remains functional
F8 still toggles HUD
HUD/game detection/telemetry behave as before
```

### 18.2 Managed

Launch:

```text
ClawHUD.exe --managed
```

Verify:

```text
no ClawHUD tray icon is created
legacy Win32 Settings is not created
GetRuntimeInfo reports Managed
same Control pipe endpoint is reachable
GetSettingsSnapshot works
settings mutations work
RequestShutdown works
F8 still works
HUD restore works
game detection works
PresentMon telemetry works
EC/system/battery telemetry works
Intel VRR startup preference behavior is unchanged
```

### 18.3 Cross-mode single instance

Verify:

```text
Managed running + normal launch
    -> second process exits; Managed stays Managed

Standalone running + --managed launch
    -> second process exits; Standalone stays Standalone

Managed running + second --managed launch
    -> second process exits
```

Do not expect automatic mode conversion in this PR.

### 18.4 Shared settings parity

Verify at least one persisted setting both directions:

```text
Standalone mutation -> Managed observes/restores it
Managed IPC mutation -> Standalone observes/restores it
```

---

## 19. Explicitly out of scope

Do **not** implement any of the following in CH-RTF-8:

```text
SteamAddon installation detection
SteamAddon process detection
SteamAddon IPC client
Integration Enabled persistence
Job Object ownership
Addon crash -> Managed kill behavior
Managed crash restart policy
Standalone -> Managed automatic conversion
Managed -> Standalone automatic conversion
mode-aware startup registration hardening
mode-aware Velopack update/restart hardening
new single-instance negotiation/forwarding
new standalone frontend
WinUI3/WPF/Web settings migration
legacy Settings deletion
StateChanged/event bus
multiple concurrent pipe clients
persistent multi-request pipe sessions
shared EC helper
PresentMon redesign
game-detection redesign
HUD renderer/presentation changes
```

The next ClawHUD PR, **CH-RTF-9**, owns mode-aware startup/update/single-instance lifecycle hardening.

SteamAddon ownership and Job Object work remain in the later `SA-HUD-*` series.

---

## 20. Acceptance checklist

The PR is complete only when all of these are true:

1. `ClawHUD.exe` resolves to Standalone by default.
2. Only explicit `--managed` resolves to Managed.
3. Launch mode is not persisted.
4. `App` receives launch mode explicitly at construction.
5. Managed mode does not call `TrayIcon::Create()`.
6. Standalone tray behavior remains unchanged.
7. Managed mode cannot create legacy `SettingsWindow` through `OpenSettings()`.
8. RuntimeMessageWindow exists in both modes.
9. F8 remains registered in both modes.
10. suspend/resume and timer delivery remain available in both modes.
11. PresentMon/EC/system/battery telemetry is unchanged between modes.
12. game detection is unchanged between modes.
13. HudController/HudPresentation is the exact same implementation in both modes.
14. tweak startup behavior is unchanged between modes.
15. Control IPC uses the same existing secure per-session endpoint in both modes.
16. `GetRuntimeInfo` truthfully reports Standalone vs Managed.
17. settings persistence is shared across modes.
18. the existing single-instance mutex remains one authority per session.
19. a second launch never changes the mode of the already running instance.
20. startup registration behavior is not redesigned in this PR.
21. Velopack update behavior is not guessed/redesigned in this PR.
22. no SteamAddon detection is added.
23. no mode branches appear inside HUD presentation/telemetry/game-detection implementation layers.
24. existing CH-RTF-4 through CH-RTF-7 protocol/dispatch/pipe tests remain passing.
25. existing HUD/VRR contract tests/assertions remain passing.
26. full CI is green.

---

## 21. Review focus for PR review

Review this PR primarily for:

### Blocking

```text
Managed still creates a tray
Managed can still create legacy Settings
RuntimeMessageWindow/runtime infrastructure accidentally becomes conditional on tray
GetRuntimeInfo reports wrong mode
mode persisted to settings/registry
separate mode-specific runtime/renderer/telemetry path introduced
single-instance mutex split by mode
SteamAddon auto-detection added
existing Control IPC disabled in Managed
F8/power/timers disabled in Managed
HUD/VRR presentation contract modified
```

### Non-blocking

```text
minor naming/style improvements
additional harmless launch-mode logging
small parser-test organization preferences
```

Do not block CH-RTF-8 for lifecycle work explicitly assigned to CH-RTF-9 unless the CH-RTF-8 implementation makes that later hardening impossible or introduces a materially broken normal execution path.

---

## 22. Handoff to CH-RTF-9

After CH-RTF-8, the architecture should be:

```text
main.cpp
    -> resolve LaunchMode
    -> Velopack/bootstrap
    -> App(instance, launchMode)

App
    ├─ LaunchMode                 process-lifetime composition state
    ├─ RuntimeMessageWindow       always
    ├─ RuntimeControlDispatch     always
    ├─ RuntimeControlPipeServer   always
    ├─ HudController              always / unchanged
    ├─ PresentMon provider        always / unchanged
    ├─ ProductionTelemetry        always / unchanged
    ├─ GameSessionController      always / unchanged
    ├─ HudSettingsStore           always / shared
    ├─ TweakStartupCoordinator    always / unchanged
    ├─ TrayIcon                   Create only in Standalone
    └─ legacy SettingsWindow      Standalone only / lazy
```

Externally:

```text
ClawHUD.exe
    -> GetRuntimeInfo = Standalone
    -> tray exists

ClawHUD.exe --managed
    -> GetRuntimeInfo = Managed
    -> no tray
    -> same Control IPC
```

CH-RTF-9 can then safely focus on the remaining lifecycle edges without touching renderer/frontend separation:

```text
Managed startup registration ownership
Managed Velopack update/exit semantics
mode-aware lifecycle reconciliation
single-instance lifecycle edge validation
```
