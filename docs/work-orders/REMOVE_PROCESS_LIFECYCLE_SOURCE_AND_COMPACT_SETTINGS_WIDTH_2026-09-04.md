# Work Order — Remove Dead `ProcessLifecycleSource`, Compact Settings Width, and Correct HUD Opacity Label

**Date:** 2026-09-04  
**Status:** Ready for implementation  
**Baseline:** `main` after PR #230  
**Target:** one focused PR containing exactly the three changes below

---

## 1. Scope decision

Implement **exactly three product changes in one PR**:

1. **Remove `ProcessLifecycleSource` completely from ClawHUD.**
2. **Reduce the WPF Settings fixed width from 700 DIP to 600 DIP.**
3. **Rename the Settings label `Background opacity` to `HUD opacity`.**

Do not add unrelated cleanup, diagnostics redesign, game-detection changes, HUD rendering changes, telemetry changes, packaging changes, or Settings behavior changes.

The application is still pre-release, so remove the obsolete diagnostic source cleanly instead of preserving compatibility shims, disabled code, aliases, or fallback behavior.

Most of this PR is deletion. A large negative diff is expected and is not a reason to split the PR.

The opacity item is **label-only**. It corrects the user-facing name to match the already-shipped runtime behavior; it does not authorize any opacity implementation change.

---

# Part A — Remove `ProcessLifecycleSource`

## 2. Why it should be removed

`ProcessLifecycleSource` was created as a **diagnostic-only evidence source** before the final production game-detection architecture was selected.

It attempts to subscribe to WMI asynchronous process lifecycle events:

```text
Win32_ProcessStartTrace
Win32_ProcessStopTrace
```

On the real MSI Claw in normal non-elevated operation, the source repeatedly fails during startup with:

```text
[ProcessLifecycle] start.result=API_FAILED stage=StartSubscription hr=0x80041003
Process lifecycle diagnostic source failed to start; continuing
```

`0x80041003` is `WBEM_E_ACCESS_DENIED`.

The current production game-detection design already records that this source produced **no usable process start/stop event data** in the normal field run and explicitly states that production game detection must not depend on it.

The 2026-09-04 post-PR230 field log confirms the same behavior remains:

```text
normal startup
-> ProcessLifecycleSource Start()
-> WMI StartSubscription
-> WBEM_E_ACCESS_DENIED
-> warning
-> continue without the source
```

No current production authority depends on it.

Therefore do **not** merely downgrade the warning level or suppress `WBEM_E_ACCESS_DENIED`.

Remove the unused source.

---

## 3. Existing architectural boundary to preserve

`ProcessLifecycleSource` currently sits under:

```text
DebugObservationController
```

alongside the remaining debug observers:

```text
WindowsGameIdentitySource
WindowLifecycleSource
PresentActivitySource
```

These debug observers are separate from production game detection.

Production authority remains owned by the current production path, including the current `GameSessionController` and its production event-driven sources/policies.

Deleting `ProcessLifecycleSource` must **not** change any production game-detection behavior.

Do not feed any replacement process-start/stop mechanism into production.

Do not replace it with:

```text
process polling
EnumProcesses polling
Toolhelp polling
WMI WITHIN polling
repeated EnumWindows
new timers
ETW redesign
new elevated helper behavior
```

There is no replacement requirement in this PR.

If process lifecycle diagnostics are needed again in the future, they should be reconsidered separately, preferably in the standalone `ClawHUD.Diag` context rather than restoring this dead source to the production app.

---

## 4. Files to delete

Delete completely:

```text
src/ClawHUD/GameDetection/ProcessLifecycleSource.h
src/ClawHUD/GameDetection/ProcessLifecycleSource.cpp
tests/ProcessLifecycleSourceTests.cpp
```

Do not leave stub classes, disabled implementations, commented code, or empty compatibility headers.

---

## 5. `DebugObservationController` cleanup

Update:

```text
src/ClawHUD/GameDetection/DebugObservationController.h
src/ClawHUD/GameDetection/DebugObservationController.cpp
```

### Header

Remove:

```cpp
#include "ProcessLifecycleSource.h"
```

Remove member:

```cpp
ProcessLifecycleSource processLifecycleSource_;
```

Update comments that currently describe the controller as owning **four** debug observation sources.

The controller should now accurately describe the remaining three:

```text
WindowsGameIdentitySource
WindowLifecycleSource
PresentActivitySource
```

Do not otherwise redesign `DebugObservationController`.

### `Start()`

Remove the call and warning path:

```cpp
if (!processLifecycleSource_.Start())
    RuntimeLogger::Log(RuntimeLogLevel::Warn,
        L"Process lifecycle diagnostic source failed to start; continuing");
```

Keep the existing behavior of the remaining sources unchanged.

Conceptually the method becomes:

```cpp
void DebugObservationController::Start()
{
    if (!windowLifecycleSource_.Start())
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"Window lifecycle diagnostic source failed to start; continuing");

    presentActivitySource_.Start(provider_);
}
```

Do not change the startup ordering of the remaining debug observers unless required by compilation.

### `Stop()`

Remove:

```cpp
processLifecycleSource_.Stop();
```

Keep the remaining effective shutdown order:

```cpp
windowLifecycleSource_.Stop();
presentActivitySource_.Stop();
```

`WindowsGameIdentitySource` continues to use its existing destruction/worker-lifetime behavior.

---

## 6. Native build cleanup

Remove the active implementation entry:

```text
src/ClawHUD/GameDetection/ProcessLifecycleSource.cpp
```

from:

```text
CMakeLists.txt
```

Do not make unrelated source-list reorderings.

Remove the complete dedicated test target for:

```text
ClawHUD.ProcessLifecycleSourceTests
```

from:

```text
cmake/ClawHUDTests.cmake
```

This includes the target declaration, source list, compile options/definitions, include directories, link libraries, properties, and `add_test(...)` entry.

Do not replace these tests with tests for a source that no longer exists.

The overall CTest test count is expected to decrease by exactly the removed test target unless another existing target legitimately changes independently.

---

## 7. WMI libraries — important non-goal

Do **not** remove `wbemuuid` globally just because `ProcessLifecycleSource` is gone.

The native code still has legitimate WMI users, including current hardware/tweak detection paths such as:

```text
SupportedHardware.cpp
Tweaks/IntelVrr/AffectedPanelDetector.cpp
```

Therefore the main executable's existing WMI/COM link dependencies must remain if still required by those sources.

Also do not alter EC Helper WMI dependencies.

This PR removes **one dead debug source**, not WMI usage from the project.

---

## 8. Documentation update

Update:

```text
docs/GAME_DETECTION_PRODUCTION_DESIGN.md
```

Its existing `ProcessLifecycleSource` diagnostic section records the failed field result and already says production must not depend on it.

Keep that historical evidence, but update the section to state that the source has now been **retired/removed from the production application** because:

```text
- it was diagnostic-only;
- normal non-elevated field operation returned WBEM_E_ACCESS_DENIED;
- it provided no production authority;
- the final production game detector does not depend on it;
- future process-lifecycle diagnostics, if needed, should be designed separately.
```

Do not rewrite the entire production game-detection design.

Do not edit old implementation work orders solely to make their historical description match today's code.

---

## 9. Required source-tree result

After this PR, searching the active source/test/build configuration for:

```text
ProcessLifecycleSource
Win32_ProcessStartTrace
Win32_ProcessStopTrace
```

should find **no active implementation/build/test references**.

Historical documentation may still mention the removed diagnostic source as historical evidence.

There must be no startup log lines like:

```text
[ProcessLifecycle] ...
Process lifecycle diagnostic source failed to start; continuing
```

because the source no longer exists.

---

# Part B — Reduce WPF Settings width to 600 DIP

## 10. Current state after PR #230

PR #230 compacted the Settings frontend vertically and reduced oversized touch-first controls.

Current fixed geometry is:

```text
700 x 600 DIP
```

At the primary target configuration:

```text
1920 x 1200 @ 150%
```

this becomes approximately:

```text
1050 x 900 physical pixels
```

The post-PR230 MSI Claw field run shows the Settings window is now vertically appropriate but still wider than necessary for its contents.

The current layout has enough horizontal room to reduce width without redesigning controls.

---

## 11. Required geometry change

Change only the fixed width:

```text
700 -> 600 DIP
```

Keep height:

```text
600 DIP
```

Final window geometry:

```text
600 x 600 DIP
```

Update:

```text
src/ClawHUD.Settings/MainWindow.xaml
```

from:

```xml
Width="700" Height="600"
MinWidth="700" MaxWidth="700"
MinHeight="600" MaxHeight="600"
```

to:

```xml
Width="600" Height="600"
MinWidth="600" MaxWidth="600"
MinHeight="600" MaxHeight="600"
```

Keep unchanged:

```text
ResizeMode=NoResize
WindowStartupLocation=CenterScreen
no ScrollViewer
Grid margin=12
existing card padding/density
existing switch dimensions
existing segment dimensions
existing stepper dimensions
existing slider dimensions
```

This is **width-only polish**.

Do not compact the controls again in this PR.

---

## 12. Why 600 DIP is acceptable

With the current outer margin and card padding, a 600 DIP window still leaves substantial usable content width.

The widest meaningful rows are currently:

```text
2-column Display mode
2-column Font
3-column Alignment
2-column Background width
HUD size label + stepper
HUD opacity slider + percentage text
```

At 600 DIP, the segmented controls retain ample width for labels such as:

```text
In-game only
Segoe UI Variable
Content width
```

Do not reduce to 560 DIP or another smaller value in this PR.

The agreed target is exactly:

```text
600 x 600 DIP
```

If an actual build shows clipping at 600 DIP, stop and report the measured conflict instead of silently redesigning or shrinking fonts/controls.

---

## 13. Preserve all Settings behavior

The width change must not modify:

```text
IRuntimeControl
Control Protocol v1
snapshot projection
mutation handlers
activation refresh
runtime-loss close behavior
single-instance behavior
tray launch behavior
framework-dependent packaging
icon handling
```

In particular preserve PR #229's startup crash fix:

```text
OpacitySlider.ValueChanged is NOT wired in XAML.
It is attached only after InitializeComponent + ViewModel/DataContext setup.
```

Do not move `ValueChanged="OnOpacityValueChanged"` back into XAML.

Preserve PR #230's icon configuration and compact-density values unchanged.

---

## 14. WPF geometry regression test update

Update:

```text
tests/ClawHUD.Settings.Tests/MainWindowStartupTests.cs
```

The existing startup test currently pins:

```text
Width  = 700
Height = 600
ResizeMode = NoResize
```

Change only the width expectation:

```csharp
Assert.Equal(600, width);
Assert.Equal(600, height);
Assert.Equal(ResizeMode.NoResize, resizeMode);
```

Keep all PR #229 regression assertions proving that real `MainWindow` construction:

```text
- does not throw;
- does not dispatch PreviewHudOpacity;
- does not dispatch CommitHudOpacity.
```

Do not weaken or replace that test.

---

# Part C — Correct the opacity label to `HUD opacity`

## 15. Current mismatch

The current WPF Settings page displays:

```text
Background opacity
```

but the production implementation does **not** apply the user-selected value only to the HUD background.

Current runtime behavior intentionally uses whole-HUD opacity:

```text
renderer background -> kept opaque for this path
Layered HWND alpha  -> user-selected HUD opacity
```

As a result, changing the slider fades the HUD as a whole, including:

```text
background
text
numbers
units
separators / foreground decoration
```

This behavior is already part of the validated production presentation path and is **not being redesigned in this PR**.

The user-facing label is therefore misleading and must be corrected.

---

## 16. Required label-only change

In:

```text
src/ClawHUD.Settings/MainWindow.xaml
```

change the visible label only:

```xml
Text="Background opacity"
```

to:

```xml
Text="HUD opacity"
```

Do **not** rename or alter runtime data contracts merely to match the UI string.

Unless required by an existing user-visible string/test, keep internal compatibility names unchanged, including existing concepts such as:

```text
BackgroundOpacityPercent
backgroundOpacity
PreviewHudOpacity
CommitHudOpacity
```

Those are implementation/protocol names and changing them would create unnecessary churn for a label-only fix.

Do not change the slider range, step, preview behavior, commit behavior, persistence, or startup semantics.

Required runtime behavior remains:

```text
50% .. 100%
5% steps
Preview while dragging
Commit on completed gesture
no opacity mutation during initial XAML construction
```

---

## 17. Opacity presentation contract — do not reinterpret this item

This work order does **not** ask to implement a new background-only opacity feature.

Do not change:

```text
HudController::SetOpacity behavior
SetLayeredWindowAttributes usage
renderer alpha policy
window-wide HUD alpha behavior
Presentation API / DirectComposition path
premultiplied-alpha presentation contract
```

The purpose of this item is only:

> Make the Settings wording accurately describe the existing whole-HUD opacity behavior.

No new opacity mechanism is authorized.

---

# Part D — Explicit non-goals

## 18. Do not include anything else

This PR must **not** modify or attempt to improve the following items unless needed only to remove `ProcessLifecycleSource` references or make the two explicit WPF text/geometry edits above:

```text
Snipping Tool optional mutable package-path diagnostic failures
WindowLifecycleSource
WindowsGameIdentitySource
PresentActivitySource
PresentMon FPS logic
self-FPS exclusion from PR #230
ExcludedExecutable LOCATIONCHANGE suppression from PR #230
GameSessionController production semantics
ProductionTargetPolicy behavior
Steam detection
Microsoft/Xbox detection
EC telemetry
battery telemetry
VRR tweak logic
VeloPack/update behavior
Settings icon
Settings vertical density
Settings height
opacity implementation / alpha policy
```

No opportunistic cleanup.

---

# Part E — HUD / VRR safety contract

## 19. HUD presentation is zero-touch

This PR has no reason to alter HUD presentation.

There must be **zero intentional behavioral diff** to:

```text
HUD windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
WS_EX_LAYERED behavior
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
ProductionHudPresentationContract()
Presentation API / DirectComposition production path
independent-flip requirement
premultiplied-alpha contract
current whole-HUD opacity implementation
```

Do not edit HUD presentation/rendering code while implementing this work order.

All existing HUD/VRR contract tests must remain present and green.

---

# Part F — Expected files

## 20. Expected deletions

```text
src/ClawHUD/GameDetection/ProcessLifecycleSource.h
src/ClawHUD/GameDetection/ProcessLifecycleSource.cpp
tests/ProcessLifecycleSourceTests.cpp
```

## 21. Expected modifications

Likely:

```text
src/ClawHUD/GameDetection/DebugObservationController.h
src/ClawHUD/GameDetection/DebugObservationController.cpp
CMakeLists.txt
cmake/ClawHUDTests.cmake
src/ClawHUD.Settings/MainWindow.xaml
tests/ClawHUD.Settings.Tests/MainWindowStartupTests.cs
docs/GAME_DETECTION_PRODUCTION_DESIGN.md
```

The opacity label and width change are both expected to occur in the existing `MainWindow.xaml` modification.

Do not expand the file list without a concrete compile/reference reason.

---

# Part G — Verification

## 22. Native build/test

Run at minimum the repository's normal native Release validation used by CI.

Expected result:

```text
native Release build succeeds
CTest succeeds
ProcessLifecycleSource test target no longer exists
all remaining HUD/VRR/game-detection tests remain green
```

Because the removal changes native source composition, a Debug build/CTest pass is also recommended if inexpensive and consistent with the current project workflow.

Do not interpret the reduced total test count caused by deleting `ClawHUD.ProcessLifecycleSourceTests` as a regression.

---

## 23. WPF build/test

Run:

```text
dotnet build src/ClawHUD.Settings/ClawHUD.Settings.csproj -c Release
dotnet test tests/ClawHUD.Settings.Tests/ClawHUD.Settings.Tests.csproj -c Release
```

Expected:

```text
Release build succeeds
all WPF tests pass
MainWindow startup regression remains green
geometry test expects 600 x 600 DIP
visible opacity label is HUD opacity
```

No packaging workflow change is expected.

---

## 24. Source/reference checks

Before completion, search the active source tree/build files for stale references.

Expected active-code result:

```text
ProcessLifecycleSource -> none
ProcessLifecycleSourceTests -> none
Win32_ProcessStartTrace -> none
Win32_ProcessStopTrace -> none
```

Historical docs may still contain old references where they are intentionally preserved as history.

Also verify that `wbemuuid` was **not blindly removed** from targets that still require WMI.

For the Settings label, verify the user-facing XAML no longer contains:

```text
Background opacity
```

and contains:

```text
HUD opacity
```

Do not require internal wire/model identifiers containing `backgroundOpacity` to be renamed.

---

# Part H — Manual MSI Claw smoke

## 25. Settings geometry and label

On the MSI Claw at:

```text
1920 x 1200 @ 150%
```

open Settings from the tray.

Verify:

```text
- window is visibly narrower than PR #230;
- approximate physical footprint is 900 x 900 px;
- all five cards fit without scrolling;
- no text clipping;
- In-game only / Segoe UI Variable / Content width labels fit;
- HUD size stepper is intact;
- opacity row is labeled exactly "HUD opacity";
- opacity slider and percentage text fit;
- changing HUD opacity still fades the complete HUD as before;
- title-bar/product icon remains present;
- window remains fixed/non-resizable.
```

Open/close several times and verify no WER/Ghost/Not Responding regression.

---

## 26. Runtime log smoke

Start ClawHUD with developer debug logging enabled, if convenient for validation.

Verify there are **no** startup lines beginning with:

```text
[ProcessLifecycle]
```

and no:

```text
Process lifecycle diagnostic source failed to start; continuing
```

The remaining debug observation sources should continue behaving as before.

No production game-detection behavior should differ merely because the failed diagnostic source was removed.

---

# Part I — Completion criteria

## 27. Done means

This PR is complete when all of the following are true:

```text
[ ] ProcessLifecycleSource.h deleted
[ ] ProcessLifecycleSource.cpp deleted
[ ] ProcessLifecycleSourceTests.cpp deleted
[ ] DebugObservationController no longer includes/owns/starts/stops it
[ ] native source list no longer builds it
[ ] dedicated CTest target removed
[ ] current production game-detection design records it as retired/removed
[ ] no replacement polling/elevated diagnostic mechanism was added
[ ] WMI dependencies still needed elsewhere were preserved
[ ] Settings width is exactly 600 DIP
[ ] Settings height remains exactly 600 DIP
[ ] MainWindowStartupTests expects 600 x 600 and retains PR #229 opacity assertions
[ ] Settings visible label is exactly "HUD opacity"
[ ] opacity slider range/preview/commit/persistence behavior is unchanged
[ ] current whole-HUD alpha implementation is unchanged
[ ] no Settings IPC mutation semantics changed
[ ] native build + remaining CTests pass
[ ] WPF Release build + tests pass
[ ] HUD/VRR presentation contract has zero behavioral diff
[ ] manual target-device Settings smoke shows no clipping
[ ] runtime log no longer contains ProcessLifecycle startup warnings
```

---

## 28. PR framing

Suggested PR title:

```text
Remove dead process-lifecycle diagnostics and finish Settings polish
```

Suggested summary structure:

```text
Part A — remove ProcessLifecycleSource
- delete failed WMI diagnostic source and dedicated tests
- remove DebugObservationController/build references
- retain the three useful debug observers
- retain WMI dependencies still used by production hardware/tweak code

Part B — Settings width
- fixed geometry 700x600 -> 600x600 DIP
- startup geometry test updated

Part C — HUD opacity label
- Background opacity -> HUD opacity
- label-only correction; existing whole-HUD alpha behavior unchanged

Validation
- native build/CTest
- WPF Release build/tests
- HUD/VRR presentation zero behavioral diff
```

Do not claim the removed source was production game-detection authority. It was diagnostic-only and had already been excluded from the production architecture.
