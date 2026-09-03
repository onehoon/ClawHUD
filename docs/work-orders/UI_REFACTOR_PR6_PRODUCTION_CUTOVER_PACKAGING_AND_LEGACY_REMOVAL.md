# UI Refactor PR6 — Production Cutover, Packaging, Single-Instance Activation, and Legacy Win32 Settings Removal

**Date:** 2026-09-03  
**Status:** Ready for implementation  
**Reviewed baseline:** `main` at `86600b4c59f2585584437f7b8746b63ce4cc7b93`  
**Previous PR:** #227 — Final Settings mutations, activation refresh, runtime-loss close  
**Parent plan:** `docs/UI refact/CLAW_HUD_SETTINGS_WPF_UI_REFACTOR_PLAN_2026-09-03.md`

---

## 1. Objective

Complete the Settings UI migration by making the WPF frontend the **only production Settings frontend**.

PR1–PR5 already completed the WPF frontend itself:

- WPF/.NET 10 Fluent shell;
- read-only Control IPC client;
- ordinary HUD setting mutations;
- Background Opacity Preview/Commit semantics;
- Intel VRR Range Fix and Start with Windows mutations;
- authoritative post-mutation snapshot reconciliation;
- activation-time refresh without polling;
- runtime-loss handling.

PR6 is therefore a **cutover and packaging PR**, not another Settings-feature PR.

At the end of PR6, production behavior must be:

```text
Windows / user starts ClawHUD
    -> ClawHUD.exe only
    -> native runtime + HUD + tray + Control IPC
    -> ClawHUD.Settings.exe is NOT running

Tray -> Settings
    -> launch sibling ClawHUD.Settings.exe
    -> WPF Settings becomes visible
    -> connects to the already-running ClawHUD Control IPC

Tray -> Settings again while WPF Settings is open
    -> no second Settings window / ViewModel / IPC client
    -> existing Settings window is brought forward

Close WPF Settings
    -> ClawHUD.Settings.exe exits
    -> WPF/CLR memory is released
    -> ClawHUD.exe and HUD continue running

Tray -> Exit
    -> existing native ClawHUD shutdown path
```

The old in-process Win32 `SettingsWindow` implementation must be removed completely from production source/build/test ownership.

This is the final implementation PR for the Settings UI refactor.

---

## 2. Development-stage cutover rule

ClawHUD is still under active development and the WPF Settings frontend has not shipped.

Perform a **clean cutover**.

Do not keep:

- a Win32/WPF frontend selector;
- fallback to the old `SettingsWindow`;
- a hidden compatibility route;
- old Settings lifetime members kept "just in case";
- duplicate Settings process-management implementations;
- an environment variable or INI switch choosing the old frontend.

Once the WPF launch path is wired, the legacy Win32 Settings code should be deleted in this same PR.

Deletion-heavy diff size is expected. The usual small-PR preference applies to **new meaningful implementation**, not to removing obsolete legacy files.

Target new implementation size should remain roughly **250–450 meaningful LOC**, excluding deleted Win32 Settings code and generated/project-file churn.

---

## 3. Latest source baseline

### 3.1 Current `main`

Reviewed baseline:

```text
86600b4c59f2585584437f7b8746b63ce4cc7b93
```

This is the squash merge of PR #227.

### 3.2 Current native Settings ownership

`App.cpp` still includes the legacy frontend:

```cpp
#include "SettingsWindow.h"
```

and reserves:

```cpp
constexpr UINT kSettingsDestroyed = WM_APP + 1;
```

The tray callback currently resolves to:

```cpp
tray_(TrayActions{
    [this] { OpenSettings(); },
    [this] { Exit(); }
})
```

`OpenSettings()` currently owns an in-process `SettingsWindow`:

```cpp
if (settings_)
{
    settings_->Show(instance_);
    return;
}

settings_ = std::make_unique<SettingsWindow>(
    static_cast<clawhud::IRuntimeControl&>(*this),
    [this] { PostSettingsDestroyed(); });
```

The legacy lifetime also requires:

```text
PostSettingsDestroyed()
SettingsDestroyed()
settings_.reset()
kSettingsDestroyed
IsDialogMessageW(settings_->Window(), ...)
```

All of this becomes obsolete after WPF cutover.

### 3.3 Current tray implementation

`TrayIcon` already provides exactly the required product commands:

```text
Settings
Exit
```

and forwards them through `TrayActions`.

Do **not** redesign `TrayIcon`.

PR6 only changes what `App::OpenSettings()` does when the existing Settings action fires.

### 3.4 Current WPF project

`src/ClawHUD.Settings/ClawHUD.Settings.csproj` already defines:

```xml
<TargetFramework>net10.0-windows</TargetFramework>
<UseWPF>true</UseWPF>
<PlatformTarget>x64</PlatformTarget>
<SelfContained>false</SelfContained>
```

This remains the production deployment model.

Do not change WPF to self-contained.

### 3.5 Current WPF startup

`App.xaml` still uses:

```xml
StartupUri="MainWindow.xaml"
ShutdownMode="OnMainWindowClose"
```

That was appropriate while the WPF process always represented one window.

PR6 needs a small explicit startup coordinator so a second `ClawHUD.Settings.exe` can act only as an **activation relay** and exit without constructing a second `MainWindow` / ViewModel / Control client.

### 3.6 Current release workflow

`.github/workflows/Build-Release.yml` currently:

- sets up .NET 8 only for the Velopack CLI;
- builds/tests the native runtime;
- stages only the native ClawHUD payload;
- does **not** publish/stage WPF Settings;
- packs with:

```text
--framework vcredist145-x64
```

The WPF frontend is currently built only in `Build-Test.yml`; it is not part of release staging.

PR6 must close this gap.

---

## 4. Final architecture after cutover

### 4.1 Process ownership

Final process model:

```text
ClawHUD.exe
  - native runtime authority
  - HUD
  - telemetry/game detection
  - tray
  - Control IPC server
  - updater

ClawHUD.Settings.exe
  - short-lived WPF frontend only
  - session-scoped single Settings instance
  - Control IPC client
  - no persisted runtime ownership
```

Do not host CLR/WPF inside `ClawHUD.exe`.

Do not keep a native `SettingsWindow` object alive.

### 4.2 Runtime authority remains native

The WPF frontend remains a Control IPC consumer only:

```text
ClawHUD.Settings.exe
    -> \\.\pipe\ClawHUD.Control.<sessionId>
    -> RuntimeControlPipeServer
    -> RuntimeControlDispatchBridge
    -> IRuntimeControl / App
```

PR6 must not move settings persistence into WPF.

### 4.3 Only one ClawHUD runtime already exists per session

The native runtime already uses:

```text
Local\ClawHUD.SingleInstance
```

Therefore a session-scoped Settings singleton is sufficient for the current product architecture: one native runtime exists for that session, and one WPF Settings frontend controls it.

Do not introduce runtime-PID-specific Settings instances.

---

## 5. HUD / VRR presentation contract — non-negotiable zero-touch boundary

This PR changes the Settings shell and packaging only.

Do **not** modify, replace, weaken, or work around:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- current `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirements;
- Presentation API / DirectComposition production path;
- premultiplied-alpha contract.

Expected source diff in:

```text
HudPresentation*
HudRenderer*
production presentation contract code
```

is **zero**.

Do not use the Settings cutover as a reason to change HUD opacity, renderer alpha, window styles, hit testing, presentation buffers, or VRR behavior.

Existing HUD/VRR regression tests must remain green.

---

## 6. Native tray -> WPF launch cutover

Keep the existing tray callback:

```cpp
[this] { OpenSettings(); }
```

but change `App::OpenSettings()` from "construct/show native SettingsWindow" to "launch WPF frontend executable".

### 6.1 Resolve Settings executable as a sibling

Use the actual running native executable path already stored in:

```text
executablePath_
```

Resolve:

```text
<directory containing ClawHUD.exe>\ClawHUD.Settings.exe
```

Example installed/current layout:

```text
...\ClawHUD\current\ClawHUD.exe
...\ClawHUD\current\ClawHUD.Settings.exe
```

Do not:

- search `%PATH%`;
- search the current working directory;
- use a hard-coded install root;
- assume `Program Files`;
- launch `dotnet ClawHUD.Settings.dll`;
- depend on the caller's working directory.

The sibling path keeps the launcher correct across Velopack `current` version swaps and portable extraction layouts.

### 6.2 Prefer a small native launcher helper

Do not put several pages of shell/process logic directly into `App.cpp`.

A small focused helper is acceptable, for example:

```text
src/ClawHUD/SettingsFrontendLauncher.h
src/ClawHUD/SettingsFrontendLauncher.cpp
```

Possible responsibilities:

```cpp
std::filesystem::path SettingsFrontendPath(
    const std::filesystem::path& runtimeExecutable);

bool LaunchSettingsFrontend(
    const std::filesystem::path& runtimeExecutable);
```

Exact API is implementation choice.

Keep it narrow; do not create a generic process-management framework.

### 6.3 Launch semantics

Use an absolute sibling path with a normal unelevated launch method such as `ShellExecuteExW` or equivalent.

Required behavior:

```text
launch succeeds
    -> return immediately
    -> native runtime does NOT wait for Settings to close

launch fails / file missing
    -> no crash
    -> log a clear error
    -> keep native runtime/HUD running
```

A small user-visible error message is acceptable for a missing/corrupt Settings frontend because Tray -> Settings is an explicit user action.

Do not elevate WPF Settings.

Do not use `runas`.

Do not start a second `ClawHUD.exe`.

### 6.4 Do not make native runtime own WPF lifetime

The preferred final design is:

> Every Tray -> Settings action may start the WPF executable, and the WPF executable itself decides whether it becomes the primary Settings process or merely signals the already-running primary and exits.

Therefore native `App` does **not** need:

- a persistent child process handle;
- a Settings PID member;
- a WaitForSingleObject polling loop;
- a child-process exit callback;
- WPF window enumeration;
- child restart logic.

This keeps Settings memory/lifetime completely outside the always-running native runtime.

---

## 7. WPF session-scoped single-instance / bring-to-front

PR6 must guarantee one visible Settings frontend per Windows session.

### 7.1 Move startup out of `StartupUri`

Remove `StartupUri="MainWindow.xaml"` from `App.xaml`.

Create the main window explicitly only after single-instance ownership has been established.

Conceptually:

```text
App.OnStartup
  -> establish session-scoped activation objects
  -> attempt Settings singleton ownership

  secondary instance:
      -> signal primary
      -> exit without MainWindow

  primary instance:
      -> register activation signal handler
      -> create MainWindow
      -> show MainWindow
      -> normal WPF lifetime
```

Keep close semantics:

```text
close MainWindow
    -> Settings process exits
    -> named objects released
```

No hide-to-tray behavior.

### 7.2 Names must be Local + session-scoped

Use deterministic names based on the current Windows session, for example:

```text
Local\ClawHUD.Settings.<sessionId>
Local\ClawHUD.Settings.Activate.<sessionId>
```

Exact names are implementation choice.

The session ID should come from the current process/session, not from configuration.

Do not use one machine-global Settings singleton across all user sessions.

### 7.3 Recommended synchronization shape

A minimal named-object design is preferred:

```text
named Mutex          -> primary ownership
named AutoResetEvent -> activation signal
```

or an equivalent small design.

Avoid:

- localhost TCP;
- another application protocol;
- a second named pipe RPC stack;
- filesystem lock files;
- periodic process scans;
- polling.

### 7.4 Startup race must be handled

The first process may still be constructing the WPF window when a second launch occurs.

Do not make this race lose the activation request or create two windows.

A practical pattern is:

1. create/open the named activation event early;
2. establish mutex ownership;
3. if secondary, signal event and exit;
4. if primary, register an event wait callback before/around MainWindow creation;
5. dispatch activation onto the WPF Dispatcher;
6. if the main window is not yet available, retain one pending activation and apply it after window creation.

Do not add a timer to solve this.

### 7.5 Secondary process must be a relay only

A second `ClawHUD.Settings.exe` process may exist very briefly only to relay the activation request.

It must **not** construct:

- `MainWindow`;
- `MainViewModel`;
- `RuntimeControlClient`;
- a second Control IPC session;
- a second Settings UI.

Expected secondary lifetime:

```text
start -> signal -> exit
```

### 7.6 Bring the existing window forward

On activation signal, primary WPF process must make the existing Settings window visible/foreground.

Handle at least:

```text
window already visible behind another app
window minimized by Win+D / shell behavior
window not currently active
```

Use the normal WPF/Win32 foreground APIs as necessary, e.g.:

```text
Show()
WindowState = Normal when needed
Activate()
SetForegroundWindow(hwnd) when required
```

Do **not** solve this by making Settings permanently Topmost.

Do **not** use an always-on-top toggle hack.

Manual validation from the actual tray interaction is required; merely preventing duplicate windows is not enough.

### 7.7 Direct launch behavior

Directly starting `ClawHUD.Settings.exe` is not the primary product entry point, but it should still obey the same singleton rule.

If ClawHUD runtime is not running, keep the PR5 behavior:

```text
WPF starts
-> initial Control IPC cannot establish a valid runtime session
-> Settings closes cleanly
```

Do **not** auto-start `ClawHUD.exe` from WPF.

---

## 8. Remove legacy native Settings lifetime plumbing

After WPF launch is wired, remove all obsolete in-process Settings ownership from `App`.

### 8.1 `App.cpp`

Remove:

```cpp
#include "SettingsWindow.h"
```

Remove:

```text
kSettingsDestroyed
PostSettingsDestroyed()
SettingsDestroyed()
```

Remove every:

```cpp
settings_.reset();
```

that exists solely for the native Settings object.

Remove the legacy message-loop branch:

```cpp
if (settings_ && settings_->Window() &&
    IsWindowVisible(settings_->Window()) &&
    IsDialogMessageW(settings_->Window(), &message))
{
    continue;
}
```

`ProcessMessages()` should return to the normal runtime/game-message processing path without a Settings-dialog special case.

Update stale comments that say the Standalone tray opens the legacy Settings window.

### 8.2 `App.h`

Remove:

```text
class SettingsWindow;
std::unique_ptr<SettingsWindow> settings_;
PostSettingsDestroyed declaration
SettingsDestroyed declaration
```

Keep `OpenSettings()` as the tray action entry point, but its semantics become external WPF launch.

Update the `WM_APP` comments. Do not renumber unrelated existing runtime/game messages merely because `WM_APP + 1` becomes unused.

### 8.3 Exit/destructor behavior

`App::~App()` / `App::Exit()` must no longer own or destroy WPF Settings.

Closing/exiting native ClawHUD must remain the existing runtime shutdown path.

Do not add cross-process termination such as:

```text
TerminateProcess(ClawHUD.Settings.exe)
```

PR5 already causes WPF Settings to close when an active IPC point proves the runtime is gone/shutting down.

Do not introduce polling merely to force immediate WPF closure while it is idle.

---

## 9. Delete legacy Win32 Settings source

Delete the obsolete production files:

```text
src/ClawHUD/SettingsWindow.cpp
src/ClawHUD/SettingsWindow.h
src/ClawHUD/SettingsWindow.Tweaks.cpp
src/ClawHUD/SettingsWindow.About.cpp
src/ClawHUD/SettingsWindow.Settings.cpp
src/ClawHUD/SettingsWindowInternal.cpp
src/ClawHUD/SettingsWindowInternal.h
src/ClawHUD/SettingsWindowGeometry.cpp
src/ClawHUD/SettingsWindowGeometry.h
```

Remove their entries from `CMakeLists.txt`.

Do not leave an unbuilt archival copy under `src/`.

Git history is the archive.

Historical work orders/docs do **not** need mass editing solely because they describe the old implementation.

Only update an active/canonical document if it incorrectly claims the legacy frontend is still production after this PR.

---

## 10. Remove legacy Settings-only native tests

The current native test graph still contains:

```text
ClawHUD.SettingsWindowGeometryTests
```

using:

```text
tests/SettingsWindowGeometryTests.cpp
src/ClawHUD/SettingsWindowGeometry.cpp
```

Delete the obsolete test source:

```text
tests/SettingsWindowGeometryTests.cpp
```

and remove its CMake test target from:

```text
cmake/ClawHUDTests.cmake
```

Do not weaken or remove tests for runtime settings semantics merely because the old UI is gone.

Keep:

- `HudSettingsStore` tests;
- Control protocol tests;
- Control dispatch/pipe tests;
- Start-with-Windows/runtime lifecycle tests;
- Intel VRR tweak tests;
- all HUD/VRR presentation regression coverage.

The rule is:

> delete tests of the deleted Win32 frontend implementation; preserve tests of product/runtime behavior.

---

## 11. CMake/native launcher integration

Add only the small WPF launcher helper required by section 6.

`CMakeLists.txt` should:

- remove all `SettingsWindow*` source entries;
- add `SettingsFrontendLauncher.cpp` if a helper is introduced;
- retain the existing native `ClawHUD.exe` WIN32 target;
- retain existing runtime libraries and HUD dependencies.

Do not make CMake invoke `dotnet publish`.

The native CMake build and the WPF .NET publish remain separate build-system concerns and are combined only in CI/release staging.

### 11.1 Native launcher test seam

Prefer a small pure path-resolution seam that can be unit-tested without starting WPF.

Required cases:

```text
C:\Apps\ClawHUD\ClawHUD.exe
    -> C:\Apps\ClawHUD\ClawHUD.Settings.exe

C:\Path With Spaces\ClawHUD.exe
    -> C:\Path With Spaces\ClawHUD.Settings.exe

relative/current-working-directory changes
    -> do not affect resolved sibling path
```

A new native test target such as:

```text
ClawHUD.SettingsFrontendLauncherTests
```

is acceptable if the helper can be tested cleanly.

Do not create heavy Win32 process mocks solely to unit-test `ShellExecuteExW`.

Actual launch/focus behavior is covered by manual smoke testing.

---

## 12. WPF single-instance implementation structure

Keep the singleton code out of `MainWindow` and `MainViewModel`.

A small frontend infrastructure type is preferred, for example:

```text
src/ClawHUD.Settings/Services/SettingsInstanceCoordinator.cs
```

Possible responsibility surface:

```text
TryAcquirePrimary()
SignalPrimary()
ActivationRequested event/callback
Dispose()
```

Exact shape is implementation choice.

The Settings ViewModel should remain concerned with settings projection/mutations, not process singleton management.

### 12.1 Cleanup

Dispose/unregister:

- named event handle;
- mutex handle/ownership;
- registered wait callback;
- any activation callback resources.

Do not leave background worker threads keeping the process alive after MainWindow closes.

### 12.2 Testability

Allow tests to use unique object names/suffixes so CI runs cannot collide with a developer's real `ClawHUD.Settings` instance.

Do not hard-code production named-object names inside tests.

---

## 13. WPF framework-dependent publish

PR6 turns WPF from "build-only" into a staged product component.

Publish using framework-dependent semantics, e.g.:

```powershell
dotnet publish src/ClawHUD.Settings/ClawHUD.Settings.csproj `
  --configuration Release `
  --self-contained false `
  --output .\wpf-publish
```

A `win-x64` RID may be supplied if required to make the apphost architecture explicit, but the output must remain **framework-dependent**.

Do not switch to self-contained merely to make packaging easier.

### 13.1 Required publish payload

At minimum release staging must receive:

```text
ClawHUD.Settings.exe
ClawHUD.Settings.dll
ClawHUD.Settings.deps.json
ClawHUD.Settings.runtimeconfig.json
```

plus any additional files that `dotnet publish` proves are genuinely required by this WPF project.

Do not manually guess/copy arbitrary `bin/Release` contents if `dotnet publish` provides the correct deployment closure.

### 13.2 Do not stage private .NET runtime payload

The staged app must not contain a private .NET Desktop Runtime.

At minimum CI/release assertions should fail if representative self-contained/runtime files unexpectedly appear, such as:

```text
coreclr.dll
clrjit.dll
hostfxr.dll
```

Do not stage:

- a `shared\Microsoft.WindowsDesktop.App` runtime tree;
- `dotnet.exe`;
- test assemblies;
- WPF testhost files;
- `bin/` or `obj/` directories.

PDB policy may follow the repository's current release defaults; do not make symbols a required runtime file.

---

## 14. VeloPack .NET 10 Desktop Runtime prerequisite

The WPF frontend is framework-dependent, so the installed product must declare the matching Desktop Runtime prerequisite.

VeloPack's documented framework syntax supports multiple comma-separated requirements and .NET Desktop Runtime identifiers in the form:

```text
net{major.minor}-{arch}-desktop
```

For this project use:

```text
net10.0-x64-desktop
```

Keep the current native Visual C++ prerequisite as well.

Change the release pack argument from:

```text
--framework vcredist145-x64
```

to:

```text
--framework net10.0-x64-desktop,vcredist145-x64
```

Do not remove `vcredist145-x64`.

Do not bundle a private .NET runtime as a substitute.

### 14.1 Update behavior matters

This PR can be the first release that adds the .NET prerequisite to an existing native-only installation.

The generated release must therefore support:

```text
existing ClawHUD install without .NET 10 Desktop Runtime
    -> new release adds net10.0-x64-desktop prerequisite
    -> prerequisite is handled before/applying the update
    -> new version then contains functional WPF Settings
```

Do not assume only clean installs exist.

### 14.2 Portable artifact policy

The WPF payload remains framework-dependent in the Velopack portable output too.

Do **not** silently make only the portable artifact self-contained.

Do **not** add a custom .NET downloader to native ClawHUD in this PR.

Acceptance must explicitly verify the generated portable artifact contains the WPF publish files and does **not** contain a private .NET runtime.

The installer/update bootstrap path is responsible for installing prerequisites. A portable extraction inherently relies on the required machine runtimes being present; keep that tradeoff consistent with the selected framework-dependent packaging policy.

---

## 15. Release workflow changes

Update `.github/workflows/Build-Release.yml`.

### 15.1 .NET SDK setup

The workflow currently explicitly sets up .NET 8 for the Velopack CLI.

PR6 needs .NET 10 to publish `net10.0-windows` WPF.

Use .NET 10 in the release job.

Velopack `vpk` 1.2.0 itself supports a `net10.0` tool target, so the existing pinned CLI version does not require preserving a separate .NET 8 setup solely for the tool.

Keep the Velopack CLI pin unless a separate explicit upgrade is required:

```text
vpk 1.2.0
```

Do not mix a Velopack version-upgrade project into this cutover PR without evidence that 1.2.0 blocks the required packaging behavior.

### 15.2 Publish WPF before stage

Add a release step before runtime staging:

```text
dotnet publish ClawHUD.Settings
    Release
    framework-dependent
    deterministic output directory
```

Fail if publish fails.

### 15.3 Stage WPF publish output

Copy the WPF publish closure into the same stage root as native runtime:

```text
stage\ClawHUD.exe
stage\ClawHUD.Settings.exe
stage\ClawHUD.Settings.dll
stage\ClawHUD.Settings.deps.json
stage\ClawHUD.Settings.runtimeconfig.json
...
```

This sibling layout is part of the native launch contract.

Do not place WPF under a version-independent external folder that the native sibling resolver cannot reach.

### 15.4 Required-file assertions

Extend the existing `$required` release-stage list to include the required Settings publish files.

A release must fail before `vpk pack` if any are missing.

Also assert that representative private runtime files are absent.

### 15.5 Keep native app as package main executable

Keep:

```text
--mainExe ClawHUD.exe
```

The native runtime remains:

- the application entry point;
- updater integration owner;
- tray owner;
- Start Menu shortcut target;
- Start-with-Windows target.

Do **not** change the main package executable to `ClawHUD.Settings.exe`.

Do not create a separate Start Menu shortcut for Settings in PR6.

### 15.6 Pack framework requirements

Use:

```text
--framework net10.0-x64-desktop,vcredist145-x64
```

Keep the existing stable channel/delta/feed behavior unchanged.

### 15.7 Release output validation

After `vpk pack`, continue validating the update feed/delta base as today.

In addition, inspect the generated full package/portable content if practical in the workflow or a local verification step so the WPF payload cannot accidentally disappear from release packaging while CI build still passes.

Do not weaken existing release-feed checks.

---

## 16. Build-Test workflow changes

`Build-Test.yml` already:

- sets up .NET 10;
- builds WPF Settings Release;
- runs WPF Settings tests;
- configures/builds native Release;
- runs native CTest.

Keep all of that.

Add a lightweight WPF publish validation so the artifact shape used by production is exercised on every PR.

Recommended flow:

```text
dotnet publish
    -c Release
    --self-contained false
    -o <temp publish dir>

assert:
    ClawHUD.Settings.exe exists
    ClawHUD.Settings.dll exists
    ClawHUD.Settings.deps.json exists
    ClawHUD.Settings.runtimeconfig.json exists

assert absent:
    coreclr.dll
    clrjit.dll
    hostfxr.dll
```

Do not upload the publish directory as a long-lived CI artifact unless there is a separate need.

The purpose is deployment-shape regression coverage, not artifact storage.

---

## 17. Tests — WPF single-instance coordinator

Add focused tests for the process coordination logic.

Avoid fragile desktop UI automation in ordinary CI.

Use unique test names/object suffixes.

Required cases:

### 17.1 First instance acquires primary ownership

```text
no owner
-> coordinator becomes primary
-> MainWindow is allowed to be constructed
```

### 17.2 Second instance is relay-only

```text
primary already owns singleton
-> second coordinator is not primary
-> activation signal reaches primary
-> secondary path does not construct MainWindow/ViewModel/client
```

The "does not construct UI/client" part may be tested through startup policy/seams rather than spawning a real second WPF window.

### 17.3 Ownership is released on exit

```text
primary coordinator disposed / process closing
-> later coordinator can acquire primary ownership
```

### 17.4 Session-specific naming

The production object names must include current session identity.

A pure name-builder test is sufficient; do not try to create another Windows logon session in CI.

### 17.5 Activation callback is dispatcher-safe

The named event callback may arrive on a worker/thread-pool thread.

The actual WPF window activation must be dispatched onto the WPF UI Dispatcher.

Test the separation/policy where practical; no cross-desktop E2E automation is required.

---

## 18. Tests — native launch/path policy

Add small native coverage for the sibling-path contract if a helper is introduced.

Required cases:

```text
normal path
path containing spaces
sibling filename exactly ClawHUD.Settings.exe
working directory does not affect resolution
```

Do not test the deleted `SettingsWindowGeometry` behavior.

Do not add generic process-launch mocking infrastructure.

---

## 19. Regression tests that must remain green

Run the complete existing WPF Settings test suite.

Run native CTest Release.

Specifically preserve existing coverage for:

- Control protocol v1;
- Control IPC security/lifetime;
- authoritative Settings mutations;
- Start-with-Windows rollback semantics;
- opacity Preview/Commit;
- Intel VRR preference semantics;
- runtime-loss handling;
- HUD click-through;
- no activation;
- topmost;
- transparent hit testing;
- independent flip;
- premultiplied alpha;
- production presentation contract.

The migration is not allowed to weaken HUD/VRR tests to make the cutover pass.

---

## 20. Resource cleanup

Search the native resources after deleting `SettingsWindow*`.

Remove only resource identifiers/assets that are proven to be used exclusively by the deleted Win32 Settings frontend.

Do **not** remove or change:

- the ClawHUD application icon;
- tray icon resources;
- unrelated message-window resources;
- fonts/licenses;
- PresentMon runtime payload;
- uninstall/update resources.

If the old Settings window was entirely programmatic and no dedicated resource remains, no resource change is required.

---

## 21. Repository cleanup search

Before completion, search current production source/build files for:

```text
SettingsWindow
SettingsWindowGeometry
SettingsWindowInternal
PostSettingsDestroyed
SettingsDestroyed
kSettingsDestroyed
settings_
```

Expected result:

- no production code/build references to the deleted Win32 frontend;
- historical docs/work orders may still contain historical references;
- WPF `MainWindow` is obviously not part of this search/removal rule.

Also search for comments such as:

```text
legacy Settings
legacy Win32 Settings
```

and update active source comments where they are now false.

---

## 22. Manual production smoke — mandatory before calling migration complete

The WPF feature implementation is already heavily unit-tested, but PR6 changes real process/package behavior and needs actual Windows/Claw validation.

### 22.1 Normal startup

On the target Claw:

1. Launch `ClawHUD.exe` normally.
2. Confirm native runtime/HUD/tray start.
3. Confirm `ClawHUD.Settings.exe` is **not** running.
4. Confirm no old Win32 Settings window appears.

### 22.2 First Settings open

1. Tray -> Settings.
2. Confirm one WPF Settings window opens.
3. Confirm process list has one long-lived `ClawHUD.Settings.exe`.
4. Confirm title uses runtime version.
5. Confirm all five cards show authoritative runtime state.

### 22.3 Repeated Tray -> Settings

With Settings already open:

1. Put another app in front.
2. Tray -> Settings again.
3. Confirm existing Settings window comes forward.
4. Confirm no second Settings window exists.
5. A short-lived relay process is acceptable; a second persistent Settings process/ViewModel is not.

Repeat rapidly several times.

There must be no duplicate visible windows or orphan Settings processes.

### 22.4 Close / reopen lifetime

1. Close Settings with X.
2. Confirm `ClawHUD.Settings.exe` exits and WPF/CLR memory disappears from the process list.
3. Confirm `ClawHUD.exe` and HUD continue running.
4. Tray -> Settings again.
5. Confirm a fresh Settings process opens normally.

### 22.5 Direct Settings launch

1. While Settings is open, directly run `ClawHUD.Settings.exe` again.
2. Confirm existing window is activated, not duplicated.
3. Stop ClawHUD runtime and directly launch Settings.
4. Confirm PR5 initial-runtime failure behavior closes Settings cleanly rather than launching another runtime.

### 22.6 Existing settings behavior

Recheck:

- Enable HUD;
- In-game only / Always;
- size;
- font;
- alignment;
- background width;
- opacity Preview/Commit;
- Intel VRR Range Fix preference;
- Start with Windows.

For Start with Windows, confirm the resulting startup shortcut still targets the native ClawHUD application path/stub, **not** `ClawHUD.Settings.exe`.

### 22.7 Activation refresh

1. Leave Settings open.
2. Change relevant runtime state externally, including F8 where applicable.
3. Return to Settings.
4. Confirm the activation snapshot refresh updates the UI.
5. Confirm no idle polling was introduced.

### 22.8 Tray Exit

1. Leave WPF Settings open.
2. Tray -> Exit.
3. Confirm native ClawHUD follows its existing clean shutdown path.
4. Confirm no WPF action terminates/restarts native ClawHUD.
5. Confirm Settings does not become a permanent unusable orphan; on its next active IPC point PR5 runtime-loss behavior closes it.

### 22.9 Target layout/touch

At:

```text
1920 x 1200
150% scale
```

confirm:

- no vertical scrolling;
- no horizontal scrolling;
- all five cards fit;
- opacity slider remains touch-usable;
- fixed window remains inside work area;
- no regression in Fluent light/dark behavior.

### 22.10 Installed package with no .NET 10 Desktop Runtime

Use a clean Windows test environment without .NET 10 Desktop Runtime x64 if available.

1. Run generated ClawHUD Setup.
2. Confirm Velopack handles the `net10.0-x64-desktop` prerequisite.
3. Confirm native ClawHUD starts normally after install.
4. Tray -> Settings.
5. Confirm WPF opens without a missing-framework failure.

### 22.11 Update from previous native-only release

Test update from a pre-PR6 release where WPF is not yet in production and .NET 10 Desktop Runtime may be absent.

Confirm:

- update prerequisite handling occurs correctly;
- WPF files arrive in the new `current` package;
- native app restarts normally;
- Tray -> Settings opens WPF;
- update/delta feed remains valid.

### 22.12 Portable artifact

Inspect/extract the generated portable release.

Confirm:

- native `ClawHUD.exe` is present;
- all required `ClawHUD.Settings.*` publish files are present in the version/current payload;
- no private .NET runtime was accidentally bundled;
- on a machine with .NET 10 Desktop Runtime present, Tray -> Settings works from portable layout.

Do not add a special self-contained portable-only frontend.

---

## 23. Suggested implementation sequence

A clean implementation order is:

### Step A — WPF singleton infrastructure

1. add `SettingsInstanceCoordinator` or equivalent;
2. remove `StartupUri`;
3. make `App.OnStartup()` primary/secondary aware;
4. add activation relay;
5. add unit tests.

Do this first so the external-launch behavior is deterministic before native cutover.

### Step B — native launcher

1. add sibling path/launch helper;
2. repurpose `App::OpenSettings()`;
3. validate Tray -> WPF manually;
4. add pure path tests.

### Step C — remove legacy frontend

1. remove `settings_` lifetime;
2. remove Settings-destroy message path;
3. remove `IsDialogMessageW` branch;
4. delete all `SettingsWindow*` files;
5. remove CMake source references;
6. remove `SettingsWindowGeometryTests`.

Do not keep the old frontend after external WPF launch works.

### Step D — packaging

1. add WPF `dotnet publish` to Build-Test/Build-Release;
2. stage WPF next to native executable;
3. assert required publish files;
4. assert no private runtime files;
5. add `net10.0-x64-desktop` to Velopack framework prerequisites;
6. keep native main exe and existing update/feed behavior.

### Step E — final regression

1. WPF tests;
2. WPF framework-dependent publish check;
3. native Release build;
4. native CTest;
5. package generation where available;
6. target-device smoke from section 22.

---

## 24. Explicit non-goals

Do not add in PR6:

- new Settings features;
- a new About page;
- localization;
- tabs/sidebar/navigation;
- polling;
- runtime push events;
- `RequestShutdown` button/command in WPF;
- a WPF tray icon;
- a Settings taskbar background mode;
- a generic process supervisor;
- a second Control IPC implementation;
- self-contained WPF deployment;
- WebView2;
- WinUI 3;
- PresentMon changes;
- EC changes;
- telemetry changes;
- game detection changes;
- Intel VRR algorithm changes;
- HUD renderer/presentation changes;
- Velopack version upgrade unless current pinned `1.2.0` is proven insufficient.

Tray `Exit` already exists and remains the runtime shutdown UI.

---

## 25. Acceptance criteria

PR6 is complete only when all of the following are true:

### Production frontend

- Tray -> Settings launches WPF `ClawHUD.Settings.exe`.
- The old Win32 Settings frontend cannot be reached.
- Only one visible/long-lived Settings instance exists per session.
- Repeated Settings requests bring the existing WPF window forward.
- Closing WPF Settings releases the WPF process while native ClawHUD continues.
- Starting ClawHUD never automatically starts WPF Settings.

### Legacy removal

- all listed `SettingsWindow*` files are deleted;
- `settings_`, `kSettingsDestroyed`, destroy callbacks, and Settings dialog message-loop handling are gone;
- CMake has no legacy Settings source entries;
- `SettingsWindowGeometryTests` and its CMake target are removed;
- no false active-source comments describe Win32 Settings as production.

### Packaging

- WPF is published framework-dependent with .NET 10;
- required WPF publish files are staged beside `ClawHUD.exe`;
- `--mainExe ClawHUD.exe` remains unchanged;
- Velopack framework list is:

```text
net10.0-x64-desktop,vcredist145-x64
```

- no private .NET runtime is bundled;
- full/update/portable packages contain the WPF frontend;
- existing release feed/delta validation remains intact.

### Correctness/regression

- all WPF Settings tests pass;
- WPF framework-dependent publish validation passes;
- native Release build passes;
- full native CTest passes;
- HUD/VRR presentation contract tests remain unchanged and green;
- 1920x1200 @150% touch/no-scroll smoke passes;
- installed clean/update packaging smoke demonstrates functional WPF launch.

---

## 26. Final intended state

After this PR there should be no further Settings frontend migration layer.

The production architecture is simply:

```text
ClawHUD.exe
    native runtime / HUD / tray / updater
        |
        | Tray -> Settings launches sibling process
        v
ClawHUD.Settings.exe
    WPF .NET 10 frontend
        |
        | Control Protocol v1
        v
ClawHUD.exe runtime authority
```

The old Win32 Settings implementation is deleted, not deprecated.

Any subsequent Settings work should be ordinary product/UI maintenance against this architecture rather than another migration phase.
