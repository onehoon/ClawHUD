# CH-RTF-1 — Runtime Message Window Extraction Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** ClawHUD Runtime / Frontend Separation  
> **Architecture source:** `docs/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md`  
> **PR plan source:** `docs/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_PR_PLAN_2026-09-02.md`  
> **Scope:** Behavior-preserving runtime message-window extraction only  
> **Status:** Ready for implementation

---

## 1. Objective

Extract the runtime-owned hidden Win32 message window from the current `TrayIcon` implementation without changing any user-visible ClawHUD behavior.

The current production implementation uses the hidden HWND created by `TrayIcon` for two unrelated responsibilities:

```text
Tray shell
- Shell_NotifyIcon
- tray callbacks
- tray context menu
- Settings / Exit commands
- TaskbarCreated recovery

Runtime infrastructure
- WM_HOTKEY / F8 HUD toggle
- WM_POWERBROADCAST suspend/resume
- WM_TIMER delivery
- ProductionTelemetryController message/timer HWND
- GameSessionController message HWND
- App-private asynchronous Settings-destroyed message target
```

This coupling blocks the future Managed mode because Managed mode must be able to omit the tray while keeping all runtime message infrastructure alive.

The target after this PR is:

```text
App
├─ RuntimeMessageWindow
│  ├─ dedicated hidden HWND
│  ├─ WM_HOTKEY / F8
│  ├─ WM_POWERBROADCAST
│  ├─ WM_TIMER
│  ├─ telemetry message/timer target
│  ├─ game-session message target
│  └─ App-private runtime message target
│
└─ TrayIcon
   ├─ its own tray callback HWND
   ├─ Shell_NotifyIcon
   ├─ TaskbarCreated handling
   ├─ tray menu
   └─ Settings / Exit commands
```

For this PR, **Standalone may have two hidden HWNDs**: one dedicated to runtime infrastructure and one retained by `TrayIcon` for tray callbacks. This is intentional. Do not attempt to share one HWND merely to reduce window count.

The purpose of this PR is to create a safe runtime/tray boundary, not to optimize hidden-window count.

---

## 2. Current production baseline

Current `TrayIcon::Create()` creates the hidden window class:

```text
ClawHUD.TrayMessageWindow
```

The same `TrayIcon::Window()` is currently used by `App` for:

```cpp
productionTelemetry_.Bind(tray_.Window(), ...);
gameSession_.BindMessageWindow(tray_.Window());
RegisterHotKey(tray_.Window(), kHudToggleHotkeyId, MOD_NOREPEAT, VK_F8);
```

Resume recovery also arms timers against `tray_.Window()`.

`App::PostSettingsDestroyed()` posts the private `kSettingsDestroyed` message to `tray_.Window()`.

`App::StopRuntimeSources()` unregisters F8 from `tray_.Window()`.

Inside `TrayIcon::WindowProc`, the current hidden HWND handles:

```text
WM_HOTKEY
WM_POWERBROADCAST
WM_TIMER
TaskbarCreated
tray left-click callback
tray right-click callback
```

The production message loop in `App::ProcessMessages()` remains the top-level dispatcher for:

```text
kSettingsDestroyed
gameSession_.HandleMessage(message)
Settings dialog navigation
TranslateMessage / DispatchMessage
```

Do not redesign that message-loop architecture in this PR.

---

## 3. Required new component

Add:

```text
src/ClawHUD/RuntimeMessageWindow.h
src/ClawHUD/RuntimeMessageWindow.cpp
```

The component should be a small native Win32 owner for the runtime-only hidden HWND.

A suitable shape is conceptually:

```cpp
class RuntimeMessageWindow
{
public:
    explicit RuntimeMessageWindow(App& app);
    ~RuntimeMessageWindow();

    bool Create(HINSTANCE instance);
    void Destroy();
    HWND Window() const noexcept;

private:
    static LRESULT CALLBACK WindowProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    App& app_;
    HINSTANCE instance_{};
    HWND window_{};
    HPOWERNOTIFY suspendResumeNotification_{};
};
```

This is illustrative, not an ABI requirement.

For this first PR, retaining a narrow direct `App&` callback dependency inside `RuntimeMessageWindow` is acceptable and preferred over introducing a generic callback bus or abstraction layer.

Do not add a generic message router, DI framework, command bus, or service framework.

---

## 4. Runtime window responsibilities

`RuntimeMessageWindow` must own the following behavior currently located in `TrayIcon`.

### 4.1 Hidden HWND lifetime

Create a dedicated hidden HWND with its own class name, for example:

```text
ClawHUD.RuntimeMessageWindow
```

The exact name may vary, but it must not reuse `ClawHUD.TrayMessageWindow`.

Recommended window characteristics should remain non-visible and non-activating, equivalent to the current hidden infrastructure intent:

```text
WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
WS_POPUP
0 x 0 hidden window
```

This window is **not the HUD presentation window**. Do not touch any HUD presentation styles or presentation code while creating it.

### 4.2 Suspend/resume notification ownership

Move:

```text
RegisterSuspendResumeNotification
UnregisterSuspendResumeNotification
```

from `TrayIcon` to `RuntimeMessageWindow`.

`WM_POWERBROADCAST` behavior must remain exactly:

```text
PBT_APMSUSPEND
    -> App::HandleSystemSuspend()

PBT_APMRESUMEAUTOMATIC
    -> App::HandleSystemResume()

PBT_APMRESUMESUSPEND
    -> no additional action
```

Preserve the current warning behavior if suspend/resume notification registration fails.

### 4.3 F8 hotkey delivery

Move `WM_HOTKEY` handling to `RuntimeMessageWindow`.

The existing F8 feature must remain in this PR.

Keep:

```text
hotkey id: kHudToggleHotkeyId
modifier: MOD_NOREPEAT
key: VK_F8
callback: App::HandleHudToggleHotkey()
```

Do not change F8 semantics, persistence behavior, manual-override semantics, or Settings refresh behavior.

The only change is which hidden HWND receives the registered hotkey.

### 4.4 Runtime timer delivery

Move `WM_TIMER` dispatch to `RuntimeMessageWindow`:

```cpp
app_.HandleTimer(static_cast<UINT_PTR>(wParam));
```

Do not change timer IDs, intervals, start/stop policy, or timer semantics.

This includes the existing production telemetry timers and App-owned resume-recovery timer infrastructure.

---

## 5. `App` integration changes

Add one `RuntimeMessageWindow` member owned by `App`.

Conceptually:

```cpp
RuntimeMessageWindow runtimeMessageWindow_;
TrayIcon tray_;
```

Construct both through the existing composition root.

### 5.1 Startup order

In `App::Run()`, after existing startup checks and before runtime components are bound to an HWND:

```text
AcquireSingleInstance
CheckForUpdates
hardware support check
ApplyStartupRegistration
RuntimeMessageWindow::Create
TrayIcon::Create
runtime component binding/startup
```

The exact local ordering between runtime-window creation and tray creation may be adjusted for cleanup safety, but the runtime HWND must exist before:

```text
ProductionTelemetryController::Bind
GameSessionController::BindMessageWindow
RegisterHotKey
resume/runtime timers can be armed
```

If runtime message-window creation fails, log an error and fail startup rather than silently falling back to the tray HWND.

Do not retain a compatibility fallback to `tray_.Window()`.

### 5.2 Replace runtime uses of `tray_.Window()`

Every runtime-facing use must move to:

```cpp
runtimeMessageWindow_.Window()
```

At minimum, update:

```text
ProductionTelemetryController::Bind(...)
GameSessionController::BindMessageWindow(...)
RegisterHotKey(...)
UnregisterHotKey(...)
SetTimer(... kResumeRecoveryTimerId ...)
KillTimer(... kResumeRecoveryTimerId ...), where currently bound to tray HWND
App::PostSettingsDestroyed()
```

Search the full production tree for all uses of `tray_.Window()` and classify each occurrence.

After this PR, `tray_.Window()` should only be used for tray-shell behavior, never runtime telemetry/game-session/hotkey/power/timer ownership.

### 5.3 Settings-destroyed asynchronous notification

Preserve the current asynchronous destruction contract:

```text
SettingsWindow WM_NCDESTROY
    -> App::PostSettingsDestroyed()
    -> PostMessage(... kSettingsDestroyed ...)
    -> App::ProcessMessages()
    -> App::SettingsDestroyed()
    -> settings_.reset()
```

Only the destination HWND changes from the tray HWND to the runtime message HWND.

Do not make Settings destruction synchronous.

Do not reset `settings_` directly from `SettingsWindow::WM_NCDESTROY`.

### 5.4 Shutdown order

Preserve current runtime-source stop semantics.

Before destroying `RuntimeMessageWindow`:

```text
CancelResumeRecovery
StopProductionSampling
StopGraphicsApiProbe
gameSession_.StopSources
debugObservation_->Stop
UnregisterHotKey
```

Then destroy Settings/tray/runtime windows in a safe deterministic order.

The exact shell-destruction ordering may be:

```text
StopRuntimeSources
settings_.reset()
tray_.Destroy()
runtimeMessageWindow_.Destroy()
PostQuitMessage
```

or an equivalent order that guarantees no runtime source posts into a destroyed runtime HWND.

`App::~App()` must remain safe and idempotent if `Exit()` already performed explicit cleanup.

---

## 6. `TrayIcon` changes

After this PR, `TrayIcon` remains a Standalone shell component and may continue to own its existing tray callback HWND.

This PR does **not** need to remove `App&` from `TrayIcon`; that is intentionally deferred to the next PR in the series.

### 6.1 Keep in `TrayIcon`

Retain:

```text
ClawHUD.TrayMessageWindow
Shell_NotifyIcon state
TaskbarCreated registration and re-add behavior
kTrayMessage callback
left-click -> OpenSettings
right-click -> tray menu
Settings menu command
Exit menu command
```

### 6.2 Remove from `TrayIcon`

Remove:

```text
RegisterSuspendResumeNotification
UnregisterSuspendResumeNotification
HPOWERNOTIFY suspendResumeNotification_
WM_POWERBROADCAST handling
WM_HOTKEY handling
WM_TIMER handling
```

`TrayIcon` must no longer be a runtime-event owner.

### 6.3 Do not optimize into one HWND

Do not route the visual tray icon through the new runtime HWND in this PR.

Keeping a dedicated tray callback HWND avoids introducing forwarding logic between `RuntimeMessageWindow` and `TrayIcon` while the architecture is being separated.

A hidden HWND has negligible cost compared with the architectural clarity gained here.

A later cleanup may revisit window consolidation only if there is a demonstrated reason. It is not part of this series' requirement.

---

## 7. CMake/build changes

Add the new production source files to the existing `ClawHUD` CMake target:

```text
src/ClawHUD/RuntimeMessageWindow.cpp
src/ClawHUD/RuntimeMessageWindow.h
```

Do not redesign the CMake structure.

Do not create a new executable or DLL for this PR.

The production application remains one `ClawHUD.exe`.

---

## 8. Test strategy

Add focused coverage where practical, but do not create tests that attempt to validate speculative implementation details.

### 8.1 Required regression behavior

Verify that a normal Standalone launch still has:

```text
exactly one visible system-tray icon
Settings opens from tray left-click
Settings opens from tray menu
Exit still terminates cleanly
Taskbar/Explorer recreation can re-add the tray icon
```

### 8.2 F8 regression

Verify that F8 still reaches the same existing application behavior after moving registration to `RuntimeMessageWindow`.

The PR must not remove or redefine the hotkey.

### 8.3 Suspend/resume regression

Verify the runtime message window receives power notifications and forwards them into the existing App suspend/resume paths.

Do not change `SuspendResumePolicy` or recovery semantics.

### 8.4 Timer/message target regression

Verify:

```text
ProductionTelemetryController binds to RuntimeMessageWindow HWND
GameSessionController binds to RuntimeMessageWindow HWND
resume-recovery SetTimer/KillTimer use RuntimeMessageWindow HWND
Settings-destroyed PostMessage uses RuntimeMessageWindow HWND
```

The existing top-level `App::ProcessMessages()` dispatch sequence must remain valid.

### 8.5 Build/test gate

Run the normal supported Debug/Release builds and the full applicable CTest suite.

Preserve all existing HUD presentation/VRR assertions.

---

## 9. HUD / VRR safety contract — non-negotiable

This PR is a hidden runtime-message-window refactor only.

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

The new `RuntimeMessageWindow` is unrelated to the HUD presentation HWND.

Do not reuse or alter `HudPresentation` window creation as part of this work.

Do not change background opacity behavior.

Existing regression tests/assertions for click-through, no activation, topmost behavior, transparent hit testing, independent flip, premultiplied alpha, and the production presentation contract must remain intact.

---

## 10. Explicit non-goals

Do not include any of the following in this PR:

```text
--managed argument parsing
Managed mode
tray suppression
Control IPC
Named Pipe server/client
runtime-control/settings interface extraction
WinUI 3 / WPF / Web UI work
SteamAddon integration
settings schema changes
startup-registration policy changes
Velopack/update-policy changes
single-instance behavior changes
F8 removal
EC helper changes
PresentMon changes
game-detection changes
HUD renderer/presentation changes
new generic shell/runtime framework
```

Those belong to later PRs in the already-defined series.

---

## 11. Expected file-level direction

### New

```text
src/ClawHUD/RuntimeMessageWindow.h
src/ClawHUD/RuntimeMessageWindow.cpp
```

### Modify

```text
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/TrayIcon.h
src/ClawHUD/TrayIcon.cpp
CMakeLists.txt
```

### Optional focused tests

```text
tests/...RuntimeMessageWindow...
cmake/ClawHUDTests.cmake
```

Do not mechanically add a test executable if the Windows message behavior cannot be tested meaningfully without brittle live-desktop assumptions. Prefer stable unit-testable seams and preserve the existing integration/manual verification path where appropriate.

---

## 12. Acceptance criteria

The PR is complete only when all of the following are true:

1. `RuntimeMessageWindow` owns a dedicated hidden runtime HWND.
2. `TrayIcon` no longer owns suspend/resume notification registration.
3. `TrayIcon::WindowProc` no longer handles `WM_HOTKEY`.
4. `TrayIcon::WindowProc` no longer handles `WM_POWERBROADCAST`.
5. `TrayIcon::WindowProc` no longer handles `WM_TIMER`.
6. F8 remains functional through `RuntimeMessageWindow`.
7. Suspend/resume reaches the same existing App handlers through `RuntimeMessageWindow`.
8. Production telemetry binds to the runtime HWND, not the tray HWND.
9. Game-session message delivery targets the runtime HWND, not the tray HWND.
10. Resume-recovery timers use the runtime HWND.
11. `App::PostSettingsDestroyed()` posts to the runtime HWND while preserving asynchronous destruction.
12. Normal Standalone tray appearance and menu behavior are unchanged.
13. Existing Settings behavior is unchanged.
14. Current one-instance/startup/update behavior is unchanged.
15. No Managed mode has been introduced yet.
16. No HUD/VRR presentation contract code has changed.
17. Supported builds and applicable tests pass.

---

## 13. Completion report expectations

When implementation is complete, report:

```text
- files added/changed;
- exact responsibilities moved from TrayIcon to RuntimeMessageWindow;
- all production call sites migrated from tray_.Window() to runtimeMessageWindow_.Window();
- confirmation that F8 was preserved;
- confirmation that Settings destruction remains asynchronous;
- confirmation that normal tray UX is unchanged;
- Debug/Release build results;
- CTest results;
- confirmation that HUD/VRR presentation files/contracts were not changed.
```

If any runtime behavior appears to require changing the HUD presentation contract, stop and report the conflict instead of making that change.
