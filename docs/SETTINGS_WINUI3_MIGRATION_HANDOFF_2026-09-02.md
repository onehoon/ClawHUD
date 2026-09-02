# ClawHUD Settings WinUI 3 Migration — Architecture Review & Handoff

> **Handoff date:** 2026-09-02  
> **Status:** Legacy Win32 Settings physical refactor complete. WinUI 3 migration not yet implemented.  
> **Purpose:** Preserve the complete Settings modernization discussion, alternatives considered, final architecture direction, constraints, build/deployment plan, and implementation sequence for the next development session.

---

## 1. Executive decision

The selected direction is **not** to migrate the always-running `ClawHUD.exe` process or the production HUD to WinUI 3.

The recommended target is:

```text
ClawHUD.exe                     [UNELEVATED, native C++, always running]
├─ tray / message window
├─ HUD / presentation
├─ telemetry
├─ game detection
├─ tweaks/core state
├─ settings persistence + authoritative settings state
├─ SettingsSession              [exists only while Settings is open]
└─ EcHelperClient
      └─ runas → ClawHUD.EcHelper.exe [ELEVATED]

ClawHUD.Settings.exe            [UNELEVATED, C++/WinRT + WinUI 3]
├─ Settings page
├─ Tweaks page
└─ About page
```

`ClawHUD.Settings.exe` is a **separate UI process**. It is launched only when the user asks to open Settings and must **exit when the Settings window is closed**.

The main reasons are:

1. keep tray-only memory/resource usage minimal;
2. prevent Windows App SDK / WinUI / XAML runtime modules and caches from living inside the always-running process;
3. isolate UI failures from HUD/telemetry/game-detection runtime;
4. keep the VRR-critical HUD presentation stack completely untouched;
5. allow the Settings UI to use native WinUI 3 controls, touch behavior, DPI handling, Mica, and Windows 11 layout without forcing the rest of ClawHUD onto Windows App SDK.

The alternative **Win32 shell + WinUI 3 content through `DesktopWindowXamlSource` / XAML Islands** is technically valid, but is **not the preferred production design** because it loads the WinUI/XAML runtime into `ClawHUD.exe` itself. Closing the island/window is not the same as returning the process to the pre-WinUI module/working-set state.

---

## 2. Current state at handoff

### 2.1 Phase 0 legacy Settings refactor is complete

The original monolithic native Settings implementation was physically separated before attempting a technology migration.

Current production layout:

```text
src/ClawHUD/
├─ SettingsWindow.cpp
├─ SettingsWindow.Settings.cpp
├─ SettingsWindow.Tweaks.cpp
├─ SettingsWindow.About.cpp
├─ SettingsWindowInternal.h
├─ SettingsWindowInternal.cpp
├─ SettingsWindowGeometry.h
└─ SettingsWindowGeometry.cpp
```

Responsibilities are now approximately:

```text
SettingsWindow.cpp
    window lifecycle
    DPI / work-area normalization
    font/backdrop
    tabs
    scrolling / touch pan shell
    shared layout
    WindowProc command dispatch

SettingsWindow.Settings.cpp
    General + HUD controls
    Settings-page layout
    current state projection into controls

SettingsWindow.Tweaks.cpp
    Intel VRR Range Fix controls
    Tweaks-page layout
    last-result projection

SettingsWindow.About.cpp
    icon/version/how-to-use controls
    About-page layout

SettingsWindowInternal.*
    Win32 control IDs / constants / stateless helpers

SettingsWindowGeometry.*
    pure window geometry decisions
```

This separation is intentional: it gives the future WinUI migration a clear page-by-page mapping instead of requiring UI migration and monolith refactoring at the same time.

### 2.2 Diagnostics changed after the original design discussion

Earlier migration discussion included a future Diagnostics page in Settings. That is now stale.

Current main has diagnostics separated into the standalone console target:

```text
ClawHUD.Diag
```

Current production Settings contains only:

```text
Settings
Tweaks
About
```

Therefore the new `ClawHUD.Settings.exe` should **not reintroduce Diagnostics** unless a later product decision explicitly changes that scope.

### 2.3 Current Settings features to preserve

Current Settings page exposes:

```text
General
- Start ClawHUD with Windows

HUD
- Enable HUD
- Display mode
  - In-game only
  - Always on
- HUD size
  - -2 / -1 / Default / +1 / +2
- Font
  - Unispace
  - Segoe UI Variable
- Alignment
  - Left
  - Center
  - Right
- Background width
  - Full width
  - Content width
- HUD Opacity
```

Current Tweaks page exposes:

```text
Intel VRR Range Fix
- Enable Intel VRR Range Fix
- affected panel/result information
- before/after range information
- last run result
```

Current About page exposes:

```text
ClawHUD
Lightweight performance HUD for MSI Claw
Version
How to use
```

The WinUI migration is initially a **presentation replacement**, not an opportunity to change these semantics.

---

## 3. Product constraints that drove the architecture

### 3.1 Tray-only resource usage is a hard requirement

The desired steady state is:

```text
Settings closed / tray-only

ClawHUD.exe                running
ClawHUD.Settings.exe       NOT running
WinUI/XAML UI runtime      NOT loaded by ClawHUD.exe
Settings IPC worker        NOT running
```

This is more important than minimizing implementation work.

ClawHUD is designed to remain resident for HUD/game detection/telemetry. A modern Settings UI should not permanently increase that resident footprint merely because the user opened Settings once.

### 3.2 Windows 11-only product

There is no requirement to retain a Windows 10-compatible Settings experience.

The UI can target Windows 11 conventions directly:

- WinUI 3 native controls;
- Segoe UI Variable/system typography;
- Mica/system backdrop;
- touch-friendly control sizes;
- native scrolling and DPI behavior;
- modern toggle/radio/slider controls;
- Windows 11 navigation and spacing.

### 3.3 Main runtime remains native C++

Do not convert the product to C# simply for the UI.

Selected UI stack:

```text
C++/WinRT + WinUI 3
```

The current C++ core remains the owner of runtime state and settings application.

### 3.4 No need for migration compatibility with deployed legacy users

At this point the project does not need a complicated legacy Settings migration layer.

The new UI should consume the same authoritative runtime/settings state from `ClawHUD.exe` rather than independently migrating or owning the existing persistence format.

---

## 4. Options considered

## 4.1 Option A — keep all-native Win32 Settings

### Shape

```text
ClawHUD.exe
└─ native SettingsWindow HWND
   └─ BUTTON / STATIC / TRACKBAR / tab control
```

### Advantages

- smallest runtime dependency set;
- no Windows App SDK prerequisite;
- lowest conceptual complexity;
- no IPC or second process.

### Problems

The existing UI already demonstrated the cost of continuing to hand-build a modern touch UI in raw Win32:

- DPI/manual sizing code;
- custom scroll/pan forwarding;
- manual control layout;
- manually managed fonts;
- limited Windows 11-native visual language;
- more work for touch-friendly behavior;
- every additional control increases `WindowProc`, IDs, layout and state-sync complexity.

### Decision

Useful as the stable legacy implementation during migration, but **not the desired final Settings UI**.

---

## 4.2 Option B — keep native Win32 outer window and host only WinUI 3 content

This is the "Win32 shell + modern content" option.

### Technical shape

```text
ClawHUD.exe
└─ native Settings HWND
   └─ DesktopWindowXamlSource
      └─ WinUI 3 page/content tree
```

Microsoft's Windows App SDK exposes `Microsoft.UI.Xaml.Hosting.DesktopWindowXamlSource` specifically so Win32/WPF/Windows Forms desktop applications can host WinUI UI elements associated with an HWND.

This option is technically feasible.

### Potential benefits

- existing native outer HWND/window activation could remain;
- gradual page/content migration is possible;
- less IPC work;
- App calls could remain direct in-process calls;
- native top-level sizing/activation code could be retained initially.

### Main problem: process lifetime

The XAML island lives **inside `ClawHUD.exe`**.

That means the always-running process has to initialize the Windows App SDK and WinUI/XAML runtime when Settings is opened.

Even if the island and its visual tree are closed/disposed, the design cannot assume that all loaded Windows App SDK / XAML modules, process-wide state, allocator caches, graphics resources and framework caches will return to the same footprint as a process that never initialized WinUI.

For ClawHUD this directly conflicts with the intended tray-only invariant.

### Additional costs

The mixed HWND/XAML architecture also retains integration work around:

- parent/child HWND sizing;
- initialization order;
- focus/tab traversal between Win32 and XAML;
- message-loop ownership;
- keyboard handling;
- DPI/window resize coordination;
- host lifetime/close handling;
- possible airspace/interop issues if native content remains around the island.

### Decision

**Rejected as the preferred production architecture.**

It remains a technically valid fallback if process separation proves impossible, but it should not be chosen merely because it avoids IPC.

---

## 4.3 Option C — replace the Settings window with WinUI 3 but keep it in `ClawHUD.exe`

This is a same-process "full Settings window migration" rather than a content island.

```text
ClawHUD.exe
├─ tray/HUD/core
└─ WinUI 3 Settings Window
```

It avoids part of the HWND/XAML-island composition complexity, but it does **not** solve the primary memory/lifetime concern: WinUI and Windows App SDK still initialize inside the always-running process.

### Decision

**Not selected.**

---

## 4.4 Option D — separate WinUI 3 Settings executable

### Shape

```text
ClawHUD.exe
    │
    │ local session IPC
    ▼
ClawHUD.Settings.exe
```

### Advantages

- WinUI/XAML memory disappears when the process exits;
- tray-only main process never needs to load WinUI;
- Settings crashes do not take down HUD/core;
- main process remains a small native Windows application;
- WinUI top-level window can use its own natural lifecycle;
- clean future page development;
- production HUD and VRR presentation remain structurally isolated.

### Cost

- a small IPC protocol is required;
- build/release must produce a second UI executable;
- Windows App SDK runtime prerequisite handling is required;
- single-instance activation now needs to route to the UI process.

### Decision

**Selected.**

---

## 5. Comparison summary

| Item | Native Win32 | Win32 + XAML Island | Same-process WinUI | Separate WinUI Settings.exe |
|---|---:|---:|---:|---:|
| Modern Windows 11 UI | Low | High | High | High |
| Touch/DPI implementation effort | High | Low | Low | Low |
| IPC required | No | No | No | Yes |
| WinUI runtime in always-running process | No | **Yes after use** | **Yes after use** | **No** |
| UI crash isolation | No | No | No | **Yes** |
| Keeps HUD/core native-only | Yes | Partially | Partially | **Yes** |
| Tray-only memory target | Best | Weak | Weak | **Best practical choice** |
| Recommended | Temporary legacy | No | No | **Yes** |

---

## 6. Final target architecture

```text
                         USER SESSION

┌──────────────────────────────────────────────────────────────┐
│ ClawHUD.exe                                                  │
│ native C++ / Win32                                          │
│                                                              │
│  TrayIcon / hidden message HWND                             │
│  HudController / HudPresentation                            │
│  ProductionTelemetryController                              │
│  GameSessionController                                      │
│  Tweaks                                                     │
│  HudSettingsStore                                           │
│  existing App state                                         │
│                                                              │
│  SettingsSession     <--- only created on Settings request   │
└───────────────┬──────────────────────────────────────────────┘
                │
                │ local named pipe
                │ request/response
                │ only while Settings is open
                ▼
┌──────────────────────────────────────────────────────────────┐
│ ClawHUD.Settings.exe                                        │
│ C++/WinRT + WinUI 3                                         │
│ unpackaged, framework-dependent                             │
│                                                              │
│  MainWindow / NavigationView                                │
│  SettingsPage                                               │
│  TweaksPage                                                 │
│  AboutPage                                                  │
│  SettingsIpcClient                                          │
│                                                              │
│  NO authoritative settings store                            │
│  NO HUD presentation                                        │
│  NO telemetry/game detector                                 │
│  NO elevated hardware ownership                             │
└──────────────────────────────────────────────────────────────┘
```

The frontend process is a projection/controller of state owned by the main runtime.

---

## 7. State ownership rules

The most important architectural rule is:

> **`ClawHUD.exe` remains the only authority for application settings and runtime mutations.**

The new WinUI process must not directly edit `settings.ini`, write `HudSettingsStore`, modify startup registration, or directly mutate HUD state.

Current `App` already owns the relevant operations:

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

The eventual flow should be:

```text
WinUI user action
   ↓
Settings IPC request
   ↓
ClawHUD.exe validates request
   ↓
existing App/core method
   ↓
existing persistence/runtime side effects
   ↓
response / refreshed snapshot
   ↓
WinUI projection updates
```

Do not create a second settings owner inside the UI process.

---

## 8. IPC design

## 8.1 Why local Named Pipe

A local named pipe is appropriate because:

- both processes are same-machine native desktop processes;
- request volume is tiny;
- the protocol is simple command/state traffic;
- no network transport is needed;
- Win32 offers direct client-PID inspection;
- security can be restricted to the current user;
- the server can exist only for the lifetime of the Settings session.

Do not add localhost HTTP, WebSocket, shared-memory synchronization, a background broker, COM server, or generic RPC framework for this UI.

## 8.2 Session lifetime

Target lifecycle:

```text
OpenSettings()
  -> create SettingsSession
  -> create random local pipe name
  -> create server endpoint
  -> launch ClawHUD.Settings.exe --pipe <session-token/name>
  -> remember launched child PID/handle
  -> accept exactly the intended UI client
  -> process Settings requests

Settings window closes
  -> ClawHUD.Settings.exe exits
  -> pipe disconnects
  -> server worker stops
  -> child/pipe/thread handles close
  -> SettingsSession destroyed
  -> zero Settings IPC worker remains
```

This is deliberately **not** a permanent IPC service.

## 8.3 Pipe security

Recommended safeguards:

- unpredictable/random per-session pipe suffix;
- current-user-only DACL where practical;
- `PIPE_REJECT_REMOTE_CLIENTS`;
- record the process ID returned by process creation;
- after connect, use `GetNamedPipeClientProcessId()` and require it to match the process launched by `ClawHUD.exe`;
- one active Settings client/session;
- fixed maximum payload size;
- reject invalid magic/version/message type/payload length;
- no privileged command exposed directly to the UI process.

## 8.4 Protocol style

Do not introduce JSON just for this channel.

A compact versioned binary protocol is enough.

Illustrative header:

```cpp
struct SettingsMessageHeader
{
    std::uint32_t magic;
    std::uint16_t protocolVersion;
    std::uint16_t messageType;
    std::uint32_t requestId;
    std::uint32_t payloadBytes;
};
```

Use fixed-width types across the wire.

Do not serialize raw C++ class layouts, `std::string`, native pointers, or ABI-dependent enums.

Suggested shared file:

```text
src/shared/SettingsProtocol.h
```

The protocol should include a small `SettingsSnapshot` plus explicit commands.

## 8.5 Initial command set

Current product scope can be covered with commands conceptually equivalent to:

```text
Hello
GetSnapshot

SetStartWithWindows
SetHudEnabled
SetHudVisibilityMode
SetHudSizeOffset
SetHudFont
SetHudAlignment
SetHudBackgroundMode
SetHudOpacity
SetIntelVrrRangeFixEnabled
```

Snapshot should contain the current authoritative values plus Tweaks result information required by the UI.

The About version can also be returned from the main process so the UI does not accidentally display a version that differs from the running core.

## 8.6 Request/response semantics

For a user mutation:

```text
UI sends SetX(requestId, value)
main validates/applies/persists
main returns success/failure + current authoritative value/snapshot
UI renders returned state
```

Do not optimistically assume persistence/apply success for operations that can fail, especially startup registration and HUD initialization.

For the current Settings surface, push/event streaming is unnecessary initially. A mutation response and explicit snapshot refresh are sufficient.

---

## 9. Main-side SettingsSession responsibility

A small main-side component is preferred rather than putting pipe/process plumbing directly into `App.cpp`.

Suggested shape:

```text
SettingsSession.h
SettingsSession.cpp
```

Responsibilities:

```text
- one active UI session
- random pipe identity
- local server creation
- Settings.exe launch
- launched PID/process handle ownership
- Hello/client PID validation
- bounded request loop
- request dispatch into an App/settings facade
- disconnect/child-exit detection
- deterministic teardown
```

Avoid turning it into a generic plugin host, multi-client daemon, message bus, or architecture framework.

The user can only have one Settings window, so a one-session design is enough.

---

## 10. UI process responsibility

Suggested project:

```text
src/ClawHUD.Settings/
├─ ClawHUD.Settings.vcxproj
├─ App.xaml
├─ App.xaml.cpp/.h
├─ MainWindow.xaml
├─ MainWindow.xaml.cpp/.h
├─ Pages/
│  ├─ SettingsPage.xaml
│  ├─ TweaksPage.xaml
│  └─ AboutPage.xaml
└─ Ipc/
   └─ SettingsIpcClient.*
```

The UI process should:

```text
1. parse --pipe
2. initialize WinUI normally
3. connect to the supplied one-shot session pipe
4. perform Hello
5. fetch SettingsSnapshot
6. render pages
7. send field-level commands on user action
8. reflect authoritative responses
9. exit the process when its main window closes
```

It must **not** hide itself in the background after close.

It must **not** add another tray icon.

---

## 11. WinUI 3 visual direction

The goal is a clean Windows 11 Settings-style application using standard platform controls rather than building a custom design system.

Recommended shell:

```text
Window
└─ NavigationView
   ├─ Settings
   ├─ Tweaks
   └─ About
```

Recommended principles:

- native WinUI controls first;
- no unnecessary third-party UI toolkit dependency;
- avoid custom-drawn cards solely to imitate Windows Settings;
- `ScrollViewer` for vertically constrained handheld displays;
- touch scrolling should work naturally without a visible scrollbar being required as the main input mechanism;
- responsive width rather than hard-coded 1920×1200-only geometry;
- use native ToggleSwitch/Slider/RadioButtons/Buttons where appropriate;
- preserve keyboard focus and accessibility;
- use Mica/system backdrop when supported;
- rely on WinUI DPI scaling rather than recreating the legacy manual `Scale()` model for page content.

### Suggested current-control mapping

| Current setting | WinUI control direction |
|---|---|
| Start with Windows | ToggleSwitch |
| Enable HUD | ToggleSwitch |
| Always / In-game only | RadioButtons or equivalent two-choice selector |
| HUD size | compact `- / value / +` buttons or small choice control |
| Font | RadioButtons / two-choice selector |
| Alignment | three-choice selector |
| Background width | two-choice selector |
| Background opacity | Slider + percentage |
| Intel VRR Range Fix | ToggleSwitch + status text |

Do not introduce ComboBox just because WinUI provides one; it is unnecessary for the existing small fixed-choice settings.

---

## 12. DPI and touch implications

One motivation for WinUI is to stop manually reproducing behavior that the framework already owns.

Legacy Win32 currently has explicit code for:

- per-monitor DPI scaling;
- min tracking size;
- work-area normalization;
- vertical mouse-wheel forwarding;
- touch pan gesture forwarding;
- per-control manual coordinates.

The WinUI pages should use native layout primitives and scrolling so those page-level responsibilities disappear.

However, do not delete the legacy geometry/DPI code until production cutover is complete.

---

## 13. Privilege boundary

The Settings UI stays **unelevated**.

Current architecture already has a dedicated elevated helper boundary:

```text
ClawHUD.exe [normal]
  └─ EcHelperClient
      └─ ShellExecuteEx("runas")
          └─ ClawHUD.EcHelper.exe [elevated]
```

Future privileged settings must follow:

```text
WinUI Settings
   ↓ normal Settings IPC
ClawHUD.exe
   ↓ existing core/helper boundary
ClawHUD.EcHelper.exe [only when required]
```

Do not:

- elevate `ClawHUD.Settings.exe` globally;
- elevate `ClawHUD.exe`;
- allow the Settings pipe to become an unrestricted elevated proxy;
- merge the EC helper into the WinUI project.

---

## 14. HUD / VRR presentation contract — absolute boundary

The Settings modernization must not modify or "modernize" the production HUD presentation path.

The following remain non-negotiable:

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
Presentation API / DirectComposition production presentation path
premultiplied-alpha presentation contract
```

Background Opacity continues to mean **background only**.

Never implement the Settings opacity option through window-wide/visual-wide HUD opacity that fades foreground text/outlines/separators.

Relevant repository guardrail:

```text
docs/HUD_PRESENTATION_REFACTOR_GUARDRAIL.md
```

The WinUI project must not acquire dependencies on `HudPresentation`, `HudRenderer`, or the DirectComposition presentation implementation.

---

## 15. Windows App SDK deployment choice

## 15.1 Selected model

`ClawHUD.Settings.exe` should be:

```text
C++/WinRT
WinUI 3
unpackaged
framework-dependent
```

Expected project properties include the unpackaged configuration, conceptually:

```xml
<AppxPackage>false</AppxPackage>
<WindowsPackageType>None</WindowsPackageType>
<WindowsAppSDKSelfContained>false</WindowsAppSDKSelfContained>
```

Framework-dependent is intentional.

Do **not** bundle the complete Windows App SDK runtime into every ClawHUD release merely to simplify first launch.

## 15.2 Why framework-dependent

Benefits:

- substantially smaller ClawHUD package;
- shared Windows App Runtime servicing;
- Settings process pays WinUI memory only while running;
- runtime installation is system-level prerequisite state, not ClawHUD resident memory.

## 15.3 Auto-initialization belongs only in Settings.exe

Microsoft documents `<WindowsPackageType>None</WindowsPackageType>` as the normal auto-initialization path for unpackaged Windows App SDK applications.

This auto-initialization must be part of the **Settings project only**.

Do not link/initialize the Windows App SDK bootstrapper from `ClawHUD.exe`, because doing so defeats the native tray-only isolation goal.

---

## 16. Windows App SDK runtime prerequisite flow

A framework-dependent unpackaged WinUI application requires the matching Windows App SDK Runtime to exist on the machine.

The main native process should perform prerequisite handling **only when Settings is requested**.

Target flow:

```text
User requests Settings
  ↓
ClawHUD.exe checks pinned Windows App SDK runtime prerequisite
  ↓
Runtime present?
  ├─ YES → create SettingsSession → launch Settings.exe
  └─ NO
       ↓
       obtain official pinned WindowsAppRuntimeInstall.exe
       verify expected SHA-256
       verify Microsoft Authenticode signature
       invoke elevated installer with --quiet
       wait for completion
       verify runtime presence again
       delete temporary installer
       launch Settings.exe
```

Microsoft documents the standalone `WindowsAppRuntimeInstall.exe` and its `--quiet` mode for unpackaged/framework-dependent applications.

### Important startup rule

Do not perform this runtime check/install path during normal tray auto-start.

Target tray startup:

```text
ClawHUD.exe --background
  -> native core/tray only
  -> no WinUI runtime prerequisite check
  -> no Windows App SDK bootstrap
  -> no Settings process
```

### Version policy

At this handoff date, Microsoft lists **Windows App SDK 2.4.0** as the latest stable release (2026-08-13).

That does **not** mean the implementation should automatically float to whatever version is latest at build time.

The implementation should choose and pin one supported stable version, including:

```text
NuGet/version
runtime prerequisite version
installer source
expected installer hash
```

Upgrade it deliberately through normal code review.

---

## 17. Build-system direction

Current repository build is native CMake:

```text
CMake
├─ ClawHUD.exe
├─ ClawHUD.EcHelper.exe
├─ ClawHUD.Diag.exe
└─ tests
```

Do not migrate the whole repository to a Windows App SDK project system.

Preferred build split:

```text
CMake / MSVC
├─ ClawHUD.exe
├─ ClawHUD.EcHelper.exe
├─ ClawHUD.Diag.exe
└─ native tests

MSBuild / .vcxproj / C++/WinRT
└─ ClawHUD.Settings.exe
```

This keeps Windows App SDK-specific MSBuild integration confined to the process that actually uses it.

Avoid relying on experimental or unusual Windows App SDK CMake integration just to make both executables use one generator.

---

## 18. Release / Velopack integration

Current release workflow stages `ClawHUD.exe`, `ClawHUD.EcHelper.exe`, PresentMon runtime files, fonts and Velopack components before `vpk pack`.

Migration must extend that flow rather than redesign it.

Target release sequence:

```text
1. CMake configure/build/test native targets
2. MSBuild ClawHUD.Settings.vcxproj Release x64
3. stage native runtime files
4. stage ClawHUD.Settings.exe + only required app-local WinUI bootstrap/assets
5. audit stage contents
6. Velopack pack with ClawHUD.exe still the main executable
```

Keep:

```text
--mainExe ClawHUD.exe
--framework vcredist145-x64
```

Do not assume Velopack has a predefined Windows App SDK runtime framework switch.

Do not use a Velopack install hook to perform network/UAC Windows App Runtime installation on every install/update. Runtime handling should occur on demand when Settings is first requested and the prerequisite is missing.

### Package footprint guard

Release CI should explicitly inspect the staged Settings output to ensure that a framework-dependent build did not accidentally pull the full Windows App SDK runtime into the package.

---

## 19. Launch semantics

This area is **not implemented yet** in current main and must change during migration.

### 19.1 Desired behavior

Manual/interactive launch:

```text
Start Menu / user runs ClawHUD.exe
  -> core starts or activates existing primary
  -> Settings opens
```

Windows startup:

```text
Startup shortcut
  -> ClawHUD.exe --background
  -> no Settings window
```

Tray click:

```text
primary ClawHUD.exe
  -> OpenSettings()
  -> launch/activate ClawHUD.Settings.exe
```

### 19.2 Current behavior that must be changed

Current `wWinMain` ignores command-line mode and `ApplyStartupRegistration()` creates a shortcut without arguments.

Therefore migration must add explicit interactive/background launch semantics.

At minimum:

```text
main.cpp / App startup
- parse --background

ApplyStartupRegistration()
- startup shortcut SetArguments("--background")
```

Do not infer background mode merely from startup timing or parent process.

---

## 20. Main single-instance activation

Current main uses:

```text
Local\ClawHUD.SingleInstance
```

and a secondary instance currently just exits.

Current tray/message window class is:

```text
ClawHUD.TrayMessageWindow
```

That stable hidden HWND is sufficient to add lightweight secondary activation without a permanent broker.

Target:

```text
Primary ClawHUD already running in tray

user runs ClawHUD.exe normally
  ↓
secondary detects existing instance
  ↓
find primary ClawHUD.TrayMessageWindow
  ↓
post/register an OpenSettings message
  ↓
secondary exits
  ↓
primary opens/activates Settings
```

For `--background`, a secondary instance should simply exit without forcing Settings open.

Do not add a persistent IPC broker merely for this activation path.

---

## 21. Existing Settings window/session activation

Only one Settings process/window should exist.

If `SettingsSession` is already active:

```text
OpenSettings()
  -> do not launch a second Settings.exe
  -> restore/foreground the existing Settings window
```

Possible implementation:

- Settings sends its HWND in the authenticated Hello handshake;
- main records the HWND only after verifying client PID;
- subsequent tray/secondary-instance activation uses the recorded HWND to restore/foreground;
- if process/window is dead, tear down stale session and create a new one.

Do not identify an arbitrary window solely by title text.

---

## 22. Failure handling

### Settings executable missing/corrupt

- main remains running;
- log the failure;
- present a small native error message if necessary;
- do not terminate HUD/core.

### Windows App SDK runtime missing and install fails

- main remains native/tray-only;
- do not repeatedly retry in background;
- report Settings prerequisite failure on the interactive request;
- next explicit Settings request may retry.

### Settings process crashes

- pipe disconnect/child process signal tears down `SettingsSession`;
- no core settings state is lost because authoritative state is in main;
- next request launches a fresh Settings process.

### Main process exits

- Settings client detects pipe loss and should close rather than continuing as a detached settings authority.

### Invalid IPC request

- reject the request;
- do not apply partial values;
- malformed/unknown protocol should end the session if trust is lost.

---

## 23. Persistence behavior

The UI migration should not change persistence format just because presentation technology changes.

Main remains responsible for:

```text
HudSettingsStore
startup shortcut registration
Intel VRR tweak setting/result
HUD runtime state
```

This removes the need for a settings-file migration in the first WinUI cutover.

If persistence is redesigned later, do it as a separate core change rather than coupling it to WinUI.

---

## 24. Source dependencies that must not leak into Settings.exe

`ClawHUD.Settings.exe` should not link against or instantiate:

```text
HudPresentation
HudRenderer
PresentMon provider/runtime
ProductionTelemetryController
GameSessionController
Steam RunningAppID watchers
EC hardware reader/helper client
Intel Arc Sync/tweak implementation
Velopack update manager
```

The WinUI executable receives only the minimal snapshot/status needed for rendering and sends user commands back to main.

This keeps startup time, dependency count and failure surface small.

---

## 25. Legacy native Settings removal strategy

Do not remove the existing Win32 Settings before the new UI is functionally complete.

Recommended cutover:

```text
Phase 0  legacy Settings physical split                     COMPLETE
Phase 1  shared Settings protocol + main session foundation
Phase 2  WinUI Settings.exe shell + read-only snapshot
Phase 3  wire Settings/Tweaks mutations
Phase 4  runtime prerequisite + launch/single-instance flow
Phase 5  release/CI staging
Phase 6  production OpenSettings cutover
Phase 7  delete legacy SettingsWindow implementation
```

During development, the old UI can remain compiled as a temporary fallback/development path.

After production cutover is proven, remove it rather than maintaining two permanent Settings implementations.

Because the product is still pre-release, do not spend effort on a long-lived compatibility framework between the two UIs.

---

## 26. Recommended implementation sequence in more detail

### Stage 1 — protocol foundation

Add a shared pure-C++ protocol definition and tests.

Deliverables:

```text
SettingsProtocol.h
protocol constants
wire enums
SettingsSnapshot DTO
message validation helpers/tests
```

No UI/runtime behavior change yet.

### Stage 2 — main Settings facade / dispatcher

Create a narrow translation from protocol commands to existing `App` APIs.

Goals:

- centralize validation;
- keep `SettingsSession` ignorant of HUD implementation details;
- make command handling unit-testable where possible.

Do not turn this into a generic application service locator.

### Stage 3 — SettingsSession + stub client process

Prove:

```text
launch
random named pipe
Hello
PID validation
GetSnapshot
process close
server teardown
reopen
```

This stage can use a tiny native stub before WinUI is involved.

### Stage 4 — C++/WinRT WinUI project shell

Create the unpackaged framework-dependent project and a minimal NavigationView.

First goal is only:

```text
launch
connect
show snapshot read-only
close -> process exits
```

### Stage 5 — Settings page mutations

Wire the current Settings page one field at a time to explicit IPC commands.

Verify failed operations return authoritative UI state.

### Stage 6 — Tweaks and About

Add Intel VRR Range Fix state/result projection and About/version.

No Diagnostics page.

### Stage 7 — Windows App SDK prerequisite handling

Add the native main-side prerequisite checker/installer flow.

Verify `--background` does zero work related to Windows App SDK.

### Stage 8 — interactive/background activation semantics

Implement:

```text
normal launch -> Settings
--background -> tray only
secondary normal launch -> signal primary OpenSettings
```

### Stage 9 — CI/release

Build Settings project, stage only intended files, package, install on a clean Windows 11 test system.

### Stage 10 — cutover

Change production `App::OpenSettings()` to use `SettingsSession`.

After validation, remove `std::unique_ptr<SettingsWindow>` and legacy native page files.

---

## 27. Acceptance tests

## 27.1 Tray-only resource isolation

Critical acceptance test:

```text
fresh boot / ClawHUD --background
```

Verify:

- `ClawHUD.Settings.exe` absent;
- no `Microsoft.UI.Xaml` / Windows App SDK UI runtime loaded in `ClawHUD.exe`;
- no Settings pipe worker/session exists;
- idle main working set matches the native-only design within expected normal noise.

Then:

```text
open Settings
close Settings
```

Verify:

- Settings process exits;
- main does not retain WinUI modules;
- repeated open/close does not accumulate main working set or handles.

## 27.2 IPC lifecycle

Test:

- valid UI process connects;
- wrong/random process cannot impersonate the launched client;
- oversized payload rejected;
- wrong protocol version rejected;
- Settings crash tears session down;
- main exit closes Settings;
- reopen after crash works;
- rapid open requests still produce one UI process.

## 27.3 Settings behavior parity

Verify every current control:

```text
Start with Windows
Enable HUD
Always / In-game only
HUD size
Font
Alignment
Background width
Background opacity
Intel VRR Range Fix
```

Confirm values persist exactly as before and take effect through existing main/core paths.

## 27.4 DPI / handheld UI

At minimum test:

```text
1920×1200 @ 150%
1920×1080 transition
return to 1920×1200
other supported scaling values
window resize / maximize / restore
touch scroll
mouse wheel
keyboard navigation
```

The legacy DPI bug that originally motivated part of the Settings cleanup must not reappear.

## 27.5 Runtime prerequisite

Test clean machine states:

```text
runtime already installed
runtime absent
runtime install success
runtime install failure/cancel
runtime version mismatch
background startup with runtime absent
```

Background startup with runtime absent must still run ClawHUD normally without prompting for/installing WinUI runtime.

## 27.6 HUD/VRR regression

All existing tests/assertions around:

```text
click-through
no activation
topmost
transparent hit testing
independent flip
premultiplied alpha
production presentation contract
```

must remain unchanged and passing.

---

## 28. Things explicitly not to do

Do not:

- migrate the HUD to WinUI;
- put WinUI controls inside the HUD window;
- change HUD window styles to make Settings work;
- initialize Windows App SDK from the always-running main process;
- keep `ClawHUD.Settings.exe` hidden after its window closes;
- add a second tray icon;
- let Settings directly write configuration files;
- let Settings directly call the elevated helper;
- create a permanent IPC server for an occasionally opened UI;
- add HTTP/WebView/Electron for Settings;
- switch the UI implementation to C# merely for convenience;
- bundle the full Windows App SDK runtime without a measured reason;
- add a generic plugin/settings-framework architecture for the small current surface;
- reintroduce Diagnostics into Settings without an explicit new product decision;
- maintain the native and WinUI Settings UIs permanently after cutover.

---

## 29. Current source anchors for the next developer

Current files worth reading first:

```text
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/TrayIcon.cpp
src/ClawHUD/SettingsWindow.h
src/ClawHUD/SettingsWindow.cpp
src/ClawHUD/SettingsWindow.Settings.cpp
src/ClawHUD/SettingsWindow.Tweaks.cpp
src/ClawHUD/SettingsWindow.About.cpp
src/ClawHUD/SettingsWindowInternal.h
src/ClawHUD/SettingsWindowInternal.cpp
src/ClawHUD/SettingsWindowGeometry.h
src/ClawHUD/SettingsWindowGeometry.cpp
CMakeLists.txt
.github/workflows/Build-Test.yml
.github/workflows/Build-Release.yml
docs/HUD_PRESENTATION_REFACTOR_GUARDRAIL.md
```

Important current implementation facts:

```text
- App currently owns std::unique_ptr<SettingsWindow>.
- App::OpenSettings() currently creates/shows the in-process native window.
- App::AcquireSingleInstance() currently exits a secondary process without forwarding activation.
- startup shortcut currently has no --background argument.
- TrayIcon hidden window class is ClawHUD.TrayMessageWindow.
- CMake is the production native build authority.
- ClawHUD.Diag is already a separate console target.
```

These are exactly the seams the next phase will replace or extend.

---

## 30. Microsoft reference material verified for this handoff

The following official documentation was checked while finalizing this handoff:

### WinUI hosting inside Win32 / XAML Islands

- `DesktopWindowXamlSource` — Windows App SDK  
  https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.hosting.desktopwindowxamlsource

Microsoft documents this API as the primary Windows App SDK XAML hosting API for non-WASDK desktop applications associated with HWNDs. This confirms that the Win32-shell/content-only alternative is technically supported.

### Using Windows App SDK in an existing/unpackaged project

- Use Windows App SDK in an existing project  
  https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/use-windows-app-sdk-in-existing-project

- Bootstrapper tutorial for unpackaged/external-location apps  
  https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/tutorial-unpackaged-deployment

- Deployment architecture and overview  
  https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/deployment-architecture

These documents confirm that unpackaged apps must initialize the Windows App SDK runtime and that `WindowsPackageType=None` enables the normal auto-initialization model.

### Framework-dependent unpackaged deployment

- Deployment guide for framework-dependent unpackaged apps  
  https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/deploy-unpackaged-apps

This documents the standalone Windows App Runtime installer and `WindowsAppRuntimeInstall.exe --quiet`.

### Runtime downloads/releases

- Windows App SDK downloads  
  https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/downloads

At the 2026-09-02 handoff, the current stable line shown there includes Windows App SDK 2.4.0 released 2026-08-13.

---

## 31. Final handoff summary

The most important context for the next session is:

```text
1. The legacy Settings refactor is already complete.
2. Do not spend another phase making the Win32 UI prettier.
3. Win32 + XAML Island was considered and is technically supported.
4. Same-process WinUI was also considered.
5. Both same-process approaches lose the key tray-memory isolation property.
6. Selected design is a separate C++/WinRT WinUI 3 ClawHUD.Settings.exe.
7. ClawHUD.exe stays native and authoritative.
8. Settings.exe exists only while the window is open.
9. Local one-shot Named Pipe IPC exists only for that session.
10. Main verifies the exact child process before accepting commands.
11. Settings never directly owns persistence or elevated hardware operations.
12. Windows App SDK is framework-dependent and initialized only in Settings.exe.
13. Missing runtime is handled only on an interactive Settings request.
14. Startup becomes ClawHUD.exe --background with no WinUI work.
15. A normal second launch signals the existing tray process to open Settings.
16. Diagnostics is now a separate ClawHUD.Diag app and is not part of new Settings scope.
17. The production HUD/DirectComposition/Presentation API/VRR contract is completely out of scope and must remain unchanged.
```

The next concrete engineering phase is therefore **Settings IPC / SettingsSession foundation**, followed by the standalone C++/WinRT WinUI 3 process.