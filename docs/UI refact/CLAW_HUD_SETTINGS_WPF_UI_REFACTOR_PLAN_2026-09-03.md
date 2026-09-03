# ClawHUD Settings WPF UI Refactor Plan

**Date:** 2026-09-03  
**Status:** Implementation plan  
**Target:** Replace the legacy Win32 Settings frontend with a lightweight standalone WPF frontend  
**Scope:** Settings frontend, Control IPC client, launch/cutover, packaging, and legacy Settings removal only

---

## 1. Purpose

ClawHUD's runtime/frontend refactor is complete enough that the Settings UI no longer needs to know about HUD implementation details.

The current runtime already provides:

- `IRuntimeControl` as the semantic frontend boundary;
- `RuntimeSettingsSnapshot` as the authoritative frontend-facing state;
- protocol-v1 Control IPC DTOs and codec;
- a secure local current-user/current-session Named Pipe server;
- main-thread dispatch through `RuntimeControlDispatchBridge`;
- authoritative post-mutation snapshots for Settings operations.

The remaining problem is therefore a frontend replacement problem, not a HUD/runtime redesign.

The goal of this plan is to replace the visually dated native Win32 Settings window with a small, modern-looking frontend without increasing the idle cost of the always-running ClawHUD runtime.

The selected direction is:

> **C# WPF + .NET 10 + built-in Microsoft Fluent theme, framework-dependent, launched only on explicit user request.**

This is intentionally not a highly styled or visually ambitious application. The desired result is a simple Windows 11-appropriate Settings window that no longer looks like classic Win32, remains touch-friendly on MSI Claw hardware, and adds minimal long-term maintenance burden.

---

## 2. Architectural baseline

### 2.1 Current runtime boundary

The frontend must preserve the existing architecture:

```text
Settings frontend
      |
      | Control IPC protocol v1
      v
RuntimeControlPipeServer
      |
      v
RuntimeControlDispatchBridge
      |
      | ClawHUD main thread
      v
ExecuteRuntimeControlRequest
      |
      v
IRuntimeControl
      |
      v
App
      |
      +---- HudController
      |
      +---- HudSettingsStore
```

The WPF frontend is only a projection and control surface for this existing runtime authority.

### 2.2 Runtime ownership remains unchanged

`App` remains the runtime composition root and the implementation of `IRuntimeControl`.

The WPF Settings frontend must never directly access or reproduce runtime ownership for:

- `HudController`;
- `HudPresentation`;
- `HudSettingsStore`;
- `settings.ini`;
- telemetry providers/controllers;
- game detection/session state;
- suspend/resume policy;
- HUD presentation lifecycle;
- Intel VRR tweak implementation internals.

### 2.3 Authoritative state rule

The frontend must treat the runtime response as authoritative.

Every mutation follows this model:

```text
user interaction
    -> Control IPC mutation request
    -> ClawHUD runtime applies product semantics
    -> runtime may succeed, reject, clamp, or roll back
    -> response contains authoritative post-mutation snapshot
    -> WPF ViewModel replaces its projected state from that snapshot
    -> UI redraws from the runtime state
```

The frontend must not assume that the requested value became the effective value merely because the user clicked a control.

This is especially important for settings whose runtime application may involve recreation or rollback.

---

## 3. Explicit non-goals

This project must **not** become an opportunity to refactor unrelated runtime domains.

The following are out of scope:

- telemetry collection or formatting changes;
- game detection changes;
- PresentMon changes;
- EC changes;
- HUD presentation implementation changes;
- DirectComposition / Presentation API changes;
- rendering changes;
- VRR presentation-path changes;
- HUD visibility algorithm changes;
- diagnostics UI;
- a new About page;
- localization infrastructure;
- a general-purpose frontend framework shared with other applications;
- a web UI or WebView2 host;
- WinUI 3 migration.

If a Settings feature appears to require changing the production HUD presentation contract, stop and treat that as a design conflict rather than changing the presentation contract.

---

## 4. Non-negotiable HUD / VRR safety boundary

The Settings refactor must remain entirely outside the production HUD presentation contract.

Do not modify, replace, weaken, or work around:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- existing `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirements;
- the existing Presentation API / DirectComposition production path;
- premultiplied-alpha requirements.

`Background Opacity` remains a **background-only user setting** from the frontend/product perspective. The WPF project must only call the existing Control IPC operations and must not attempt to implement opacity itself at the window, visual, renderer, or presentation layer.

Existing regression coverage for click-through, no activation, topmost behavior, transparent hit testing, independent flip, premultiplied alpha, and the production presentation contract must remain intact.

---

## 5. Technology decision

### 5.1 Selected frontend

Use:

- **C#**;
- **WPF**;
- **.NET 10**;
- Microsoft built-in WPF Fluent theme;
- framework-dependent deployment;
- system light/dark theme behavior where available through the built-in theme;
- no third-party UI framework unless a concrete missing requirement is later demonstrated.

### 5.2 Why WPF is preferred here

The Settings frontend is intentionally small:

- one page;
- no tab navigation;
- no sidebar;
- no diagnostics surface;
- no large data grids;
- no complex animation;
- no media or browser content;
- only a small set of toggles, selection controls, size controls, and one slider.

For this scope, WPF provides a better balance than WinUI 3 or a web frontend:

- substantially more modern appearance than the current Win32 controls;
- built-in Fluent styling is sufficient for the desired visual quality;
- straightforward C# implementation;
- simple Named Pipe client implementation;
- mature DPI behavior;
- good touch support when controls are sized correctly;
- no need for a WebView/native bridge;
- no reason to add Windows App SDK UI runtime complexity solely for this small page.

### 5.3 Packaging model

`ClawHUD.Settings.exe` must be **framework-dependent**.

Do not publish the Settings frontend self-contained.

The ClawHUD package must not carry a private .NET Desktop Runtime payload for the WPF frontend. The existing VeloPack installation/update path is responsible for the required .NET runtime prerequisite policy.

This keeps the Settings payload small while preserving the key runtime property:

> When Settings is closed, the WPF process and its CLR/WPF memory disappear completely. The always-running native ClawHUD process does not host WPF or CLR.

---

## 6. Process and lifetime model

### 6.1 Startup

Launching ClawHUD must **not** launch `ClawHUD.Settings.exe`.

Normal startup is:

```text
Windows / user launches ClawHUD
        |
        v
ClawHUD.exe
  - runtime
  - HUD
  - tray
  - Control IPC server

ClawHUD.Settings.exe does not exist
```

This rule applies to normal startup, Start-with-Windows startup, and any background/runtime launch path unless a future explicit product requirement changes it.

### 6.2 Explicit Settings launch only

The Settings process is created only when the user explicitly chooses Settings from the ClawHUD shell/tray.

```text
Tray -> Settings
        |
        v
launch ClawHUD.Settings.exe
        |
        v
connect to existing ClawHUD Control IPC
```

Opening Settings must never start a second ClawHUD runtime.

### 6.3 Settings close behavior

The Settings window is a short-lived configuration frontend, not a background companion process.

Policy:

- no tray icon for `ClawHUD.Settings.exe`;
- no hide-to-tray behavior;
- no background resident Settings process;
- closing the Settings window terminates `ClawHUD.Settings.exe`;
- `Alt+F4` has the same result;
- taskbar/window Close has the same result;
- closing Settings must **not** terminate `ClawHUD.exe`.

The preferred window chrome is:

- Close button: present;
- Minimize button: not exposed;
- Maximize button: not exposed;
- fixed-size Settings window.

A small Settings window has no useful minimized or maximized operating state, and removing those states makes process lifetime obvious: close means exit and memory is returned.

### 6.4 Tray Exit behavior

The existing ClawHUD tray must continue to provide a real application exit command.

```text
Tray
  - Settings -> launch/show Settings frontend
  - Exit     -> terminate ClawHUD runtime through its normal shutdown path
```

`Exit` must remain semantically different from closing Settings.

If `ClawHUD.Settings.exe` is open when the runtime exits, the frontend must not remain as a useless orphan window. It should detect runtime unavailability on its next active IPC interaction and close cleanly. A narrowly scoped runtime-lifetime detection mechanism may be added later if needed, but do not introduce polling solely to keep the Settings process synchronized while idle.

### 6.5 Duplicate Settings launch

Only one Settings window/process should be visible for one ClawHUD runtime/session.

Preferred behavior:

1. user selects Settings while no frontend exists -> launch it;
2. user selects Settings while it already exists -> bring the existing window to the foreground rather than creating another independent Settings client.

The exact implementation can be chosen during the launch/cutover PR, but it must stay frontend-specific and must not alter the ClawHUD single-runtime contract.

---

## 7. Window and layout target

### 7.1 Primary hardware/layout reference

Design and validate first against:

- display: **1920 x 1200**;
- Windows scale: **150%**;
- effective WPF coordinate space: approximately **1280 x 800 DIP** before taskbar/work-area deductions.

This is the primary Claw layout target, not a hard-coded physical-pixel assumption.

WPF must remain DPI-aware and use DIP-based layout.

### 7.2 Initial window target

Recommended initial client/window target:

- width: approximately **700 DIP**;
- height: approximately **660 DIP**;
- centered on the active work area;
- fixed size;
- no horizontal scrollbar;
- no vertical scrollbar.

At 150% scaling, 700 x 660 DIP is approximately 1050 x 990 physical pixels, leaving reasonable room inside a 1920 x 1200 display after normal taskbar/work-area deductions.

The exact final width/height may be adjusted slightly during visual implementation, but the acceptance condition is more important than the literal number:

> All current Settings controls must fit naturally without scrolling at 1920 x 1200 / 150%, with touch-appropriate control sizes and comfortable spacing.

### 7.3 No scrolling

Do not make a `ScrollViewer` part of the normal design.

The current product scope is small enough that needing scrolling would indicate that spacing or card grouping is too wasteful.

Both horizontal and vertical scrollbars should be absent at the primary target configuration.

### 7.4 Work-area safety

Although 1920 x 1200 / 150% is the primary design reference, startup positioning must still respect the current monitor work area.

The window must not deliberately open partly outside the usable work area.

Do not add complex responsive navigation or alternate layouts solely for unsupported/pathological display sizes. Keep the implementation proportional to the product scope.

---

## 8. Touch requirements

The Settings frontend is intended for handheld use and must be operable without a mouse.

### 8.1 General touch rules

Use approximately:

- 40-44 DIP minimum interactive control height;
- generous horizontal hit areas for selection controls;
- 16-20 DIP card internal padding;
- approximately 10-12 DIP inter-card spacing;
- clear selected/unselected state that does not depend on hover;
- sufficient spacing so adjacent touch targets are not easy to hit accidentally.

### 8.2 Selection controls

Small classic radio-button targets are not desirable.

For values such as:

- `In-game only / Always`;
- `Unispace / Segoe UI Variable`;
- `Left / Center / Right`;
- `Full width / Content width`;

use compact Fluent-styled push/segmented selection controls with a large touch target and obvious selected state.

Do not create an elaborate custom control library. A small local style/template is acceptable if required to achieve a simple segmented-button appearance.

### 8.3 Slider

The Background Opacity slider must be comfortably operable by touch.

Its visual thumb and effective hit area must not be mouse-sized only.

The existing runtime semantics remain:

- drag/tracking -> `PreviewHudOpacity`;
- final/committed value -> `CommitHudOpacity`.

Frontend touch behavior must preserve this distinction.

---

## 9. Information architecture

### 9.1 One page only

The new Settings frontend has exactly one primary page.

Do not introduce:

- tabs;
- NavigationView;
- sidebar navigation;
- hamburger navigation;
- separate About page;
- separate General/HUD/Tweaks pages.

The UI should read from top to bottom in order of likely interaction frequency.

### 9.2 Card-only grouping

Use cards to separate logical areas.

Cards themselves should **not** have section titles such as `HUD`, `Appearance`, `General`, or `Tweaks`.

The controls and labels inside each card should make the grouping self-explanatory.

Avoid decorative cards inside cards.

### 9.3 Card order

The agreed order is:

1. HUD enable + display mode;
2. HUD size + font + alignment;
3. background width + background opacity;
4. Intel VRR Range Fix;
5. Start with Windows.

This keeps all HUD controls at the top and the two independent application/tweak settings below them.

### 9.4 Concept layout

```text
ClawHUD <runtime-version>

+----------------------------------------------------+
| Enable HUD                                   [ON]  |
|                                                    |
| Display mode                                      |
| [ In-game only ]  [ Always ]                      |
+----------------------------------------------------+

+----------------------------------------------------+
| HUD size            [ - ]   Default   [ + ]       |
|                                                    |
| Font                                               |
| [ Unispace ]  [ Segoe UI Variable ]               |
|                                                    |
| Alignment                                          |
| [ Left ]  [ Center ]  [ Right ]                   |
+----------------------------------------------------+

+----------------------------------------------------+
| Background width                                   |
| [ Full width ]  [ Content width ]                 |
|                                                    |
| Background opacity                                 |
| [--------- slider -----------]  70%               |
+----------------------------------------------------+

+----------------------------------------------------+
| Intel VRR Range Fix                         [ON]   |
+----------------------------------------------------+

+----------------------------------------------------+
| Start with Windows                          [ON]   |
+----------------------------------------------------+
```

This is a composition guide, not a requirement to create custom visual chrome for every line.

---

## 10. Visual direction

The goal is **clean and current**, not highly stylized.

### 10.1 Desired character

The window should feel appropriate beside other Windows 11 utilities while remaining visibly lightweight.

Use:

- built-in Fluent theme;
- normal system typography;
- restrained corner radius;
- subtle card border/background differentiation;
- system theme behavior;
- system accent where naturally supplied by the framework;
- consistent spacing;
- clear control states.

### 10.2 Avoid

Do not add visual complexity simply to make the app look more modern.

Avoid:

- large hero headers;
- oversized app branding;
- navigation chrome;
- excessive shadows;
- decorative gradients;
- custom acrylic stacks;
- animation-heavy transitions;
- dashboard aesthetics;
- oversized cards;
- redundant section headings.

The intended result is closer to a compact modern Windows utility than a showcase application.

---

## 11. Title and version

The About page is removed.

Display the runtime version directly in the window title/app title:

```text
ClawHUD 0.x.x
```

The preferred version source is `GetRuntimeInfo.applicationVersion`, because that represents the ClawHUD runtime instance the frontend is actually controlling.

Do not maintain a separate user-visible Settings-version field for normal UI purposes.

If runtime metadata cannot be obtained during startup, the frontend may temporarily display `ClawHUD` while handling the connection failure; do not fabricate a runtime version from the frontend assembly.

---

## 12. Language policy

The first WPF frontend is **English only**.

Do not add localization resource infrastructure solely for the current set of strings.

Do not add an in-app language selector.

If multilingual support becomes a real product requirement later, it can be introduced as a separate change with an explicit localization design.

---

## 13. Settings and Control IPC mapping

The current protocol-v1 surface is sufficient for the planned UI.

### 13.1 Startup/read operations

Use:

- `GetRuntimeInfo`;
- `GetSettingsSnapshot`.

### 13.2 HUD controls

Map controls directly to the existing operations:

| UI | Control IPC operation |
|---|---|
| Enable HUD | `SetHudEnabled` |
| Display mode | `SetHudVisibilityMode` |
| HUD size | `SetHudSizeOffset` |
| Font | `SetHudFont` |
| Alignment | `SetHudAlignment` |
| Background width | `SetHudBackgroundMode` |
| Opacity drag | `PreviewHudOpacity` |
| Opacity commit | `CommitHudOpacity` |

Current product values remain:

- HUD size offset: `-2 .. +2`;
- visibility: `Always`, `InGameOnly`;
- font: `Unispace`, `SegoeUiVariable`;
- alignment: `Left`, `Center`, `Right`;
- background mode: `FullWidth`, `ContentWidth`;
- opacity: `50 .. 100%`, 5% step.

### 13.3 Remaining controls

| UI | Control IPC operation |
|---|---|
| Intel VRR Range Fix | `SetIntelVrrRangeFixEnabled` |
| Start with Windows | `SetStartWithWindows` |

No new runtime mutation API is currently required for the agreed UI scope.

---

## 14. WPF client architecture

Keep the frontend small.

A reasonable initial structure is:

```text
src/ClawHUD.Settings/
  ClawHUD.Settings.csproj
  App.xaml
  App.xaml.cs
  MainWindow.xaml
  MainWindow.xaml.cs

  ViewModels/
    MainViewModel.cs

  Services/
    RuntimeControlClient.cs
    ControlProtocolCodec.cs

  Models/
    RuntimeInfo.cs
    SettingsSnapshot.cs

  Styles/
    SettingsStyles.xaml
```

Names may be adjusted during implementation, but do not grow this into a generic framework.

### 14.1 ViewModel responsibility

`MainViewModel` should:

- expose projected runtime state;
- initiate Control IPC commands;
- accept authoritative returned snapshots;
- expose transient connection/error state required by the window;
- contain no HUD implementation logic;
- contain no direct persistence logic.

### 14.2 RuntimeControlClient responsibility

The client should own:

- deterministic per-session pipe endpoint resolution compatible with the runtime;
- request-id generation/correlation;
- protocol-v1 request encoding;
- Named Pipe connect/write/read lifecycle;
- protocol-v1 response decoding;
- response status mapping;
- bounded message handling compatible with the native protocol contract.

It should not own product semantics.

### 14.3 Connection model

Match the existing runtime server behavior rather than inventing a persistent frontend session protocol.

Preferred request model:

```text
connect
 -> send one request
 -> read one response
 -> close
```

A Settings window does not generate enough traffic to justify a new long-lived transport/session abstraction.

---

## 15. Initial load behavior

On window startup:

1. connect to the existing ClawHUD Control endpoint;
2. request `GetRuntimeInfo`;
3. verify protocol/runtime availability;
4. set the title from the runtime version;
5. request `GetSettingsSnapshot`;
6. populate the ViewModel;
7. enable normal interaction.

Do not directly read `settings.ini` as a fallback.

If the runtime cannot be reached, do not display stale or locally fabricated settings as if they were authoritative.

A simple connection-failure state is enough; this frontend is only useful while the ClawHUD runtime exists.

---

## 16. Mutation and UI synchronization behavior

### 16.1 General controls

For button/toggle/selection changes:

1. user requests a value;
2. temporarily prevent duplicate overlapping mutation for that control if necessary;
3. send the corresponding Control IPC request;
4. receive the authoritative response snapshot;
5. replace projected state from the response;
6. render controls from that state.

Do not persist anything from WPF.

### 16.2 Failure behavior

If the operation returns an error status:

- do not leave the UI showing an unconfirmed requested value;
- re-read the snapshot when possible;
- show a restrained, non-modal error indication only if useful;
- avoid generic retry loops or complex state machines for this small frontend.

### 16.3 External runtime state changes

The current Control IPC is request/response, not a runtime-to-frontend event stream.

Therefore the initial WPF frontend does not need a new push/event protocol merely to mirror every external runtime state change instantly.

Use targeted refresh points such as:

- initial window load;
- window re-activation/focus return;
- after every mutation;
- after recovery from a transient IPC failure when appropriate.

Do **not** add a continuous Settings polling loop solely to mirror external changes such as the F8 HUD override.

If real-time push synchronization becomes an explicit product requirement later, design it separately rather than expanding protocol v1 opportunistically during the UI migration.

---

## 17. Background opacity interaction

Opacity is the one Settings interaction with distinct preview/commit semantics.

### 17.1 Required behavior

- range: 50-100%;
- step: 5%;
- value label shown beside the slider;
- touch-friendly slider hit area;
- while actively tracking/dragging: use `PreviewHudOpacity`;
- when tracking ends/final value is accepted: use `CommitHudOpacity`;
- apply the returned authoritative snapshot to the UI.

### 17.2 Persistence boundary

The WPF frontend never decides when/how `settings.ini` is written.

It only expresses preview versus commit intent through the existing IPC operations.

---

## 18. Start with Windows

The WPF frontend only exposes the existing runtime-owned setting.

It must not:

- create/delete shortcuts itself;
- reproduce VeloPack path-resolution logic;
- write startup registry entries;
- directly inspect the startup shortcut to infer state.

Use `SetStartWithWindows` and reflect the returned snapshot.

---

## 19. Intel VRR Range Fix

The first WPF frontend only needs the existing enable/disable control.

Do not turn this migration into a redesign of tweak status reporting.

The runtime snapshot already carries the setting and existing last-result data, but the agreed one-page UI does not require an About/diagnostic/status page.

The UI may remain a simple toggle unless a separate product decision explicitly adds result/status presentation later.

---

## 20. Legacy Win32 Settings migration strategy

Keep the existing native Settings frontend operational while the WPF frontend is built.

Do not cut production over in the first PR.

This allows each intermediate PR to merge without leaving `main` without a functioning Settings surface.

The replacement sequence is:

```text
PR 1-5
  existing Win32 Settings remains production frontend
  WPF frontend grows independently

PR 6
  production tray launch switches to WPF frontend
  validate cutover
  remove legacy native Settings implementation
```

Do not maintain both frontends as long-term product surfaces after cutover.

---

## 21. PR breakdown

The preferred implementation sequence is **six small PRs**.

The project favors narrow PRs and should generally keep each PR comfortably reviewable, targeting roughly <= 500 LOC of meaningful change where practical.

### PR 1 - WPF Settings shell and visual foundation

**Goal:** Create the standalone frontend project without changing production Settings launch behavior.

Scope:

- add `ClawHUD.Settings` WPF project;
- target .NET 10;
- framework-dependent configuration;
- enable built-in Fluent theme;
- create fixed-size main window;
- no minimize/maximize states;
- close exits Settings process;
- apply 1920 x 1200 / 150% layout baseline;
- implement five-card one-page static layout;
- no scrolling;
- establish touch-friendly spacing/control sizes;
- English labels only;
- placeholder runtime-disabled state is acceptable;
- existing Win32 Settings remains untouched as the production path.

Acceptance:

- project builds independently;
- Settings process exits when its window is closed;
- no WPF/.NET process remains afterward;
- layout fits 1920 x 1200 / 150% without scrollbars;
- touch targets meet the agreed approximate sizing;
- native ClawHUD/HUD behavior is unchanged.

Estimated change: approximately 200-350 LOC, excluding generated project metadata where appropriate.

---

### PR 2 - Protocol-v1 WPF Control IPC client

**Goal:** Allow WPF to read the existing runtime through the supported external boundary.

Scope:

- C# protocol-v1 DTOs/enums required by Settings;
- explicit little-endian frame codec compatible with the native contract;
- bounded payload/string validation matching the existing protocol requirements;
- per-session Control Pipe name derivation;
- one-request/one-response Named Pipe client;
- request-id correlation;
- `GetRuntimeInfo`;
- `GetSettingsSnapshot`;
- basic status/error mapping;
- ViewModel read-only projection from runtime snapshot;
- title becomes `ClawHUD <runtime-version>`.

Do not add mutations yet.

Acceptance:

- WPF can read the runtime version;
- WPF can display all current Settings snapshot values;
- no direct file/settings-store access exists in the WPF project;
- invalid/unavailable runtime does not produce fabricated settings;
- existing native Settings remains production frontend.

Estimated change: approximately 350-500 LOC.

---

### PR 3 - HUD Settings mutations

**Goal:** Make the primary HUD controls fully functional through Control IPC.

Scope:

- Enable HUD;
- display mode;
- HUD size +/-;
- font selection;
- alignment selection;
- background width selection;
- authoritative snapshot refresh after every mutation;
- prevent invalid size movement beyond `-2 .. +2` in the UI while still trusting runtime validation;
- simple per-control busy/error handling as needed.

Opacity preview/commit remains for PR 4.

Acceptance:

- all covered settings modify the running HUD through existing IPC;
- successful responses project their returned snapshot;
- rejected/rolled-back runtime values are reflected correctly;
- WPF never persists settings directly;
- no runtime/HUD presentation code changes are required unless a real interface defect is discovered and separately justified.

Estimated change: approximately 350-500 LOC.

---

### PR 4 - Background opacity and interaction polish

**Goal:** Implement the special live-preview interaction correctly and complete handheld interaction quality.

Scope:

- 50-100% slider;
- 5% step snapping;
- percentage label;
- `PreviewHudOpacity` during active slider tracking;
- `CommitHudOpacity` at final interaction;
- authoritative snapshot restoration after commit/failure;
- touch interaction validation;
- final control heights, padding, card spacing, and selected-state polish;
- verify no scrollbars at target resolution/scaling.

Acceptance:

- preview does not become frontend-owned persistence;
- commit persists only through runtime semantics;
- touch slider is practically usable;
- UI remains stable when runtime returns an effective value different from a transient requested value;
- no presentation contract changes.

Estimated change: approximately 150-300 LOC.

---

### PR 5 - Intel VRR Fix, Start with Windows, final frontend behavior

**Goal:** Complete the agreed Settings feature set before production cutover.

Scope:

- Intel VRR Range Fix toggle;
- Start with Windows toggle;
- final runtime-unavailable UX;
- activation-time snapshot refresh;
- final title/version behavior;
- ensure there is no About UI;
- ensure there is no localization infrastructure;
- final one-page visual cleanup without introducing new navigation or decorative complexity.

Acceptance:

- all agreed current Settings options are functional from WPF;
- window contains only the agreed one-page controls;
- title uses runtime version;
- no About page/tab exists;
- no Settings background process remains after window close.

Estimated change: approximately 200-350 LOC.

---

### PR 6 - Production cutover, VeloPack integration, legacy Win32 removal

**Goal:** Make WPF the only production Settings frontend and delete the obsolete native implementation.

Scope:

- tray `Settings` launches the WPF frontend only on explicit user action;
- implement single-Settings-instance/show-existing behavior;
- confirm ClawHUD startup does not launch Settings;
- preserve real tray `Exit` behavior;
- framework-dependent publish/packaging configuration;
- VeloPack package includes Settings application files but not a bundled .NET Desktop Runtime;
- validate installed and portable/dev behavior as applicable to current project packaging policy;
- remove `SettingsWindow` ownership from `App`;
- remove legacy Settings destroyed-message/lifecycle plumbing that becomes dead;
- remove obsolete native Settings source files/resources/tests;
- retain Control IPC and `IRuntimeControl` as the only frontend/runtime boundary;
- update project/docs/build references affected by removal.

Acceptance:

- fresh ClawHUD start leaves no Settings/.NET/WPF process running;
- Tray -> Settings opens WPF Settings;
- requesting Settings again does not create independent duplicate windows;
- closing Settings terminates the Settings process but leaves ClawHUD running;
- Tray -> Exit performs real ClawHUD runtime shutdown;
- all Settings values remain functional after cutover;
- package contains no self-contained .NET runtime payload for Settings;
- legacy native Settings implementation is gone;
- HUD presentation/VRR contract regression tests remain unchanged and passing.

Estimated change: approximately 300-500 LOC depending on build/packaging cleanup.

---

## 22. Testing strategy

### 22.1 Protocol/client tests

Add focused tests for the C# client codec/DTO behavior where practical:

- correct protocol-v1 header values;
- little-endian encoding;
- request/response request-id correlation;
- enum mapping;
- bool payload mapping;
- size offset mapping;
- opacity percent mapping;
- malformed/truncated frame rejection;
- unsupported protocol/status handling;
- maximum payload/string bounds relevant to the frontend.

Do not duplicate the entire native codec test suite without value; test the compatibility surface the C# implementation actually owns.

### 22.2 ViewModel tests

Test:

- initial snapshot projection;
- each mutation applies returned authoritative snapshot;
- failure does not leave optimistic UI state committed;
- HUD size endpoint behavior;
- opacity preview versus commit invocation;
- runtime unavailable state;
- activation refresh behavior if implemented in the ViewModel/service layer.

### 22.3 Manual UI validation

At minimum validate on the primary device configuration:

- 1920 x 1200;
- 150% scaling;
- touch input;
- mouse input;
- system light mode;
- system dark mode;
- no horizontal scroll;
- no vertical scroll;
- all card content visible;
- all selection controls can be operated by touch;
- slider can be operated reliably by touch;
- Close fully terminates `ClawHUD.Settings.exe`.

### 22.4 Lifecycle validation

Verify explicitly:

1. launch ClawHUD -> only native runtime is present;
2. do not open Settings -> no CLR/WPF process appears;
3. Tray -> Settings -> WPF process appears;
4. close Settings -> WPF process disappears;
5. runtime remains active after Settings close;
6. reopen Settings -> fresh authoritative snapshot is loaded;
7. Tray -> Exit -> runtime terminates normally;
8. no legacy Settings window can still be opened after cutover.

### 22.5 HUD regression boundary

Run existing HUD regression coverage required by the repository contract.

The frontend migration must not require changing expected behavior for:

- click-through;
- no activation;
- topmost;
- transparent hit testing;
- independent flip;
- premultiplied alpha;
- production presentation contract.

---

## 23. Error-handling principles

Keep error handling proportional to a small Settings utility.

Prefer:

- disabled controls while initial runtime state is unavailable;
- small inline status/error text if needed;
- authoritative refresh after a failed mutation;
- close when the runtime is permanently unavailable and the window cannot serve a useful purpose.

Avoid:

- modal dialogs for every transient operation failure;
- background reconnect loops;
- periodic polling solely for liveness;
- elaborate retry state machines;
- duplicating runtime rollback logic in the frontend.

---

## 24. Packaging and deployment requirements

The final package must preserve the desired idle resource model.

Required:

```text
Installed ClawHUD, Settings closed

ClawHUD.exe
  native runtime only

No ClawHUD.Settings.exe
No WPF process
No frontend-hosted CLR
```

When Settings is opened:

```text
ClawHUD.exe
ClawHUD.Settings.exe
  WPF / .NET runtime loaded only here
```

When Settings closes:

```text
ClawHUD.exe
```

The WPF publish must remain framework-dependent. Do not silently change it to self-contained in CI/release packaging.

VeloPack remains the owner of the prerequisite/runtime distribution policy; the UI refactor must not embed a private .NET Desktop Runtime into the ClawHUD application package.

---

## 25. Definition of done

The UI refactor is complete when all of the following are true:

- legacy Win32 Settings is no longer reachable or built as the product Settings frontend;
- WPF Settings is the only production Settings UI;
- Settings launches only from explicit user action;
- normal ClawHUD startup never creates the WPF process;
- closing Settings completely terminates its process and returns its memory;
- ClawHUD tray retains a real runtime `Exit` command;
- the Settings window fits without scrolling at 1920 x 1200 / 150%;
- controls are usable by touch;
- the UI is a single page with five simple untitled cards;
- HUD controls are at the top;
- Intel VRR Range Fix follows the HUD controls;
- Start with Windows is last;
- there is no About page;
- runtime version is shown with the ClawHUD title;
- UI is English only;
- WPF is framework-dependent;
- .NET Desktop Runtime is not bundled into the Settings application payload;
- the frontend uses Control IPC only;
- no WPF component directly reads/writes runtime settings persistence;
- all mutations re-project the authoritative runtime snapshot;
- HUD presentation/telemetry/game-detection internals remain outside this refactor;
- VRR-critical production presentation invariants and regression coverage are preserved.

---

## 26. Final target architecture

```text
                 explicit user action only
                         Tray -> Settings
                                |
                                v
+-------------------------------------------------------+
| ClawHUD.Settings.exe                                  |
| C# / WPF / .NET 10                                    |
| Framework-dependent                                   |
| Built-in Fluent                                       |
| One page / five untitled cards / no scrolling         |
|                                                       |
| MainWindow -> MainViewModel -> RuntimeControlClient   |
+-----------------------------+-------------------------+
                              |
                              | Control IPC protocol v1
                              | one request / one response
                              v
+-------------------------------------------------------+
| ClawHUD.exe                                           |
| Native runtime                                        |
|                                                       |
| RuntimeControlPipeServer                              |
|   -> RuntimeControlDispatchBridge                     |
|   -> ExecuteRuntimeControlRequest                     |
|   -> IRuntimeControl                                  |
|   -> App                                              |
|      -> HudController                                 |
|      -> HudSettingsStore                              |
+-------------------------------------------------------+

Settings closed:
    ClawHUD.Settings.exe exits completely.
    ClawHUD.exe continues running.

Tray Exit:
    ClawHUD.exe follows its real normal shutdown path.
```

This separation is the core design constraint for the migration: **modernize the frontend without moving UI technology, persistence authority, or frontend lifetime cost into the always-running HUD runtime.**
