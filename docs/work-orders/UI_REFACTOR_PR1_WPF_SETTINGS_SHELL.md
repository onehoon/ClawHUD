# UI Refactor PR1 — WPF Settings Shell and Visual Foundation

**Date:** 2026-09-03  
**Status:** Ready for implementation  
**Reviewed baseline:** `main` at `021e993aa09c67a9112dd4ccde3b85711a9e35f7`  
**Parent plan:** `docs/UI refact/CLAW_HUD_SETTINGS_WPF_UI_REFACTOR_PLAN_2026-09-03.md`

---

## 1. Objective

Create the new standalone `ClawHUD.Settings.exe` WPF frontend shell and its one-page visual foundation **without changing any production ClawHUD behavior yet**.

This PR is intentionally a frontend-only foundation PR.

At the end of PR1:

- the new WPF project exists and builds independently;
- it can be launched manually by a developer for visual review;
- it uses the built-in Microsoft WPF Fluent theme on .NET 10;
- it contains the agreed fixed-size, touch-friendly, five-card Settings layout;
- closing the Settings window terminates the Settings process;
- no Settings process is launched when `ClawHUD.exe` starts;
- the existing native Win32 Settings window remains the production Settings frontend;
- there is **no Control IPC client yet**;
- there are **no runtime mutations yet**;
- release packaging/cutover remains unchanged.

The purpose is to establish a small, reviewable UI/process foundation before adding protocol code in PR2.

---

## 2. Current code baseline relevant to this PR

The current repository is still a native CMake application at the root:

- `CMakeLists.txt` declares `project(ClawHUD LANGUAGES CXX)`;
- `ClawHUD.exe`, `ClawHUD.EcHelper.exe`, and `ClawHUD.Diag.exe` are native C++ targets;
- the production `ClawHUD` target still compiles the legacy `SettingsWindow*.cpp` files;
- `App` still owns `std::unique_ptr<SettingsWindow>` and `App::OpenSettings()` still opens the Win32 frontend in Standalone mode;
- there is currently no C#/.NET project or solution under `src/`;
- `Build-Test.yml` currently validates only the native CMake build + CTest suite;
- `Build-Release.yml` currently stages only the native runtime payload and packages it with VeloPack.

Do **not** disturb those facts in PR1 except for adding independent CI compilation of the new WPF project.

The runtime/frontend contract already exists, but it is not consumed in this PR. PR2 will add the WPF-side protocol client.

---

## 3. Hard scope boundary

### 3.1 In scope

PR1 may change only the following categories:

1. add the standalone C# WPF project under `src/ClawHUD.Settings/`;
2. add the WPF application/window shell;
3. add local WPF visual resources/styles required for the agreed simple UI;
4. add DPI-awareness metadata if needed for the fixed handheld layout target;
5. add narrow `.gitignore` entries for the new SDK project outputs;
6. extend PR CI so the WPF project is compiled on every PR/push.

### 3.2 Explicitly out of scope

Do **not** implement any of the following in PR1:

- Named Pipe connection code;
- `ClawHudControlProtocol` C# DTOs;
- Control IPC framing/codec;
- `GetRuntimeInfo`;
- `GetSettingsSnapshot`;
- any `Set*` operation;
- `PreviewHudOpacity` / `CommitHudOpacity` behavior;
- ViewModel/runtime synchronization;
- runtime-unavailable UX beyond a static/non-functional shell;
- tray launch integration;
- duplicate Settings process handling;
- bringing an existing Settings window to foreground;
- release staging of `ClawHUD.Settings.exe`;
- VeloPack `.NET Desktop Runtime` prerequisite wiring;
- changing `vpk pack --framework`;
- changing startup shortcuts;
- changing `App::OpenSettings()`;
- removing any Win32 Settings source;
- changing `SettingsWindow` behavior;
- changing `IRuntimeControl`;
- changing the Control IPC protocol;
- changing `App`, `HudController`, `HudSettingsStore`, telemetry, game detection, PresentMon, EC, suspend/resume, or diagnostics;
- localization infrastructure;
- About page;
- third-party WPF/UI packages;
- WinUI 3;
- WebView2/web frontend.

If implementation appears to require one of these changes, stop and keep it for the later PR assigned to that concern.

---

## 4. HUD / VRR safety contract — zero-touch requirement

PR1 must not modify any production HUD presentation or rendering file merely to support Settings UI work.

In particular, do not modify or work around:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- existing `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirements;
- Presentation API / DirectComposition production presentation path;
- premultiplied-alpha presentation behavior.

Expected diff for all HUD presentation/rendering implementation files in this PR: **zero**.

---

## 5. New project structure

Use a small standalone SDK-style project. Do not create a repository-wide `.sln` only for PR1; the current repository does not use one and the WPF project can be built directly by path.

Recommended structure:

```text
src/
  ClawHUD.Settings/
    ClawHUD.Settings.csproj
    App.xaml
    App.xaml.cs
    MainWindow.xaml
    MainWindow.xaml.cs
    app.manifest
    Styles/
      SettingsStyles.xaml
```

Keep the structure proportional to this application. Do not introduce folders such as `Infrastructure`, `Domain`, `Navigation`, `Pages`, `Converters`, or a general UI framework before they are actually needed.

PR2 may add `Services/` or protocol-specific files when there is real code to place there.

---

## 6. Project configuration

Create `src/ClawHUD.Settings/ClawHUD.Settings.csproj` as a normal framework-dependent WPF executable.

Required properties should be equivalent to:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>WinExe</OutputType>
    <TargetFramework>net10.0-windows</TargetFramework>
    <UseWPF>true</UseWPF>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <AssemblyName>ClawHUD.Settings</AssemblyName>
    <RootNamespace>ClawHUD.Settings</RootNamespace>
    <PlatformTarget>x64</PlatformTarget>
    <SelfContained>false</SelfContained>
    <ApplicationManifest>app.manifest</ApplicationManifest>
  </PropertyGroup>
</Project>
```

Exact ordering is not important. The semantic requirements are.

### 6.1 Framework-dependent only

Do not add:

- `PublishSingleFile`;
- `PublishTrimmed`;
- self-contained runtime assets;
- a private .NET runtime folder;
- a RuntimeIdentifier solely to force a self-contained/native-style layout.

PR1 should remain a normal framework-dependent WPF build.

The release installer's runtime prerequisite policy is deferred to PR6, where VeloPack packaging is intentionally changed.

### 6.2 No NuGet UI framework

Do not add WPF UI libraries or MVVM frameworks in this PR.

The desired appearance is achievable with:

- built-in WPF Fluent theme;
- standard WPF controls;
- a small local resource dictionary for card spacing and any narrowly needed option-button styling.

Adding a third-party Fluent library would defeat the lightweight-maintenance goal.

---

## 7. Fluent theme

Use the Microsoft-provided WPF Fluent theme built into modern WPF.

Preferred application-level configuration:

```xml
<Application
    x:Class="ClawHUD.Settings.App"
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    StartupUri="MainWindow.xaml"
    ShutdownMode="OnMainWindowClose"
    ThemeMode="System">
    ...
</Application>
```

`ThemeMode="System"` is preferred over manually importing the Fluent resource dictionary because it keeps the application aligned with the user's Windows light/dark mode.

Do not add a custom theme engine.

If the .NET 10 compiler emits the documented WPF experimental API warning for `ThemeMode`, do not suppress unrelated warnings globally. Keep any suppression, if actually required by the build policy, narrow and documented. A warning by itself is not a reason to abandon the built-in Fluent theme.

---

## 8. Process lifetime behavior

The WPF Settings process is short-lived.

### Required behavior

- `ClawHUD.Settings.exe` is not launched by `ClawHUD.exe` in PR1;
- the new WPF app is launched only manually during PR1 development/testing;
- there is one main window;
- close (`X`) closes the main window;
- `Alt+F4` closes the main window;
- closing the main window terminates the WPF application;
- no hidden window remains;
- no Settings tray icon exists;
- no background worker exists;
- no timer exists;
- no resident WPF process remains after closing.

Use the normal WPF application lifetime model rather than explicit `Environment.Exit()` or force-killing the process.

`ShutdownMode="OnMainWindowClose"` is preferred so the intended lifetime is explicit.

### Window chrome

The Settings window should expose only Close as a useful caption action.

Use normal Windows caption chrome and a fixed-size window. Prefer:

```xml
ResizeMode="NoResize"
```

Do not implement a custom title bar in PR1.

Do not create custom minimize/maximize interception code. The window simply does not offer those operating states.

---

## 9. DPI policy

Primary validation target:

- display resolution: `1920 x 1200`;
- Windows scaling: `150%`;
- effective WPF workspace: approximately `1280 x 800 DIP` before work-area deductions.

The layout must be expressed in WPF DIPs, never physical pixel calculations.

Add an application manifest declaring modern DPI awareness. Prefer Per-Monitor V2 for this Windows 11-only frontend, for example:

```xml
<asmv3:application>
  <asmv3:windowsSettings>
    <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
    <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
  </asmv3:windowsSettings>
</asmv3:application>
```

Use a valid complete application manifest around those settings.

Do not add runtime DPI APIs or Win32 DPI message handling unless the standard WPF/manifest path proves insufficient in a real test.

---

## 10. Window geometry

Start with the agreed compact target:

```text
Width  = approximately 700 DIP
Height = approximately 660 DIP
```

Recommended initial XAML:

```xml
Width="700"
Height="660"
MinWidth="700"
MaxWidth="700"
MinHeight="660"
MaxHeight="660"
ResizeMode="NoResize"
WindowStartupLocation="CenterScreen"
```

The exact values may move by a small amount during PR1 if required to satisfy the acceptance test on the actual 1920 x 1200 / 150% target.

The acceptance condition is more important than preserving the literal `700 x 660` values:

> all five current Settings cards fit naturally with touch-sized controls and no scrolling at 1920 x 1200 / 150%.

Do not solve layout pressure by adding a `ScrollViewer`.

Do not solve it by shrinking touch targets below the agreed usable size.

Adjust spacing/card heights first.

---

## 11. Main page information architecture

There is exactly one page.

Do not use:

- `TabControl`;
- sidebar;
- `NavigationView` imitation;
- hamburger menu;
- separate pages;
- About section/page.

The top-to-bottom order is fixed for this refactor:

1. HUD enable + display mode;
2. HUD size + font + alignment;
3. background width + background opacity;
4. Intel VRR Range Fix;
5. Start with Windows.

All HUD controls remain above the unrelated tweak/startup settings.

---

## 12. Card design

Use cards solely as visual grouping surfaces.

### Rules

- five cards total;
- no card title/header such as `HUD`, `Appearance`, `General`, or `Tweaks`;
- no nested cards;
- subtle rounded border/background only;
- approximately 16-20 DIP internal padding;
- approximately 10-12 DIP vertical spacing between cards;
- card styling must work in system light and dark modes;
- do not hard-code a light-only white card background;
- avoid gradients, heavy shadows, acrylic stacks, or decorative graphics.

Create one reusable card style in `Styles/SettingsStyles.xaml` rather than duplicating Border properties five times.

Prefer theme/system-derived brushes. Do not introduce a large custom color palette.

---

## 13. Touch sizing

This UI is for handheld use as well as mouse use.

Interactive-looking controls must reserve touch-sized geometry from the first PR even though they are not connected to runtime actions yet.

Target approximately:

- option/button minimum height: `40-44 DIP`;
- plus/minus buttons: approximately `44 x 40 DIP` or larger;
- generous horizontal hit area for two/three-choice options;
- enough spacing that adjacent controls are not easy to hit accidentally;
- state must be visually recognizable without hover.

Do not optimize the layout for tiny desktop mouse targets.

---

## 14. Static PR1 control composition

PR1 is not allowed to pretend that it owns runtime state.

Therefore the page may use representative static values solely for visual review, but it must not persist or apply them.

Recommended composition:

```text
Card 1
  Enable HUD                         [ representative binary control ]
  Display mode
  [ In-game only ] [ Always ]

Card 2
  HUD size                           [ - ] Default [ + ]
  Font
  [ Unispace ] [ Segoe UI Variable ]
  Alignment
  [ Left ] [ Center ] [ Right ]

Card 3
  Background width
  [ Full width ] [ Content width ]
  Background opacity
  [ slider ] 70%

Card 4
  Intel VRR Range Fix                [ representative binary control ]

Card 5
  Start with Windows                 [ representative binary control ]
```

### Non-functional shell behavior

Do not add fake save logic.

Do not write an INI file.

Do not maintain a fake settings model that will later need to be removed.

For controls that would otherwise look misleadingly functional, one acceptable PR1 technique is to render them normally but disable hit testing for the content area until PR2/PR3 wiring exists. The exact technique is up to the implementer, but the user must not be able to click a setting in PR1 and believe it changed ClawHUD.

Do not show a large `Not connected` banner; this is a development foundation that is not yet the production Settings path.

---

## 15. Selection-control styling

Avoid classic tiny radio-button circles as the primary touch surface.

For multi-choice settings, establish a compact button-like/segmented visual foundation using standard WPF primitives.

Constraints:

- keep the style local to this project;
- no custom control assembly;
- no generalized segmented-control framework;
- no complex animation;
- selected and unselected states must be clear in light and dark modes;
- target height remains approximately 40-44 DIP.

The actual binding/group behavior is added when runtime-backed state is introduced later. PR1 only needs a credible visual shell.

---

## 16. Background opacity row

The PR1 slider is visual only.

It must reflect the future product constraints in its static setup:

- minimum: `50`;
- maximum: `100`;
- representative value: `70`;
- future step: `5`.

Do not implement preview/commit handlers yet.

Those semantics belong to PR4.

The slider should still be large enough to manipulate comfortably by touch once enabled later.

---

## 17. Title behavior in PR1

Use:

```text
ClawHUD
```

as the window title in PR1.

Do **not** append a frontend assembly version such as `ClawHUD 1.0.0`.

The final design uses the runtime's `GetRuntimeInfo.applicationVersion`, but runtime metadata is intentionally not connected in PR1. Runtime-version title wiring belongs to PR5.

No About page is added.

---

## 18. Language policy

English only.

Do not add:

- `.resx` localization files solely for UI strings;
- localization services;
- language selectors;
- Korean resources.

Keep user-visible strings directly in this small first frontend until localization is explicitly requested as a separate product feature.

---

## 19. `.gitignore`

The repository currently ignores CMake build directories but does not yet have .NET SDK-project output rules.

Add narrow ignores for the new project, for example:

```gitignore
/src/ClawHUD.Settings/bin/
/src/ClawHUD.Settings/obj/
```

Do not broadly rewrite the repository ignore policy in this PR.

---

## 20. CI integration

The WPF project must not exist without automated compilation coverage.

Update `.github/workflows/Build-Test.yml` only as needed to build the new project in addition to the existing native job.

Recommended minimal sequence inside the existing Windows job:

```yaml
- name: Setup .NET 10
  uses: actions/setup-dotnet@v4
  with:
    dotnet-version: '10.0.x'

- name: Build WPF Settings Release x64
  shell: pwsh
  run: dotnet build src/ClawHUD.Settings/ClawHUD.Settings.csproj --configuration Release
```

Keep the existing CMake configure/build/CTest steps intact.

A separate CI job is not required unless it materially simplifies the workflow.

### Do not modify release packaging in PR1

`Build-Release.yml` must continue to stage/package the same production files it does today.

Specifically, PR1 must **not**:

- stage `ClawHUD.Settings.exe` into the release;
- call `dotnet publish` as part of release packaging;
- change the VeloPack `--framework` list;
- change `--mainExe`;
- change shortcuts;
- change delta/update feed behavior.

This allows PR1 to merge safely while production continues to use the Win32 Settings frontend.

---

## 21. Files expected to change

Expected new files:

```text
src/ClawHUD.Settings/ClawHUD.Settings.csproj
src/ClawHUD.Settings/App.xaml
src/ClawHUD.Settings/App.xaml.cs
src/ClawHUD.Settings/MainWindow.xaml
src/ClawHUD.Settings/MainWindow.xaml.cs
src/ClawHUD.Settings/app.manifest
src/ClawHUD.Settings/Styles/SettingsStyles.xaml
```

Expected existing-file changes:

```text
.gitignore
.github/workflows/Build-Test.yml
```

The exact file split may vary slightly, but adding runtime/native source changes is a scope warning.

### Files that should normally have zero diff

At minimum:

```text
CMakeLists.txt
.github/workflows/Build-Release.yml
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/RuntimeControl.h
src/shared/ClawHudControlProtocol.h
src/ClawHUD/RuntimeControlPipeServer.*
src/ClawHUD/RuntimeControlDispatchBridge.*
src/ClawHUD/HudController.*
src/ClawHUD/HudPresentation.*
src/ClawHUD/HudRenderer.*
src/ClawHUD/HudPresentationContract.*
src/ClawHUD/SettingsWindow*
```

If one of these requires a diff, re-check the PR boundary before proceeding.

---

## 22. Implementation order

Recommended implementation order:

### Step 1 — create the project

Add the SDK-style WPF project and confirm:

```powershell
dotnet build src/ClawHUD.Settings/ClawHUD.Settings.csproj -c Release
```

works before adding styling complexity.

### Step 2 — configure application theme/lifetime

Add:

- `ThemeMode="System"`;
- `ShutdownMode="OnMainWindowClose"`;
- `StartupUri="MainWindow.xaml"`.

Confirm closing the window removes the process.

### Step 3 — configure fixed window/DPI

Add:

- fixed-size geometry;
- `ResizeMode="NoResize"`;
- centered startup;
- Per-Monitor V2 manifest.

### Step 4 — establish shared styles

Add only the styles required for:

- cards;
- touch-sized option buttons;
- touch-sized +/- buttons if needed;
- compact consistent label spacing.

### Step 5 — compose all five cards

Build the complete static page in the agreed order.

### Step 6 — validate 150% handheld target

On 1920 x 1200 / 150%:

- no vertical scrollbar;
- no horizontal scrollbar;
- no clipped bottom card;
- no clipped caption;
- no overlapping controls;
- no tiny touch targets.

Adjust geometry/spacing rather than introducing scrolling.

### Step 7 — wire CI build

Add .NET 10 setup + WPF build to `Build-Test.yml` without disturbing the native test sequence.

---

## 23. Verification requirements

### 23.1 Required automated build verification

Run:

```powershell
dotnet restore src/ClawHUD.Settings/ClawHUD.Settings.csproj
dotnet build src/ClawHUD.Settings/ClawHUD.Settings.csproj -c Release --no-restore
```

Also verify the native project remains clean:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -T v145 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

No existing native test may be removed or weakened because the PR is UI-only.

### 23.2 Framework-dependent check

Optionally perform a local publish check:

```powershell
dotnet publish src/ClawHUD.Settings/ClawHUD.Settings.csproj `
  -c Release `
  --self-contained false `
  -o build-settings-publish
```

Confirm the output is a framework-dependent application and no private .NET Desktop Runtime payload is introduced into the repository or package.

This is a verification command only in PR1; do not add WPF publishing to `Build-Release.yml` yet.

### 23.3 Manual lifetime smoke test

Launch the WPF project manually.

Verify:

1. one `ClawHUD.Settings.exe` process appears;
2. no `ClawHUD.exe` is automatically started by the WPF app;
3. the window has no useful minimize/maximize state;
4. closing with `X` terminates `ClawHUD.Settings.exe`;
5. `Alt+F4` terminates `ClawHUD.Settings.exe`;
6. no hidden Settings process remains after close.

### 23.4 Manual visual test

At `1920 x 1200`, Windows scaling `150%`:

- all five cards are visible at once;
- there is no `ScrollViewer`/scrollbar;
- cards are visually separated but do not have card titles;
- HUD settings are at the top;
- Intel VRR Range Fix is below all HUD cards;
- Start with Windows is last;
- controls are large enough to target by touch;
- no layout depends on hover to communicate selected state;
- system Light mode looks coherent;
- system Dark mode looks coherent;
- no large unused empty region forces unnecessary window height.

### 23.5 Production regression smoke test

Run the normal native `ClawHUD.exe` after building PR1 and verify:

- normal ClawHUD startup does not launch `ClawHUD.Settings.exe`;
- tray still exists in Standalone mode;
- tray Settings still opens the existing native Win32 Settings window;
- existing Win32 Settings controls continue to operate;
- tray Exit continues to perform the existing real runtime shutdown;
- HUD runtime behavior is unchanged.

---

## 24. Acceptance criteria

PR1 is complete only when all of the following are true:

1. `src/ClawHUD.Settings/ClawHUD.Settings.csproj` targets .NET 10 WPF and builds successfully on Windows x64.
2. The project is framework-dependent (`SelfContained=false`) and carries no private .NET runtime.
3. The built-in Microsoft WPF Fluent theme follows system Light/Dark mode.
4. The window uses a normal title bar with Close available and no minimize/maximize workflow.
5. Closing the main window terminates the Settings process and returns its CLR/WPF memory.
6. `ClawHUD.Settings.exe` is not launched by normal ClawHUD startup.
7. The page contains exactly the agreed five card groups in the agreed order.
8. Cards have no section titles.
9. There is no About page or About card.
10. User-visible strings are English only; no localization infrastructure is added.
11. The layout fits 1920 x 1200 / 150% with no horizontal or vertical scrolling.
12. Touch-target geometry is approximately 40-44 DIP minimum for the primary interactive controls.
13. The title is `ClawHUD` only in PR1; no fake/frontend version is displayed.
14. No Control IPC or fake persistence/settings ownership is implemented.
15. Existing Win32 Settings remains the production frontend.
16. `Build-Test.yml` compiles the WPF project and still runs the complete existing native build/CTest path.
17. `Build-Release.yml` remains unchanged.
18. Native ClawHUD runtime/HUD/presentation behavior remains unchanged.
19. HUD/VRR presentation-contract files have zero functional diff.

---

## 25. PR size / review guidance

Target this PR to remain comfortably reviewable, roughly **<= 500 LOC of meaningful implementation change** where practical.

Generated SDK outputs do not belong in Git.

If the UI requires hundreds of lines of custom control templates merely to imitate WinUI, simplify the design. The product requirement is "clean, modern, and touch-friendly," not a pixel-perfect Windows Settings clone.

Prioritize:

1. correct process lifetime;
2. correct fixed handheld geometry;
3. Fluent system theme;
4. clean card hierarchy;
5. touch usability;
6. minimal implementation surface.

---

## 26. Deferred work after PR1

PR1 deliberately leaves the page disconnected.

Next stages remain:

```text
PR2  Control IPC client + read-only runtime snapshot
PR3  HUD setting mutations
PR4  Background opacity Preview/Commit interaction semantics
PR5  Intel VRR Fix + Start with Windows + runtime version title
PR6  production tray cutover + VeloPack packaging + legacy Win32 Settings removal
```

Do not pull later-stage work into PR1 merely because the static controls already exist.

---

## 27. Completion report expected from implementation

The implementation PR description/final report should include:

- files added/changed;
- final window DIP dimensions;
- confirmation that the page fits 1920 x 1200 / 150% without scrolling;
- confirmation of Light/Dark Fluent behavior;
- confirmation that Close/Alt+F4 leaves no Settings process;
- `dotnet build` result;
- native CMake build result;
- CTest result/count;
- explicit statement that `Build-Release.yml`, `App`, Win32 Settings, Control IPC, HUD renderer/presentation were not changed.
