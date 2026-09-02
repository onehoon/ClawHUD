# CH-RTF-2 — Tray Shell Callback Decoupling Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** ClawHUD Runtime / Frontend Separation  
> **Architecture source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md`  
> **PR plan source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_PR_PLAN_2026-09-02.md`  
> **Previous PR:** #209 — CH-RTF-1 Runtime Message Window Extraction  
> **Scope:** Behavior-preserving Tray shell callback decoupling only  
> **Status:** Ready for implementation

---

## 1. Objective

Remove the remaining direct dependency from `TrayIcon` to the concrete `App` class.

PR #209 already completed the important runtime-window separation:

```text
RuntimeMessageWindow
- F8 / WM_HOTKEY
- suspend/resume / WM_POWERBROADCAST
- WM_TIMER
- ProductionTelemetryController HWND
- GameSessionController HWND
- App-private async message target

TrayIcon
- Shell_NotifyIcon
- tray callback HWND
- TaskbarCreated recovery
- tray menu
- Settings command
- Exit command
```

After that PR, `TrayIcon` no longer needs any runtime behavior from `App`.

The only remaining concrete coupling is:

```cpp
App& app_;
```

used for exactly two shell actions:

```text
App::OpenSettings()
App::Exit()
```

This PR must replace that broad `App&` dependency with a tiny explicit Tray shell action boundary.

Target shape:

```text
App
  |
  | supplies two callbacks
  v
TrayIcon
  |- openSettings
  `- exit
```

`TrayIcon` must not know that `App` exists after this PR.

This is still a **behavior-preserving refactor**. Do not add Managed mode, launch-mode parsing, IPC, new Settings behavior, or frontend migration here.

---

## 2. Current production baseline after PR #209

Current `TrayIcon.h` still forward-declares:

```cpp
class App;
```

and stores:

```cpp
App& app_;
```

The constructor is currently:

```cpp
explicit TrayIcon(App& app);
```

`TrayIcon.cpp` currently includes:

```cpp
#include "App.h"
```

and invokes the application directly from tray-shell input paths:

```cpp
case kSettingsCommand:
    app_.OpenSettings();
    break;

case kExitCommand:
    app_.Exit();
    break;
```

and:

```cpp
if (message == kTrayMessage && lParam == WM_LBUTTONUP)
{
    app_.OpenSettings();
    return 0;
}
```

No runtime event path remains in `TrayIcon` after PR #209.

Do not reintroduce any runtime responsibility into `TrayIcon`.

---

## 3. Required design

Introduce one small callback/action structure dedicated to Tray shell actions.

A suitable design is conceptually:

```cpp
struct TrayActions
{
    std::function<void()> openSettings;
    std::function<void()> exit;
};
```

and:

```cpp
class TrayIcon
{
public:
    explicit TrayIcon(TrayActions actions);
    ...

private:
    TrayActions actions_;
};
```

The exact spelling may vary slightly, but preserve these design rules:

1. The boundary contains only the actions the tray actually needs.
2. `TrayIcon` must not include or forward-declare `App`.
3. `TrayIcon.cpp` must not include `App.h`.
4. Do not create an abstract application-wide interface just for these two actions.
5. Do not create a generic command bus, message broker, service locator, DI framework, or shell framework.
6. Do not move `OpenSettings()` or `Exit()` implementation out of `App` in this PR.

A small value-type callback structure is preferred because the lifetime is simple and `App` remains the composition root.

---

## 4. `App` composition changes

`App` remains responsible for wiring the Tray shell to application behavior.

Update construction from the current conceptual form:

```cpp
App::App(HINSTANCE instance)
    : instance_(instance),
      runtimeMessageWindow_(*this),
      tray_(*this)
{
    ...
}
```

to a narrow callback composition equivalent to:

```cpp
App::App(HINSTANCE instance)
    : instance_(instance),
      runtimeMessageWindow_(*this),
      tray_(TrayActions{
          [this] { OpenSettings(); },
          [this] { Exit(); }})
{
    ...
}
```

This example is illustrative. Match the repository's compiler/language style rather than forcing this exact formatting.

Important lifetime rule:

- the callbacks may capture `this` because `TrayIcon` is owned by the same `App` instance;
- `App` destroys the tray before its own lifetime ends;
- do not add heap-owned shared state or `shared_ptr<App>` merely to support these callbacks.

Keep the current explicit `tray_.Destroy()` cleanup behavior intact.

---

## 5. `TrayIcon` changes

### 5.1 Remove concrete application knowledge

Remove from `TrayIcon.h`:

```cpp
class App;
```

Remove from `TrayIcon.cpp`:

```cpp
#include "App.h"
```

Replace:

```cpp
App& app_;
```

with the narrow action/callback state.

### 5.2 Left-click behavior

Preserve the current behavior exactly:

```text
tray left click
-> Open Settings
```

The only change is the call path:

```text
Before
TrayIcon -> App::OpenSettings()

After
TrayIcon -> openSettings callback -> App::OpenSettings()
```

Do not change click gesture semantics, debounce behavior, activation behavior, or Settings lifetime.

### 5.3 Context menu behavior

Preserve the current menu exactly:

```text
ClawHUD   (disabled heading)
---------------------------
Settings
Exit
```

Preserve command behavior:

```text
Settings -> openSettings callback
Exit     -> exit callback
```

Do not add menu items or Managed-mode UI in this PR.

### 5.4 Callback validity

Because these actions are required production dependencies, prefer construction that makes the intended callbacks explicit.

If the implementation permits empty `std::function` values, do not allow an empty callback to crash the tray message path. A simple guarded invocation is sufficient if needed:

```cpp
if (actions_.openSettings)
    actions_.openSettings();
```

Do not introduce elaborate error handling for an impossible production composition error.

---

## 6. `RuntimeMessageWindow` must remain unchanged in responsibility

PR #209 established the runtime-shell boundary.

Do not move any of these back into `TrayIcon`:

```text
WM_HOTKEY / F8
WM_POWERBROADCAST
WM_TIMER
RegisterSuspendResumeNotification
ProductionTelemetryController message target
GameSessionController message target
resume-recovery timer target
App-private runtime message target
```

`RuntimeMessageWindow` may still retain its current narrow direct `App&` dependency in this PR.

That is intentional.

`RuntimeMessageWindow` is runtime infrastructure and its application/runtime callback boundary will be addressed only if a later concrete need appears. Do not broaden CH-RTF-2 into a second callback refactor.

---

## 7. Do not introduce Managed mode yet

This PR prepares the Tray to become an optional Standalone shell component later.

It must **not** yet implement:

```text
--managed command-line parsing
launch-mode enum/state
conditional tray creation
no-tray execution
Control IPC
SteamAddon process ownership
single-instance mode reconciliation
mode-aware update behavior
```

Normal startup must still create:

```text
RuntimeMessageWindow
+
TrayIcon
```

exactly as after PR #209.

The first actual optional/no-tray composition is scheduled later in the series after the runtime-control and IPC foundations exist.

---

## 8. Files expected to change

Primary production files:

```text
src/ClawHUD/TrayIcon.h
src/ClawHUD/TrayIcon.cpp
src/ClawHUD/App.cpp
```

`App.h` should only change if required by the chosen callback type placement or include cleanup.

A new source file should not be necessary.

If the action structure is small, define it next to `TrayIcon` rather than creating a generic shell-abstraction module.

No CMake source-list change should be needed unless the implementation unexpectedly adds a file; avoid doing so without a concrete reason.

---

## 9. Testing and verification

This is primarily a compile-time ownership cleanup, but the user-visible tray behavior must remain intact.

### 9.1 Static/code verification

Confirm all of the following after implementation:

```text
TrayIcon.h does not mention App
TrayIcon.cpp does not include App.h
TrayIcon stores no App* / App&
TrayIcon invokes only narrow Tray actions
RuntimeMessageWindow still owns runtime system-event delivery
```

Search the production tree for `TrayIcon` construction and ensure `App` is the composition point supplying the actions.

### 9.2 Tray behavior regression

Verify, where an interactive desktop is available:

```text
normal ClawHUD launch creates one tray icon
left-click opens Settings
right-click opens the same context menu
Settings menu item opens Settings
Exit menu item exits ClawHUD cleanly
Explorer/Taskbar recreation still restores the tray icon
```

### 9.3 Settings lifetime regression

Do not alter the current Settings ownership/lifetime sequence.

Closing Settings must still follow the existing asynchronous destruction path established by the runtime message window.

### 9.4 Runtime regression

This PR should not affect runtime behavior, but verify that the existing build/test suite still protects:

```text
F8 HUD toggle
suspend/resume policy
telemetry/game-session behavior
HUD settings persistence
HUD presentation contract
```

### 9.5 Build/test gate

Run the repository's supported Release build and full applicable CTest suite used by CI.

Also run Debug locally when available, consistent with the previous PR verification practice.

Do not weaken or exclude existing tests to make this refactor pass.

---

## 10. HUD / VRR safety contract — non-negotiable

This PR is a Tray shell dependency cleanup only.

Do **not** modify, replace, weaken, or work around any production HUD presentation behavior, including:

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

Do not modify `HudPresentation` or `HudRenderer` as part of this task.

Do not change background opacity behavior.

Existing regression assertions for click-through, no activation, topmost behavior, transparent hit testing, independent flip, premultiplied alpha, and the production presentation contract must remain intact.

---

## 11. Out of scope

Explicitly out of scope:

```text
Managed mode
Control IPC
Named Pipe server/client
runtime-control settings interface
legacy Settings migration
new standalone Settings frontend
WinUI 3 / WPF / Web UI
SteamAddon integration
startup/update policy changes
single-instance policy changes
F8 removal
runtime message-window redesign
HUD/renderer changes
telemetry changes
game-detection changes
EC helper changes
VRR tweak changes
```

Do not opportunistically combine CH-RTF-3 work into this PR.

---

## 12. Acceptance criteria

The PR is complete only when all of the following are true:

- `TrayIcon` has no concrete `App` dependency.
- `TrayIcon.h` no longer forward-declares `App`.
- `TrayIcon.cpp` no longer includes `App.h`.
- Tray shell behavior is supplied through a minimal two-action callback boundary or equivalently narrow design.
- Left-click still opens Settings.
- Context-menu Settings still opens Settings.
- Context-menu Exit still exits the application.
- Taskbar recreation behavior is unchanged.
- `RuntimeMessageWindow` continues to own all runtime message infrastructure introduced by PR #209.
- F8 remains enabled and unchanged.
- Settings destruction/lifetime behavior is unchanged.
- Normal startup still always creates the tray.
- No Managed mode or IPC implementation is added.
- No production HUD presentation/VRR contract code is changed.
- Supported build and applicable full test suite pass.

---

## 13. Expected result after CH-RTF-2

The production ownership should read conceptually as:

```text
App
├─ RuntimeMessageWindow(App runtime callbacks)
│  ├─ F8
│  ├─ power
│  ├─ timers
│  └─ runtime HWND delivery
│
└─ TrayIcon(TrayActions)
   ├─ openSettings callback
   ├─ exit callback
   ├─ Shell_NotifyIcon
   ├─ TaskbarCreated recovery
   └─ tray menu / clicks
```

The important architectural property is:

> **The Standalone tray shell no longer imports or controls the whole application; it only emits the two shell intents it owns.**

This leaves the next PR (`CH-RTF-3`) free to introduce the in-process runtime-control contract and migrate the legacy Win32 Settings frontend without carrying unnecessary Tray/Application coupling forward.
