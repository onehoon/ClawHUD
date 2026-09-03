# Hotfix — WPF Settings Startup Crash on First Open

**Date:** 2026-09-04  
**Status:** Ready for implementation  
**Priority:** P0 / production Settings unusable  
**Reviewed baseline:** `main` at `f387af8cd765aa7fbd950a88f7797a30ebff1f3f`  
**Regression introduced/exposed by:** WPF Settings production cutover (#228)

---

## 1. Objective

Fix the reproducible startup crash/hang of `ClawHUD.Settings.exe` immediately after the WPF Settings window is opened from the tray.

The native `ClawHUD.exe` runtime remains alive and healthy. The failure is isolated to the new WPF Settings process.

This hotfix must:

1. remove the startup-time opacity Slider event hazard;
2. guarantee that XAML initialization cannot be interpreted as a user opacity mutation;
3. add minimal WPF crash diagnostics so future frontend exceptions leave a usable managed stack trace;
4. add a regression test/smoke seam for the startup initialization boundary;
5. leave the native runtime, Control IPC protocol/server, HUD renderer/presentation, VRR path, packaging architecture, and singleton design unchanged.

Keep this PR small and focused.

---

## 2. Field evidence from `C:\GoogleDrive\ClawHUD\logs\0904`

The 2026-09-04 device log shows the same Settings failure repeatedly.

Observed sequence on each attempt:

```text
native ClawHUD logs "Launched Settings frontend"
    -> ClawHUD.Settings.exe creates and shows the WPF MainWindow
    -> Windows Error Reporting attaches shortly afterward
    -> several seconds later Windows creates a "ClawHUD (Not Responding)" ghost window
    -> Settings HWNDs/process are destroyed
```

Three consecutive attempts followed the same pattern.

Representative timing:

```text
Attempt 1
23:59:15.088  Settings frontend launched
23:59:16.207  WPF MainWindow shown
23:59:16.691  WER fault activity begins
~23:59:21     ghost / Not Responding window
~23:59:23     Settings process destroyed

Attempt 2
23:59:24.751  Settings frontend launched
23:59:25.751  WPF MainWindow shown
23:59:26.070  WER fault activity begins
~23:59:30     ghost window
~23:59:31     Settings process destroyed

Attempt 3
23:59:33.186  Settings frontend launched
23:59:34.157  WPF MainWindow shown
23:59:34.514  WER fault activity begins
~23:59:39     ghost window
~23:59:39     Settings process destroyed
```

The native runtime does **not** terminate during these failures.

Its Control pipe was already running and the HUD/telemetry runtime remained active.

Therefore do not treat this as:

- native ClawHUD shutdown;
- PresentMon failure;
- HUD presentation failure;
- Control server lifetime failure;
- singleton relay failure.

The visible `Not Responding` ghost is a secondary Windows/WER symptom, not the first failure event.

---

## 3. Code-level root-cause candidate

The current `MainWindow` constructor is:

```csharp
public MainWindow()
{
    InitializeComponent();
    _viewModel = new MainViewModel(_client);
    _viewModel.PropertyChanged += OnViewModelPropertyChanged;
    _viewModel.RuntimeLost += OnRuntimeLost;
    DataContext = _viewModel;
    Loaded += OnLoadedAsync;
    Activated += OnActivated;
}
```

Current XAML declares the opacity handler directly:

```xml
<Slider x:Name="OpacitySlider"
        Minimum="50"
        Maximum="100"
        ...
        ValueChanged="OnOpacityValueChanged"
        Thumb.DragStarted="OnOpacityDragStarted"
        Thumb.DragCompleted="OnOpacityDragCompleted" />
```

Current handler immediately dereferences `_viewModel`:

```csharp
private async void OnOpacityValueChanged(
    object sender,
    RoutedPropertyChangedEventArgs<double> e)
{
    if (_suppressOpacityValueChanged)
        return;

    ushort snapped = SnapOpacity(e.NewValue);
    if (_viewModel.IsOpacityInteractionActive)
        _viewModel.UpdateOpacityGesture(snapped);
    else
        await _viewModel.ChangeOpacityAsync(snapped);
}
```

This creates a real WPF initialization hazard:

```text
InitializeComponent()
    -> Slider is created
    -> Minimum changes from default 0 to 50
    -> WPF may coerce Value from 0 to 50
    -> ValueChanged may fire while InitializeComponent is still running
    -> _viewModel has not yet been assigned
    -> handler dereferences _viewModel
```

The field evidence and timing are strongly consistent with this path.

The implementation should still verify the exact exception during development if a debugger/managed exception output is available, but **do not delay the hotfix waiting for another device capture**. The initialization ordering is invalid regardless: XAML construction is not a user opacity interaction and must never invoke runtime mutation handling.

---

## 4. Required fix — defer opacity `ValueChanged` subscription

Do not paper over the issue with only:

```csharp
if (_viewModel is null) return;
```

The stronger invariant is:

> `OnOpacityValueChanged` is a user-interaction handler and must not be subscribed until the WPF window, ViewModel, and DataContext are fully initialized.

### 4.1 Remove XAML `ValueChanged` subscription

Change `MainWindow.xaml` from:

```xml
ValueChanged="OnOpacityValueChanged"
```

to no `ValueChanged` declaration.

Keep:

```xml
Thumb.DragStarted="OnOpacityDragStarted"
Thumb.DragCompleted="OnOpacityDragCompleted"
```

unless testing proves those routed handlers can also fire during construction. They normally require an actual drag and are not part of the startup coercion path.

### 4.2 Subscribe after initialization in code-behind

After `InitializeComponent`, ViewModel creation, event wiring, and `DataContext` assignment, attach the Slider handler explicitly.

Preferred shape:

```csharp
public MainWindow()
{
    InitializeComponent();

    _viewModel = new MainViewModel(_client);
    _viewModel.PropertyChanged += OnViewModelPropertyChanged;
    _viewModel.RuntimeLost += OnRuntimeLost;
    DataContext = _viewModel;

    // Only user-era value changes are observed. XAML construction/coercion is
    // intentionally outside the opacity mutation boundary.
    OpacitySlider.ValueChanged += OnOpacityValueChanged;

    Loaded += OnLoadedAsync;
    Activated += OnActivated;
}
```

A slightly different ordering is acceptable if the same invariant is clear and tested.

### 4.3 Do not send an initial opacity mutation

Initial Slider construction may leave the control at its minimum until the first authoritative snapshot arrives.

That is fine.

After `GetSettingsSnapshotAsync()` succeeds:

```text
_viewModel.ApplySnapshot(snapshot)
    -> PropertyChanged
    -> OnViewModelPropertyChanged
    -> guarded OpacitySlider.Value = authoritative value
```

The existing `_suppressOpacityValueChanged` guard must continue to make that programmatic update non-mutating.

Expected startup IPC remains exactly:

```text
GetRuntimeInfo
GetSettingsSnapshot
```

There must be **no** automatic:

```text
PreviewHudOpacity(50)
CommitHudOpacity(50)
```

or any other opacity request caused by window initialization.

---

## 5. Preserve existing opacity interaction semantics

Do not redesign PR4 behavior.

After the window is initialized, preserve:

### Mouse/touch drag

```text
DragStarted
    -> BeginOpacityInteraction

ValueChanged while drag active
    -> UpdateOpacityGesture
    -> coalesced PreviewHudOpacity

DragCompleted
    -> EndOpacityInteractionAsync
    -> final CommitHudOpacity
```

### Keyboard / track click

When there is no active drag:

```text
ValueChanged
    -> ChangeOpacityAsync(snapped)
    -> preview/final commit policy already implemented by OpacityInteractionCoordinator
```

### Authoritative snapshot update

```text
ViewModel snapshot changes
    -> programmatic Slider.Value assignment under _suppressOpacityValueChanged
    -> no user mutation
```

Do not change:

- 50..100 range;
- 5% steps;
- Preview/Commit protocol operations;
- coalescing behavior;
- one-at-a-time interaction policy;
- authoritative runtime snapshot semantics.

---

## 6. Add WPF frontend crash logging

The field log proved that native `clawhud.log` can see WER/window lifecycle effects but cannot show the managed exception or stack trace from `ClawHUD.Settings.exe`.

Add a very small frontend-only crash logger.

Suggested file:

```text
src/ClawHUD.Settings/Services/SettingsCrashLogger.cs
```

Use:

```text
%LOCALAPPDATA%\ClawHUD\logs\clawhud-settings.log
```

Do **not** write concurrently into native `clawhud.log`.

### 6.1 Required data

Each fatal entry should contain at least:

```text
timestamp
process id
source
exception.ToString()
```

`Exception.ToString()` is required because it contains exception type, message, inner exception details, and managed stack trace.

### 6.2 Hook WPF dispatcher failures

In `App`, register:

```csharp
DispatcherUnhandledException += ...
```

Log the exception.

Do **not** set:

```csharp
e.Handled = true;
```

for an unknown fatal exception.

The purpose is diagnostics, not swallowing corruption and pretending the Settings UI is healthy.

### 6.3 Hook process-level managed failures

Also register a best-effort:

```csharp
AppDomain.CurrentDomain.UnhandledException += ...
```

Log the exception object when it is an `Exception`.

`TaskScheduler.UnobservedTaskException` is optional; do not expand this hotfix into a generic diagnostics framework.

### 6.4 Logger must never become the crash source

All logger code must be best-effort/no-throw.

Conceptually:

```csharp
internal static void LogFatal(string source, Exception exception)
{
    try
    {
        Directory.CreateDirectory(...);
        File.AppendAllText(...);
    }
    catch
    {
        // Never replace the original failure with logging failure.
    }
}
```

A small size bound/rotation is welcome, but do not add a large logging subsystem in this emergency PR.

If adding a bound, keep it simple, e.g. recreate/rotate around 512 KiB–1 MiB.

---

## 7. Startup regression coverage

The existing test suite covers protocol, ViewModel, opacity coordinator, and singleton logic, but did not exercise the actual `MainWindow.InitializeComponent()` lifecycle that exposed this issue.

Add a focused regression test if it can be made stable on the existing `windows-latest` WPF test environment.

### 7.1 Preferred real constructor smoke

Run WPF construction on a dedicated STA thread.

Conceptual structure:

```csharp
[Fact]
public void MainWindow_Construction_DoesNotDispatchOpacityMutationOrThrow()
{
    Exception? failure = null;

    var thread = new Thread(() =>
    {
        try
        {
            // Initialize WPF application resources if required by the
            // StaticResource lookups in MainWindow.xaml.
            var app = new App();
            app.InitializeComponent();

            var window = new MainWindow();
            window.Close();
        }
        catch (Exception ex)
        {
            failure = ex;
        }
    });

    thread.SetApartmentState(ApartmentState.STA);
    thread.Start();
    Assert.True(thread.Join(TimeSpan.FromSeconds(10)));
    Assert.Null(failure);
}
```

Adapt for WPF `Application` singleton constraints and xUnit execution as needed.

Do not introduce flaky UI automation.

Do not call `Show()` unless necessary: the bug boundary is construction/`InitializeComponent`, and avoiding `Show()` keeps this test independent of real runtime IPC.

### 7.2 Assert no startup opacity request where practical

The strongest regression test also proves that MainWindow construction cannot call:

```text
PreviewHudOpacity
CommitHudOpacity
```

If direct injection into `MainWindow` is currently too invasive for a hotfix, do **not** redesign the whole window for DI solely for this test.

Instead, at minimum provide:

1. the real STA constructor smoke; and
2. an explicit code-level assertion/invariant that `ValueChanged` is attached only after construction.

A small internal helper/seam is acceptable if it makes this clean.

### 7.3 If direct WPF construction is unreliable in CI

If creating a real `Application` in xUnit proves fundamentally unstable because the test host shares one AppDomain across tests, use a narrowly scoped alternative rather than adding a new UI framework/package.

Acceptable fallback:

- extract the subscription boundary into a small internal method/state that can be unit-tested;
- keep manual executable startup smoke mandatory;
- document why full `MainWindow` construction was not used in CI.

Do **not** add Selenium/Appium/WinAppDriver/etc.

---

## 8. Manual reproduction/verification — mandatory

This is a device-reproduced production crash; unit tests alone are not sufficient.

On the MSI Claw that produced the 0904 log:

### 8.1 Cold Settings open

1. Ensure `ClawHUD.exe` is running.
2. Confirm no `ClawHUD.Settings.exe` process exists.
3. Tray -> Settings.
4. Confirm WPF Settings opens and remains responsive for at least 30 seconds.
5. Confirm no Windows ghost / `Not Responding` window appears.
6. Confirm no WER event is generated for the Settings process.

### 8.2 Repeat lifecycle

Repeat at least 5 times:

```text
open Settings
close Settings
confirm Settings process exits
open again
```

There must be no intermittent startup crash.

### 8.3 Repeated open while already running

With Settings open:

1. move another application to foreground;
2. Tray -> Settings again;
3. confirm the existing WPF window is brought forward;
4. confirm no duplicate long-lived Settings process/window.

Do not regress PR6 singleton/activation behavior while fixing startup.

### 8.4 Opacity initialization

Use a non-minimum saved opacity, preferably 70% or 85%.

1. restart ClawHUD;
2. open Settings;
3. confirm Slider initializes to the saved authoritative opacity;
4. confirm opening Settings does **not** change HUD opacity;
5. confirm settings persistence remains unchanged merely by opening the window.

### 8.5 Opacity user interaction

Verify:

- drag previews live;
- final release commits;
- keyboard/track click still commits correctly;
- closing/reopening shows the committed value.

### 8.6 Crash logger sanity

For normal successful startup, the presence of the log file is not important.

For a deliberately triggered/debug-only test exception if convenient:

- confirm `clawhud-settings.log` contains a timestamp and managed stack trace;
- remove any deliberate throw before commit.

Do not ship a deliberate crash trigger.

---

## 9. CI / verification

Required before merge:

```text
dotnet build src/ClawHUD.Settings/ClawHUD.Settings.csproj -c Release
dotnet test tests/ClawHUD.Settings.Tests/ClawHUD.Settings.Tests.csproj -c Release
```

Run the existing native Release build/CTest as normal because this repository's merge gate includes it, even though native production code should not change.

Expected native source diff for this hotfix: **zero**.

Packaging workflow changes are not required unless the implementation changes file inclusion. A new `.cs` file is automatically part of the SDK-style WPF project/publish output.

---

## 10. HUD / VRR safety contract — zero touch

This is a WPF frontend startup fix only.

Do **not** modify:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirement;
- Presentation API / DirectComposition production path;
- premultiplied alpha contract;
- background-only opacity rendering semantics.

Expected diff in native HUD/presentation code: **zero**.

Existing HUD/VRR tests must remain green.

---

## 11. Explicit non-goals

Do not use this hotfix to:

- redesign WPF Settings architecture;
- change the session singleton implementation;
- alter Tray -> Settings launch semantics;
- add polling;
- change Control Protocol v1;
- change native Control pipe behavior;
- add retries to hide frontend exceptions;
- make WPF self-contained;
- change VeloPack prerequisites;
- change UI layout/size/styles;
- add localization;
- change opacity range or step;
- change opacity Preview/Commit semantics;
- add a general application telemetry/crash-reporting service;
- change native runtime logging.

---

## 12. Expected files

Primary expected changes:

```text
src/ClawHUD.Settings/MainWindow.xaml
src/ClawHUD.Settings/MainWindow.xaml.cs
src/ClawHUD.Settings/App.xaml.cs
src/ClawHUD.Settings/Services/SettingsCrashLogger.cs

tests/ClawHUD.Settings.Tests/<startup regression test>.cs
```

No native C++ source should need modification.

No build/release workflow modification should be necessary.

---

## 13. Acceptance criteria

Merge only when all are true:

### Crash fix

- `ValueChanged="OnOpacityValueChanged"` is no longer wired during XAML construction;
- opacity `ValueChanged` subscription occurs only after MainWindow/ViewModel initialization;
- opening Settings cannot send an opacity mutation due to control initialization;
- repeated device startup no longer produces WER/ghost/Not Responding behavior.

### Existing behavior

- initial `GetRuntimeInfo` + `GetSettingsSnapshot` flow remains intact;
- authoritative snapshot initializes Slider correctly;
- opacity drag Preview + final Commit still work;
- keyboard/track-click opacity changes still work;
- singleton/bring-to-front still works;
- closing Settings still terminates only the Settings process;
- native ClawHUD runtime remains alive.

### Diagnostics

- unhandled WPF dispatcher exceptions are best-effort logged with full `Exception.ToString()`;
- process-level managed unhandled exceptions are also best-effort logged;
- logging failure can never throw over the original application path;
- exceptions are not silently marked handled.

### Regression

- WPF Release build passes;
- WPF tests pass;
- startup regression coverage is added or a documented stable seam is used if full WPF construction is not viable in xUnit;
- native Release build/CTest remains green;
- HUD/VRR presentation contract remains untouched.

---

## 14. Final intended startup boundary

After the hotfix:

```text
App.OnStartup
    -> MainWindow constructed
        -> InitializeComponent
            -> Slider XAML initializes/coerces values
            -> NO opacity user handler is attached yet
        -> MainViewModel created/wired
        -> DataContext assigned
        -> OpacitySlider.ValueChanged handler attached
        -> Loaded/Activated handlers attached
    -> window shown

MainWindow.Loaded
    -> GetRuntimeInfo
    -> GetSettingsSnapshot
    -> ApplySnapshot
    -> guarded programmatic Slider.Value update
    -> NO opacity mutation

actual user changes opacity
    -> ValueChanged handler
    -> existing Preview/Commit logic
```

That lifecycle boundary is the core fix. Keep it explicit in code so a future XAML refactor cannot accidentally reintroduce startup-time runtime mutations.
