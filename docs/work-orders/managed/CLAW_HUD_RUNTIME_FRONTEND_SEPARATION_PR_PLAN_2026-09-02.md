# ClawHUD Runtime / Frontend Separation — Detailed PR Plan

> **Completion note (CH-RTF-10):** the ClawHUD PR series planned below is
> complete — CH-RTF-1..10 merged as PRs #209–#217 + this PR. The `SA-HUD-*` follow-up
> series belongs to the SteamAddonforClaw repository and is not implemented in
> ClawHUD.

> **Plan date:** 2026-09-02  
> **Repository:** `onehoon/ClawHUD`  
> **Analyzed main HEAD:** `6c3ef9c4618dd281482e4dfe938691fda388cdeb`  
> **Architecture source:** `docs/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md`  
> **Status:** Complete (CH-RTF-1..10). SA-HUD-* remains SteamAddon-side follow-up.  
> **Planning constraint:** keep each implementation PR comfortably below roughly 500 changed LOC. This is a PR-design constraint used to choose boundaries; individual future work orders do not need to restate it.

---

## 1. Objective

The next development series must convert the current ClawHUD application shell into a runtime-first architecture without changing the production HUD implementation.

The end state is:

```text
ClawHUD.exe
├─ one native ClawHUD runtime
│  ├─ HudController
│  │  └─ HudPresentation
│  ├─ PresentMonTelemetryProvider
│  ├─ ProductionTelemetryController
│  ├─ GameSessionController
│  ├─ HudSettingsStore
│  ├─ TweakStartupCoordinator
│  ├─ EC / battery / system telemetry
│  └─ stable local Control IPC
│
└─ launch-mode shell
   ├─ Standalone (default)
   │  ├─ runtime message window
   │  ├─ Tray
   │  └─ standalone Settings frontend entry point
   │
   └─ Managed (`--managed`)
      ├─ same runtime message window
      ├─ same HUD / telemetry / game detection / settings
      └─ no Tray
```

The same Control IPC must later support:

```text
legacy Win32 Settings during migration
future standalone frontend (Win32 / WPF / WinUI3 / Web)
SteamAddonforClaw HUD page
```

This series is **not** a renderer rewrite, game-detection redesign, PresentMon redesign, or UI-framework migration.

---

## 2. Latest-code findings that determine the PR order

The PR sequence below is based on the current `main` production implementation, not the older pre-refactor structure.

### 2.1 `App` is still the valid composition root

The completed application refactor already separated the important production domains.

Current `App` directly owns:

```text
HudSettingsStore
TrayIcon
HudController
PresentMonTelemetryProvider
ProductionTelemetryController
GameSessionController
DebugObservationController (lazy)
SettingsWindow (lazy)
TweakStartupCoordinator
```

This is not a reason to introduce another generic `RuntimeHost`, DI container, plugin framework, or broad application framework.

The new work should make a narrow runtime/frontend boundary while keeping `App` as the top-level composition/mediation point unless a later concrete extraction is required by code ownership.

### 2.2 The current `TrayIcon` is not merely a tray icon

This is the most important current-code constraint.

`TrayIcon::Create()` currently creates the hidden window class:

```text
ClawHUD.TrayMessageWindow
```

That HWND currently owns or receives all of the following:

```text
WM_HOTKEY / F8 HUD toggle
WM_POWERBROADCAST suspend/resume
WM_TIMER for production telemetry and resume recovery
Steam/game-session WM_APP delivery target through App binding
Settings-destroyed private App message
TaskbarCreated handling
tray icon callbacks and menu commands
```

`App::Run()` then binds runtime components directly to `tray_.Window()`:

```text
ProductionTelemetryController::Bind(tray_.Window(), ...)
GameSessionController::BindMessageWindow(tray_.Window())
RegisterHotKey(tray_.Window(), ...)
```

`App::HandleSystemResume()` and retry paths also call `SetTimer(tray_.Window(), ...)`.

Therefore **Managed mode cannot simply skip `tray_.Create()`**. Doing that today would remove the runtime's message/timer/power/hotkey infrastructure together with the visual tray icon.

The first implementation boundary must separate:

```text
RuntimeMessageWindow
    from
TrayIcon / tray menu
```

before Managed mode is attempted.

### 2.3 Current Settings is already lazy but coupled to concrete `App`

`SettingsWindow` currently receives:

```cpp
SettingsWindow(App& app)
```

and calls the current public App Settings facade directly.

On `WM_NCDESTROY`, Settings performs an asynchronous destruction notification:

```text
SettingsWindow
    -> App::PostSettingsDestroyed()
    -> PostMessage(current tray HWND, private message)
    -> App message pump
    -> App::SettingsDestroyed()
    -> settings_.reset()
```

This asynchronous destruction behavior is intentional and must remain safe while the boundary changes.

The existing Win32 UI is valuable as the first runtime-control client because it can verify the new boundary without simultaneously changing UI technology.

### 2.4 Runtime settings semantics already exist and should be reused

The current public App facade already exposes almost the exact operations needed by an external frontend:

```text
StartWithWindows / SetStartWithWindows
HudEnabled / SetHudEnabled
HudOptions
HudFont / SetHudFont
SetHudAlignment
SetHudBackgroundMode
SetHudOpacity
SetHudSizeOffset
SetHudVisibilityMode
IntelVrrRangeFixEnabled / SetIntelVrrRangeFixEnabled
IntelVrrLastResult
```

Several of these are not simple field assignments. They can recreate presentation state, drive game-session/telemetry changes, persist settings, or roll back on failure.

The IPC must call these existing product semantics rather than independently editing `settings.ini`.

### 2.5 Opacity has existing preview/commit semantics

The legacy Settings slider already distinguishes live tracking from persistence:

```text
thumb tracking
    -> apply opacity live
    -> do not persist every movement

tracking finished
    -> persist final value
```

The runtime-control contract must preserve this as explicit preview/commit behavior.

### 2.6 Startup/update behavior is currently Standalone-oriented

`main.cpp` currently ignores the command line and creates `App(instance)` with no launch-mode concept.

`App::Run()` currently does, in order:

```text
AcquireSingleInstance
CheckForUpdates
hardware support check
ApplyStartupRegistration
TrayIcon::Create
bind runtime components to tray HWND
start game/Steam/debug/PresentMon sources
register F8
restore HUD
start tweak coordinator
message loop
```

`CheckForUpdates()` currently applies a pending Velopack update through:

```text
WaitExitThenApplyUpdates(... restart enabled ...)
```

and then exits the process.

Managed mode therefore needs explicit policy around:

```text
startup registration
pending update restart
tray creation
launch arguments
```

rather than just an `if (!managed) ShowTray()` at the end.

### 2.7 Current single-instance behavior is simple and useful

The current mutex is:

```text
Local\ClawHUD.SingleInstance
```

A second process currently exits rather than forwarding activation.

This already enforces the critical one-runtime-per-session rule. The early Managed implementation should preserve that property and add only the minimum semantics required by the agreed lifecycle.

### 2.8 Build structure is already adequate

Production is still one native CMake `ClawHUD` target, with the helper and diagnostics as separate targets. Tests are partly in root `CMakeLists.txt` and mostly in `cmake/ClawHUDTests.cmake`.

No build-system redesign is required for runtime separation.

New native sources and focused test targets can be added to the existing CMake structure.

---

## 3. Non-negotiable constraints for every PR

Every PR in this series must preserve the production HUD/VRR contract unchanged.

Do not modify, replace, weaken, or work around:

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

Also preserve:

```text
one production HudController/HudPresentation implementation
one PresentMonTelemetryProvider authority
current game-detection semantics
current EC telemetry path
current tweak behavior
current settings persistence semantics
current suspend/resume recovery semantics
```

The architectural work belongs above those domains.

---

## 4. PR series overview

Recommended ClawHUD implementation sequence:

```text
CH-RTF-1  Extract runtime message window from TrayIcon
CH-RTF-2  Decouple tray shell callbacks from concrete App
CH-RTF-3  Introduce runtime-control interface and migrate legacy Settings
CH-RTF-4  Define versioned Control IPC wire protocol and codecs
CH-RTF-5  Add main-thread runtime-control dispatch bridge
CH-RTF-6  Add secure read-only named-pipe Control server
CH-RTF-7  Add settings mutations, opacity commit semantics, and shutdown IPC
CH-RTF-8  Add Standalone / Managed launch composition
CH-RTF-9  Harden mode-aware startup/update/single-instance lifecycle
CH-RTF-10 Final integration-contract regression/cleanup pass
```

Then SteamAddonforClaw can implement its side independently:

```text
SA-HUD-1  ClawHUD installation/compatibility discovery
SA-HUD-2  ClawHUD IPC client + read-only status/snapshot
SA-HUD-3  HUD Settings page mutations
SA-HUD-4  Integration ON/OFF process ownership and mode transition
SA-HUD-5  Boot/crash/update/uninstall lifecycle hardening
```

The Addon sequence is included here to prove that the ClawHUD contract is sufficient; its work orders should live in the SteamAddon repository when implementation begins.

---

# 5. ClawHUD PR details

## CH-RTF-1 — Extract `RuntimeMessageWindow` from `TrayIcon`

### Purpose

Create the infrastructure Managed mode will need **without changing any user-visible behavior**.

Today the hidden HWND and the system tray icon are one object. Split them so the runtime message infrastructure exists independently of whether a tray icon is shown.

### Primary code areas

```text
src/ClawHUD/TrayIcon.h
src/ClawHUD/TrayIcon.cpp
src/ClawHUD/App.h
src/ClawHUD/App.cpp
CMakeLists.txt
cmake/ClawHUDTests.cmake (if a focused test target is added)
```

New files conceptually:

```text
src/ClawHUD/RuntimeMessageWindow.h
src/ClawHUD/RuntimeMessageWindow.cpp
```

### Target ownership

`RuntimeMessageWindow` should own:

```text
hidden HWND creation/destruction
window class / WindowProc
suspend/resume notification registration
WM_POWERBROADCAST delivery
WM_HOTKEY delivery
WM_TIMER delivery
private App/runtime WM_APP dispatch entry point
```

`TrayIcon` should retain only:

```text
Shell_NotifyIcon state
TaskbarCreated tray restoration
tray menu
Settings/Exit user commands
```

The visual tray can use the runtime HWND as its notification HWND; it does not need to own that HWND.

### App changes

All current runtime uses of:

```cpp
tray_.Window()
```

must move to:

```text
runtimeMessageWindow.Window()
```

including:

- telemetry binding;
- game-session message binding;
- F8 registration/unregistration;
- resume retry timers;
- asynchronous Settings-destroyed notification.

### Must not change

- whether the tray appears;
- tray click/menu behavior;
- F8 behavior;
- suspend/resume behavior;
- timer IDs or semantics;
- game-session WM_APP IDs;
- Settings lifetime;
- HUD renderer/presentation.

### Verification focus

- normal launch still creates exactly one tray icon;
- F8 still works;
- suspend/resume callbacks still reach App;
- telemetry/game-session timers/messages still arrive;
- closing Settings still asynchronously releases `SettingsWindow`;
- Taskbar restart still restores the tray icon.

### Planning size

Approximately 250–400 changed LOC depending on test factoring.

---

## CH-RTF-2 — Decouple `TrayIcon` shell callbacks from concrete `App`

### Purpose

Make `TrayIcon` a shell component rather than an object that imports and controls the whole application.

This PR continues the shell cleanup but remains behavior-preserving.

### Current coupling

`TrayIcon` currently stores:

```cpp
App& app_;
```

and directly invokes:

```text
App::OpenSettings
App::Exit
App::HandleHudToggleHotkey
App::HandleSystemSuspend
App::HandleSystemResume
App::HandleTimer
```

After CH-RTF-1, runtime event callbacks already belong to `RuntimeMessageWindow`, so TrayIcon only needs Settings and Exit shell actions.

### Target design

Use a very small callback structure or equivalent narrow interface, for example conceptually:

```text
TrayActions
- openSettings
- exit
```

Do not introduce a generic command bus.

### Primary code areas

```text
TrayIcon.h/.cpp
App.h/.cpp
RuntimeMessageWindow.*
```

### Acceptance

- `TrayIcon.cpp` no longer needs `App.h`;
- tray remains Standalone-capable;
- all runtime system-event delivery stays in `RuntimeMessageWindow`;
- no behavior change.

### Why separate this from CH-RTF-1

Keeping the first PR centered on the critical HWND move makes review easier. This second PR removes the remaining direct shell-to-App coupling without mixing it with window ownership changes.

### Planning size

Approximately 100–220 changed LOC.

---

## CH-RTF-3 — Introduce the in-process runtime-control contract and migrate legacy Settings

### Purpose

Establish the semantic API that every future frontend will use, before adding any pipe transport.

The existing Win32 Settings becomes the first client of this contract.

### Target API responsibility

Define a narrow interface/service representing product-level controls, conceptually:

```text
GetRuntimeInfo
GetSettingsSnapshot
SetStartWithWindows
SetHudEnabled
SetVisibilityMode
SetHudSizeOffset
SetHudFont
SetHudAlignment
SetHudBackgroundMode
SetHudOpacityPreview
CommitHudOpacity
SetIntelVrrRangeFixEnabled
GetIntelVrrLastResult
RequestShutdown (may remain shell-only until later PR)
```

The exact C++ type name should be chosen during implementation, but it must describe ClawHUD runtime control rather than a particular UI.

### Snapshot rule

Introduce one in-process authoritative snapshot type containing frontend-relevant values.

Do not expose:

```text
HudController object
HudPresentation object
PresentMon provider
EC client
game detector objects
SettingsWindow HWND/control IDs
```

### Existing semantics remain authoritative

The implementation should delegate to the current App/runtime methods rather than reimplement behavior.

This is especially important for:

- enable/disable orchestration;
- presentation recreate/rollback behavior;
- visibility mode side effects;
- startup registration rollback;
- settings persistence;
- Intel VRR setting persistence.

### Legacy Settings migration

Change `SettingsWindow` so it depends on the narrow runtime-control contract instead of concrete `App&` for setting state/mutations.

Keep UI-lifetime notification separate, e.g. a narrow destruction callback. Do not put `PostSettingsDestroyed()` into the public external settings API solely because the Win32 window needs it.

### Important parity behavior

Preserve the current special refresh behavior after mutations such as font rollback and F8 changes. If the frontend needs to refresh, use the authoritative snapshot rather than reaching back into App-specific controls.

### Primary code areas

```text
App.h/.cpp
SettingsWindow.h/.cpp
SettingsWindow.Settings.cpp
SettingsWindow.Tweaks.cpp
possibly SettingsWindow.About.cpp
new runtime-control header/source
focused tests
```

### Acceptance

- current Win32 Settings still looks/behaves the same;
- Settings no longer needs the full concrete App control surface for product settings;
- all mutations still go through existing runtime semantics;
- no `settings.ini` behavior changes;
- no IPC yet.

### Planning size

Approximately 300–450 changed LOC.

---

## CH-RTF-4 — Define the versioned Control IPC wire protocol and codec tests

### Purpose

Freeze a small transport-independent wire contract before implementing the server.

This PR should contain **no runtime behavior change**.

### Suggested location

```text
src/shared/ClawHudControlProtocol.h
```

and, if needed:

```text
src/ClawHUD/RuntimeControlCodec.*
```

### Header concept

Use explicit fixed-width fields, for example:

```text
magic
protocol version
message type
request id
payload byte count
```

Do not serialize raw native class memory.

### Initial message families

Reserve/define only what is needed:

```text
GetRuntimeInfo
GetSettingsSnapshot
SetHudEnabled
SetVisibilityMode
SetHudSizeOffset
SetHudFont
SetHudAlignment
SetHudBackgroundMode
SetHudOpacityPreview
CommitHudOpacity
SetIntelVrrRangeFixEnabled
SetStartWithWindows (if retained externally)
RequestShutdown
```

Responses must be capable of returning authoritative state rather than only `OK`.

### Compatibility rules

- explicit protocol version;
- additive message evolution preferred;
- unknown message types rejected cleanly;
- invalid payload lengths rejected before decode;
- maximum frame size fixed;
- wire enums use fixed integer values independent of C++ enum layout.

### Tests

Pure codec/protocol tests should cover:

- valid header/frame round trip;
- truncated header;
- oversized payload;
- unsupported magic/version;
- unknown message type;
- invalid enum/value range;
- opacity bounds/encoding where applicable.

### Why this is its own PR

Transport and message-thread dispatch are much easier to review if the bytes-on-the-wire contract is already proven independently.

### Planning size

Approximately 200–350 changed LOC including tests.

---

## CH-RTF-5 — Add main-thread runtime-control dispatch bridge

### Purpose

Ensure IPC worker threads never directly mutate HWND/presentation/game-session state.

Current runtime behavior is message-loop-oriented. `HudController`, telemetry target changes, Settings lifetime, timers, and shutdown should continue to be coordinated on the main application thread.

### Target design

Add a narrow dispatch mechanism from background control transport to `RuntimeMessageWindow` / App main thread.

Conceptually:

```text
IPC worker
    -> validated RuntimeControlRequest
    -> queue/post to RuntimeMessageWindow
    -> main thread invokes runtime-control contract
    -> authoritative RuntimeControlResponse
    -> signal response back to worker
```

The implementation may use a small owned request object + Win32 event/future equivalent, but must have explicit lifetime/cancellation rules.

### Requirements

- no direct `HudController` call from pipe thread;
- no direct SettingsWindow call from pipe thread;
- shutdown cannot leave a waiting control request permanently blocked;
- requests received after shutdown begins fail deterministically;
- main thread remains the mutation authority.

### Initial scope

This PR can use an in-process fake/test producer rather than a real pipe.

Prove:

```text
request -> main-thread dispatch -> authoritative response
```

first.

### Primary code areas

```text
RuntimeMessageWindow.*
App.*
runtime-control contract implementation
new dispatch queue/bridge
focused tests
```

### Planning size

Approximately 250–400 changed LOC.

---

## CH-RTF-6 — Add secure read-only Named Pipe Control server

### Purpose

Make a running ClawHUD externally discoverable and inspectable without yet allowing mutations.

### Initial command scope

Only:

```text
GetRuntimeInfo
GetSettingsSnapshot
```

This keeps transport/security review separate from mutation behavior.

### Pipe characteristics

The server is process-lifetime runtime infrastructure in both launch modes.

Required properties:

- local Windows Named Pipe;
- current-user/session scoped;
- remote clients rejected;
- bounded payload size;
- one server authority per ClawHUD runtime;
- clean stop during App shutdown;
- protocol validation before dispatch.

Unlike the old separate-Settings handoff, the name must be discoverable by an independently installed compatible frontend. Do not use a random pipe name that only a child process can know.

The exact discoverable name can include user/session identity to avoid cross-session ambiguity.

### Security

Use Windows pipe security suitable for current-user-only access. `PIPE_REJECT_REMOTE_CLIENTS` or equivalent local-only protection is required.

Do not expose privileged EC/helper operations over this pipe.

### Lifecycle

```text
runtime starts
    -> pipe server starts

frontend connects
    -> handshake/read requests

runtime exits
    -> listener stops
    -> active client unblocks/fails
    -> all transport resources release
```

### Tests

- valid current-user connection;
- malformed frame rejection;
- unsupported version;
- disconnect/reconnect;
- server stop with client connected;
- read-only calls return the same authoritative snapshot as in-process control.

### Planning size

Approximately 300–450 changed LOC.

---

## CH-RTF-7 — Add IPC settings mutations and graceful shutdown

### Purpose

Complete the ClawHUD-side external frontend control surface.

### Add mutation commands

```text
SetHudEnabled
SetVisibilityMode
SetHudSizeOffset
SetHudFont
SetHudAlignment
SetHudBackgroundMode
SetHudOpacityPreview
CommitHudOpacity
SetIntelVrrRangeFixEnabled
SetStartWithWindows (only if retained in the public contract)
RequestShutdown
```

### Mutation result rule

Every mutation returns the authoritative post-mutation state/value.

Do not return success merely because a request parsed.

This preserves behavior such as presentation recreation rollback.

### Opacity

Preserve:

```text
Preview
    -> live runtime change
    -> no repetitive INI write

Commit
    -> final runtime change
    -> persistence
```

### Shutdown

`RequestShutdown` must enter the normal App shutdown path on the main thread.

Do not terminate the process from the IPC worker thread.

### F8/external state changes

Do not build a generic event bus yet.

For the first protocol:

- open/activated frontend requests a fresh snapshot;
- mutation response contains authoritative state;
- leave protocol room for a future narrow state-changed notification.

### Tests

At minimum verify:

- each message maps to the intended runtime-control operation;
- invalid values are rejected;
- opacity preview does not persist while commit does;
- failed/rolled-back runtime mutation returns actual current value;
- shutdown drains/stops the server safely.

### Planning size

Approximately 300–450 changed LOC.

---

## CH-RTF-8 — Add explicit Standalone / Managed launch composition

### Purpose

Introduce the user-visible architectural capability after the runtime no longer depends on the tray HWND.

### Command-line model

```text
ClawHUD.exe
    -> Standalone

ClawHUD.exe --managed
    -> Managed
```

Standalone is always the default. Mode is not persisted.

### Main/App changes

`main.cpp` currently ignores its command line. Add a small pure launch-mode parser and pass the resolved mode into App composition.

Recommended policy object/data:

```text
LaunchMode::Standalone
LaunchMode::Managed
```

### Standalone composition

```text
RuntimeMessageWindow    ON
Control IPC             ON
Tray                    ON
Standalone Settings     available
```

### Managed composition

```text
RuntimeMessageWindow    ON
Control IPC             ON
Tray                    OFF
Standalone Settings     not automatically opened
```

### Critical requirement

Both modes use the same:

```text
HudController
HudPresentation
ProductionTelemetryController
GameSessionController
PresentMonTelemetryProvider
HudSettingsStore
TweakStartupCoordinator
EC path
```

No duplicate runtime class or alternate renderer.

### Hotkey/power/timer behavior

F8, suspend/resume, telemetry timers, game-session messages and resume recovery must continue to work in Managed mode because they now use `RuntimeMessageWindow`, not TrayIcon.

### Single instance

Preserve exactly one ClawHUD runtime per user session.

A normal `ClawHUD.exe` launched while a Managed runtime already exists must not convert it back to Standalone.

A `--managed` launch racing an existing Standalone instance may fail the single-instance acquisition; the external owner is responsible for graceful Standalone shutdown before retrying Managed launch.

### Tests

Use pure launch policy tests plus a small process/manual matrix:

```text
normal launch -> tray visible
managed launch -> no tray
both -> same runtime settings/HUD behavior
second normal launch while Managed -> no second runtime
second Managed launch -> no second runtime
```

### Planning size

Approximately 250–400 changed LOC.

---

## CH-RTF-9 — Harden mode-aware startup, update, and lifecycle policy

### Purpose

Resolve the lifecycle edges that are currently Standalone-specific before SteamAddon takes ownership of Managed mode.

This PR should implement only ClawHUD-side policy. Addon ownership/restart mechanics remain in the Addon repo.

### 9.1 Startup registration

Current `App::Run()` always calls `ApplyStartupRegistration()`.

Managed mode must not become the owner of Standalone startup registration.

Recommended rule:

```text
Standalone
    -> preserve current startup registration reconciliation

Managed
    -> do not rewrite standalone startup registration merely because the external owner launched the runtime
```

The stored Standalone preference remains untouched.

### 9.2 Pending Velopack update

Current `CheckForUpdates()` applies pending updates and requests restart.

Lifecycle architecture requires:

```text
Standalone pending update
    -> preserve normal ClawHUD update/restart behavior

Managed pending update
    -> apply update and exit without accidentally relaunching as Standalone
    -> living Addon owner observes Managed child exit and relaunches installed ClawHUD with --managed
```

Use the actual Velopack API behavior verified during implementation. Do not assume restart preserves `--managed` unless it is explicitly proven.

### 9.3 Runtime info

`GetRuntimeInfo` should expose at least:

```text
app version
protocol version
launch mode
runtime readiness
```

so an owner can determine whether the existing runtime is Standalone or Managed.

### 9.4 Graceful shutdown idempotency

Multiple possible shutdown triggers can occur during Windows shutdown, update, Integration disable, or owner teardown.

Reuse the existing idempotent App shutdown path; do not create separate Managed shutdown logic.

### Lifecycle scenarios to test/document

```text
ClawHUD startup first, then Addon Integration ON
Addon/Managed first, then normal ClawHUD startup fires
near-simultaneous boot launches
Standalone manual launch while Managed exists
Managed runtime update exit
Windows shutdown/reboot
IPC temporarily unavailable during runtime restart
```

### Planning size

Approximately 200–350 changed LOC.

---

## CH-RTF-10 — Final ClawHUD integration-contract regression and cleanup

### Purpose

Finish the ClawHUD side with no new architecture.

This is a focused stabilization/deletion pass after all real behavior is already merged.

### Review targets

- stale `tray_.Window()` runtime dependencies;
- stale direct `SettingsWindow -> App` setting-control dependencies;
- IPC code accidentally reaching renderer/telemetry internals;
- duplicate settings serialization logic;
- mode-specific branches below the intended shell boundary;
- stale comments saying TrayIcon owns the application message window;
- stale WinUI3-handoff assumptions that contradict the new runtime/frontend architecture;
- build/test source-list organization for the new small classes.

### Test matrix

Run the complete current suite plus focused new control/mode tests.

Manual validation should include both modes with the same real game:

```text
Standalone
Managed
```

and compare:

- HUD appearance;
- visibility behavior;
- FPS/graphics API behavior;
- system/EC telemetry;
- F8;
- suspend/resume;
- click-through/no-activation/topmost;
- independent flip / existing VRR diagnostics/assertions.

### Cleanup rule

Do not remove the legacy Win32 Settings here unless a replacement frontend has already reached parity. Runtime separation and frontend replacement are deliberately separate programs of work.

### Planning size

Approximately 100–300 changed LOC.

---

# 6. SteamAddonforClaw follow-up PR map

These are cross-repository dependencies, not ClawHUD implementation PRs. They are listed because the ClawHUD contract must support them without further architecture changes.

## SA-HUD-1 — Installation and compatibility discovery

### Goal

Add a dedicated HUD page/surface that can represent:

```text
ClawHUD not installed
ClawHUD installed but incompatible
ClawHUD installed and compatible
```

No process ownership yet.

### Behavior

- discover separately installed ClawHUD;
- do not bundle ClawHUD;
- do not update ClawHUD;
- if absent, disable integration/settings controls and show Addon-owned install guidance/link;
- if present, prepare the Integration toggle.

Keep this independent from controller/routing/QAM/profile functionality.

---

## SA-HUD-2 — Read-only ClawHUD IPC client

### Goal

Implement the C# client for:

```text
GetRuntimeInfo
GetSettingsSnapshot
```

### Behavior

- protocol version validation;
- render installed/runtime/mode state;
- no INI parsing;
- no mutations;
- no process restart yet.

This is the cross-language proof that the ClawHUD wire protocol is usable.

---

## SA-HUD-3 — HUD Settings page mutations

### Goal

Expose ClawHUD settings in the Addon UI through the same IPC contract.

### Scope

Map UI controls to:

```text
HUD enabled
visibility mode
size
font
alignment
background mode
opacity preview/commit
Intel VRR tweak setting
```

Only expose Start-with-Windows if the desired product UX remains clear that this is the **Standalone ClawHUD** preference.

The Addon never writes ClawHUD INI directly.

---

## SA-HUD-4 — Integration ownership and Standalone -> Managed transition

### Goal

Implement the core Integration toggle.

### Integration ON

```text
if no ClawHUD runtime:
    launch installed ClawHUD.exe --managed

if existing runtime is Standalone:
    RequestShutdown
    bounded wait for exit
    launch --managed

if existing runtime is Managed:
    adopt/reuse only if it belongs to the current valid ownership situation;
    otherwise reconcile according to the final ownership implementation
```

### Integration OFF

```text
shutdown owned Managed runtime
do not automatically launch Standalone
```

The Addon does not change ClawHUD's stored Start-with-Windows preference.

---

## SA-HUD-5 — Managed lifetime / boot / crash / update hardening

### Goal

Implement the lifecycle policy documented in the architecture document.

### Required convergence rule

While:

```text
SteamAddon is alive
AND Integration = ON
```

there must converge to:

```text
exactly one ClawHUD runtime
mode = Managed
tray = absent
```

### Cases

- Addon starts first at boot;
- ClawHUD standalone startup starts first;
- both start nearly simultaneously;
- Addon exits normally;
- Addon crashes/is killed;
- Managed ClawHUD crashes/is killed;
- Addon restarts/updates;
- Managed ClawHUD exits to apply a self-update;
- Integration is toggled off;
- ClawHUD is uninstalled while Addon is open;
- Addon is uninstalled while Integration was enabled.

### Ownership implementation

A Windows Job Object with kill-on-owner-close is a strong candidate for ensuring a Managed process does not remain orphaned after owner death, but the exact mechanism must be validated against the Addon's update/restart model before implementation.

Managed crash recovery should be bounded; do not create an infinite restart loop.

---

# 7. Dependency graph

```text
CH-RTF-1  RuntimeMessageWindow extraction
    ↓
CH-RTF-2  Tray shell decoupling
    ↓
CH-RTF-3  In-process runtime-control contract
    ↓
CH-RTF-4  Wire protocol
    ↓
CH-RTF-5  Main-thread dispatcher
    ↓
CH-RTF-6  Read-only pipe server
    ↓
CH-RTF-7  Mutations + shutdown
    ↓
CH-RTF-8  Managed mode
    ↓
CH-RTF-9  lifecycle/update policy
    ↓
CH-RTF-10 regression/cleanup

                            ┌─ SA-HUD-1 installation discovery
CH-RTF-6/7/8 stable ────────┼─ SA-HUD-2 read-only IPC
                            ├─ SA-HUD-3 settings UI
                            └─ SA-HUD-4/5 lifecycle ownership
```

The SteamAddon UI page work can begin once the protocol/runtime-info surface is stable, but process ownership should wait until Managed mode and ClawHUD-side update policy are merged.

---

# 8. Why the series is intentionally split this way

The sequence separates four different risk classes.

### A. Win32 message infrastructure risk

Handled first by:

```text
CH-RTF-1
CH-RTF-2
```

This proves that the runtime can exist without the visual tray icon while preserving the exact timer/hotkey/power/message behavior.

### B. Product-settings semantic risk

Handled by:

```text
CH-RTF-3
```

The legacy UI proves the new runtime-control API before cross-process serialization exists.

### C. IPC/security/threading risk

Handled by:

```text
CH-RTF-4
CH-RTF-5
CH-RTF-6
CH-RTF-7
```

Protocol, main-thread dispatch, transport security, and mutations are intentionally separate review surfaces.

### D. Lifecycle/mode risk

Handled last by:

```text
CH-RTF-8
CH-RTF-9
```

Managed mode is introduced only after the same runtime can operate without TrayIcon ownership and after an external controller can inspect/shutdown it safely.

This is easier for both implementation agents and PR review than introducing `--managed`, IPC, tray suppression, and settings refactoring in one large change.

---

# 9. Explicitly deferred work

Do **not** include the following in this PR series unless a concrete blocker is discovered:

```text
WinUI3 migration
WPF frontend
browser/Web settings frontend
legacy Win32 Settings deletion
SteamAddon binary bundling of ClawHUD
shared EC-helper service
PresentMon architecture change
HUD renderer/presentation change
game-detection redesign
new diagnostics UI
new generic event bus
new generic RPC framework
DI/service-container conversion
plugin architecture
```

After CH-RTF-10, the standalone frontend can be chosen independently.

The Web UI option remains explicitly reviewed and viable, but it is not part of runtime separation.

---

# 10. Test strategy across the series

## 10.1 Existing regression suite

Every implementation PR should continue running the current full CTest suite applicable to the environment.

Do not weaken or remove existing presentation-contract assertions.

## 10.2 New focused test groups

Likely new tests should be small and pure where possible:

```text
RuntimeMessageWindow / shell policy tests where practical
RuntimeControl snapshot/mutation tests
Control protocol codec tests
Control dispatch tests
Named Pipe transport/protocol tests
LaunchMode parser/policy tests
Managed lifecycle decision tests
```

Avoid tests that require a real game or live desktop when a pure policy test can prove the rule.

## 10.3 Manual hardware validation checkpoints

Not every PR needs full hardware validation, but at least the following checkpoints should receive real-device smoke testing:

### After CH-RTF-1/2

- Standalone tray and Settings;
- F8;
- game detection;
- suspend/resume;
- normal HUD telemetry.

### After CH-RTF-7

- external test client changes all settings;
- opacity drag/commit behavior;
- graceful remote shutdown;
- runtime survives client disconnect/reconnect.

### After CH-RTF-8/9

- Standalone and Managed modes on real hardware;
- no tray in Managed;
- identical HUD rendering/telemetry;
- boot ordering scenarios;
- update/restart behavior.

---

# 11. Final expected ClawHUD ownership after the series

The code should read conceptually as:

```text
main.cpp
    -> parse launch mode
    -> Velopack/bootstrap
    -> App composition root

App
    ├─ RuntimeMessageWindow      always
    ├─ RuntimeControlService     always
    ├─ RuntimeControlServer      always
    ├─ HudController             unchanged production owner
    ├─ PresentMonTelemetryProvider
    ├─ ProductionTelemetryController
    ├─ GameSessionController
    ├─ HudSettingsStore
    ├─ TweakStartupCoordinator
    │
    ├─ TrayIcon                  Standalone only
    └─ legacy SettingsWindow     Standalone only / temporary frontend
```

The important architectural line is:

```text
frontends
    ↓
RuntimeControl contract
    ↓
App/runtime authorities
```

not:

```text
frontend
    ↓
settings.ini / HudController / renderer / PresentMon directly
```

---

# 12. Completion criteria for the whole program

The ClawHUD side is ready for arbitrary frontend selection and SteamAddon integration when all of the following are true:

1. `ClawHUD.exe` normal launch remains a fully independent Standalone app.
2. `ClawHUD.exe --managed` runs the same runtime with no Tray.
3. Both modes have the same hidden runtime message infrastructure.
4. F8, suspend/resume, timers, game-session messages, HUD rendering, telemetry and tweaks work in both modes.
5. Exactly one ClawHUD runtime can exist per user session.
6. An external same-user client can discover the Control pipe, negotiate protocol version, read runtime/settings state, mutate supported settings and request graceful shutdown.
7. Runtime settings remain authoritative and are persisted only by ClawHUD.
8. Opacity preview/commit behavior is preserved.
9. Managed mode does not rewrite Standalone startup ownership.
10. Managed update behavior cannot accidentally relaunch as Standalone; external owner can regain Managed ownership after update exit.
11. SteamAddon can implement its HUD page without depending on ClawHUD source code, INI schema, renderer, PresentMon, EC helper, or installation packaging.
12. Existing HUD presentation/VRR invariants and regression tests remain unchanged and passing.
13. No standalone frontend technology has been forced by the runtime architecture.

At that point the next independent product decision can be made among:

```text
keep Win32
WPF
WinUI 3
browser-based local Web UI
```

without another ClawHUD runtime redesign.
