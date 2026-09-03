# UI Refactor PR5 — Final Settings Mutations, Activation Refresh, and Runtime-Loss UX

**Date:** 2026-09-03  
**Status:** Ready for implementation  
**Reviewed baseline:** `main` at `97ae39b9c10d88133d13948050bfd06671018248`  
**Previous PR:** #226 — Background Opacity Preview / Commit Interaction  
**Parent plan:** `docs/UI refact/CLAW_HUD_SETTINGS_WPF_UI_REFACTOR_PLAN_2026-09-03.md`

---

## 1. Objective

Complete the agreed WPF Settings feature set before production cutover.

PR5 must make the two remaining cards interactive:

```text
Intel VRR Range Fix
Start with Windows
```

through the existing Control IPC operations:

```text
SetIntelVrrRangeFixEnabled = 19
SetStartWithWindows        = 10
```

PR5 also completes the remaining frontend-only behavior needed before the tray/packaging cutover:

- refresh the authoritative settings snapshot when the Settings window is re-activated;
- do this without a polling loop or new runtime event channel;
- close the WPF Settings frontend cleanly when an active IPC interaction proves that the ClawHUD runtime is gone or shutting down;
- preserve the runtime-derived window title/version behavior;
- remove stale PR-stage comments/read-only wording from the final one-page frontend;
- keep the UI English-only, one-page, touch-friendly, and scroll-free.

At the end of PR5, `ClawHUD.Settings.exe` should be functionally complete as a Settings frontend.

PR6 remains responsible for making it the production frontend:

```text
tray launch cutover
single Settings process / bring-to-front policy
release/VeloPack packaging
legacy Win32 Settings removal
production lifecycle integration
```

---

## 2. Development-stage freedom

ClawHUD is still under active development and this WPF frontend has not shipped.

Do not preserve temporary PR2/PR3/PR4 frontend shapes merely for compatibility.

It is acceptable in PR5 to clean up frontend-only naming and flow when that makes the final design simpler, for example:

- rename `AreDiscreteHudControlsEnabled` if a broader final name better represents cards 1-5;
- centralize result classification for runtime-loss detection;
- move activation refresh into `MainViewModel` rather than duplicating transport logic in `MainWindow`;
- remove comments such as `read-only in PR3`;
- simplify test fakes to support the final mutation set.

Do **not** add deprecated aliases, compatibility shims, duplicate mutation paths, fallback protocols, or temporary frontend migration code.

The current native Control Protocol v1 and runtime semantics remain authoritative.

---

## 3. Latest source baseline

### 3.1 `main`

Reviewed baseline:

```text
97ae39b9c10d88133d13948050bfd06671018248
```

This includes PR4 / #226.

The WPF frontend currently supports:

```text
GetRuntimeInfo
GetSettingsSnapshot

SetHudEnabled
SetHudVisibilityMode
SetHudSizeOffset
SetHudFont
SetHudAlignment
SetHudBackgroundMode
PreviewHudOpacity
CommitHudOpacity
```

Cards 4 and 5 still only project snapshot state and are not interactive.

### 3.2 Current ViewModel authority model

`MainViewModel` owns only a projection of the latest authoritative runtime `SettingsSnapshot`.

It currently has:

```text
_snapshot
_mutationInFlight
OpacityInteractionCoordinator
```

and deliberately keeps:

```text
IsChecked = OneWay
```

for discrete controls.

This model remains correct.

The requested value must never become the frontend Settings authority.

### 3.3 Current opacity exclusion model

PR4 added mutual exclusion between ordinary discrete mutations and an active opacity interaction.

Do not create a second mutation lane for cards 4 and 5.

The two new toggles should participate in the same discrete mutation exclusion policy as the existing HUD controls.

Conceptually:

```text
any discrete mutation in flight
    -> opacity cannot start
    -> other discrete settings cannot start

opacity interaction active/finalizing
    -> cards 1-5 discrete mutations cannot start
```

Close/window chrome remains available.

---

## 4. Native semantics that PR5 must preserve

### 4.1 Start with Windows has internal rollback

Current native `App::SetStartWithWindows(bool)` is not a simple assignment.

Current behavior is equivalent to:

```cpp
void App::SetStartWithWindows(bool enabled)
{
    if (startWithWindows_ == enabled)
        return;

    const bool previous = startWithWindows_;
    startWithWindows_ = enabled;

    if (!ApplyStartupRegistration())
    {
        startWithWindows_ = previous;
        return;
    }

    SaveHudSettings();
}
```

Therefore a request can be unable to change the effective setting because shortcut creation/removal failed.

However `RuntimeControlWireMapping.cpp` currently handles the IPC operation as:

```text
SetStartWithWindows(request.flag)
-> GetSettingsSnapshot()
-> ControlStatus::Ok + authoritative snapshot
```

It does **not** convert the internal shortcut failure into `OperationFailed`.

This is intentional existing runtime behavior for the frontend contract.

Therefore this frontend logic is forbidden:

```text
user clicks ON
-> locally show ON because request returned Ok
```

Correct behavior:

```text
user clicks ON
-> SetStartWithWindows(true)
-> response snapshot says true  -> show ON
-> response snapshot says false -> show OFF
```

The response snapshot is the only proof of the effective value.

A merge-critical test must cover this rollback-shaped response.

### 4.2 Intel VRR Range Fix toggle is a persisted preference

Current native behavior is:

```cpp
void App::SetIntelVrrRangeFixEnabled(bool enabled)
{
    intelVrrRangeFixEnabled_ = enabled;
    hudSettingsStore_.SaveIntelVrrRangeFixEnabled(enabled);
}
```

The runtime starts the tweak startup coordinator separately during application startup using the persisted/current value.

Therefore the Settings toggle means:

> enable or disable the Intel VRR Range Fix preference for the runtime/product policy.

Do **not** invent new semantics in PR5 such as:

- immediately re-running the VRR fix when the toggle is turned on;
- immediately reverting panel state when the toggle is turned off;
- calling Intel graphics APIs directly from WPF;
- showing new VRR execution controls/status surfaces;
- changing the existing tweak startup coordinator.

PR5 only calls the existing `SetIntelVrrRangeFixEnabled` Control operation and renders the returned snapshot.

### 4.3 Both operations already return authoritative snapshots

Current runtime mapping already does:

```text
SetStartWithWindows
    -> mutation
    -> SnapshotResponse

SetIntelVrrRangeFixEnabled
    -> mutation
    -> SnapshotResponse
```

No native runtime API or wire-protocol change is required.

---

## 5. Scope

### 5.1 In scope

PR5 may modify the WPF frontend and its tests to implement:

1. bool request encoding for `SetStartWithWindows`;
2. bool request encoding for `SetIntelVrrRangeFixEnabled`;
3. successful response snapshot decoding for both operations;
4. typed `RuntimeControlClient` methods for both operations;
5. interactive card 4 toggle;
6. interactive card 5 toggle;
7. shared discrete mutation exclusion with cards 1-3 and opacity;
8. authoritative rollback/reconciliation for both toggles;
9. activation-time snapshot refresh;
10. bounded runtime-loss handling / clean frontend close;
11. final runtime-version/title behavior review;
12. final cleanup of PR-stage frontend comments/read-only labels;
13. automated tests for all above frontend behavior.

### 5.2 Out of scope

Do not implement in PR5:

- tray launching of `ClawHUD.Settings.exe`;
- duplicate Settings process detection / bring-to-front;
- `RequestShutdown` from WPF;
- tray Exit changes;
- release/VeloPack packaging changes;
- .NET Desktop Runtime prerequisite/install changes;
- legacy Win32 `SettingsWindow*` removal;
- native `App::OpenSettings()` cutover;
- continuous polling;
- a runtime push/event subscription channel;
- Intel VRR algorithm changes;
- Intel VRR immediate apply/revert behavior;
- display/driver manipulation from WPF;
- new Intel VRR status/result UI;
- localization;
- About page;
- tabs/navigation/sidebar/ScrollViewer;
- HUD renderer/presentation changes;
- telemetry/game detection/PresentMon/EC changes.

The native runtime should have **zero source diff** in this PR.

---

## 6. HUD / VRR presentation contract — zero touch

This remains a frontend-only Control IPC consumer change.

Do not modify, replace, weaken, or work around:

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

Do not change background-opacity semantics in this PR.

Expected HUD/presentation diff:

```text
zero
```

---

## 7. Protocol encoder changes

The C# `ControlOperation` enum already contains:

```csharp
SetStartWithWindows = 10,
SetIntelVrrRangeFixEnabled = 19,
```

The existing `ControlRequest` already has:

```csharp
bool? Flag
```

Do not add a new payload field.

### 7.1 Shared bool payload shape

Both new operations use the same protocol-v1 one-byte bool payload as `SetHudEnabled`:

```text
false -> 0x00
true  -> 0x01
payload size = 1
```

Refactor the existing encoder cleanly, e.g. conceptually:

```csharp
ControlOperation.SetStartWithWindows or
ControlOperation.SetHudEnabled or
ControlOperation.SetIntelVrrRangeFixEnabled
    => new[] { (byte)(RequireFlag(request) ? 1 : 0) };
```

Exact syntax is implementation choice.

Do not duplicate bool encoders.

### 7.2 Validation

Both operations must reject a missing `Flag` before transport.

Existing zero request-id validation remains.

`RequestShutdown` remains unsupported by the WPF sender in PR5.

---

## 8. Response decoder changes

Extend the existing snapshot-carrying operation classification to include:

```text
SetStartWithWindows
SetIntelVrrRangeFixEnabled
```

Conceptually:

```csharp
private static bool CarriesSnapshot(ControlOperation operation) => operation is
    ...
    ControlOperation.SetStartWithWindows or
    ControlOperation.SetIntelVrrRangeFixEnabled;
```

Reuse the existing one `DecodeSnapshot` implementation.

Do not add operation-specific snapshot DTOs.

All existing strict response validation remains:

- protocol version/header;
- response kind;
- request-id correlation;
- operation correlation;
- known status;
- exact payload length;
- enum/bool/product bounds;
- strict UTF-8;
- no trailing bytes.

---

## 9. RuntimeControlClient additions

Add typed methods equivalent to:

```csharp
Task<ControlClientResult<SettingsSnapshot>> SetStartWithWindowsAsync(
    bool enabled,
    CancellationToken cancellationToken = default);

Task<ControlClientResult<SettingsSnapshot>> SetIntelVrrRangeFixEnabledAsync(
    bool enabled,
    CancellationToken cancellationToken = default);
```

Both must reuse the existing shared:

```text
Snapshot(...)
-> ExecuteAsync(...)
-> one fresh NamedPipeClientStream
-> one request
-> one response
-> close
```

Do not introduce:

- a second pipe client;
- persistent pipe sessions;
- retry loops;
- polling;
- synchronous `.Wait()` / `.Result` calls.

---

## 10. ViewModel mutation behavior

Cards 4 and 5 are normal discrete Settings mutations.

They should use the same authoritative mutation path as the existing card 1-3 discrete controls.

Recommended intent methods:

```csharp
internal Task ToggleIntelVrrRangeFixAsync()
internal Task ToggleStartWithWindowsAsync()
```

or equivalent typed methods.

Each should derive the request from the current authoritative snapshot:

```text
requested = !current authoritative value
```

then:

```text
send mutation
-> success snapshot -> apply whole snapshot
-> failure -> keep/reassert previous authoritative snapshot
```

Do not make their `IsChecked` bindings TwoWay-authoritative.

Keep:

```text
Mode=OneWay
```

and use click/command intent forwarding just like the existing HUD toggles.

### 10.1 Start-with-Windows rollback requirement

This case must work without any special UI workaround:

```text
snapshot.StartWithWindows = false
user clicks ON
request SetStartWithWindows(true)
runtime cannot create shortcut
runtime rolls back
IPC returns Ok + snapshot.StartWithWindows = false
UI must show OFF
```

Do not treat `ControlStatus::Ok` as proof that the requested bool became effective.

### 10.2 Intel VRR result field

The returned `SettingsSnapshot` may also contain `IntelVrrLastResult`.

Continue decoding it because it is part of the protocol snapshot.

Do not add a visible result panel in PR5.

---

## 11. Final control-busy model

After PR5 there are discrete controls across all five cards.

A final naming cleanup is encouraged if the current property name becomes misleading.

For example:

```text
AreDiscreteHudControlsEnabled
```

may be renamed to something such as:

```text
AreDiscreteSettingsControlsEnabled
```

or another clear final name.

The exact name is not important.

Required semantics are:

```text
snapshot missing
    -> all Settings mutation controls disabled

ordinary discrete mutation in flight
    -> all discrete controls disabled
    -> opacity slider disabled

opacity interaction / finalization active
    -> all discrete controls disabled
    -> opacity slider remains usable for its own active gesture

idle + snapshot available
    -> all controls enabled
```

Cards 4 and 5 must participate in this same guard.

Do not create separate independent busy flags for Start-with-Windows and Intel VRR.

---

## 12. Activation-time authoritative refresh

A separate WPF frontend has no runtime push event channel.

Runtime state can change outside the WPF window, for example:

- F8 HUD toggle;
- legacy Win32 Settings during development before PR6 cutover;
- another valid Control IPC client;
- startup/shortcut state rollback reflected by runtime state.

PR5 should add a targeted snapshot refresh on **window activation**.

Do not add continuous polling.

### 12.1 Desired flow

After the initial load has completed:

```text
Settings window becomes active again
    -> if frontend is idle
    -> GetSettingsSnapshot
    -> success: ApplySnapshot(whole snapshot)
```

This should use the existing `RuntimeControlClient`.

Do not fetch `GetRuntimeInfo` on every activation.

The title/version is stable for the runtime instance and was already obtained at initial load.

### 12.2 Avoid duplicate initial load

WPF `Activated` may occur around the initial window show before or during `Loaded`.

Guard activation refresh so startup does not accidentally issue duplicate initial snapshot requests.

Use a simple state such as:

```text
_initialLoadComplete
_refreshInFlight
```

or equivalent.

### 12.3 Do not refresh through active interactions

Do not begin activation refresh while:

- a discrete mutation is in flight;
- opacity drag/finalization is active;
- another activation refresh is already in flight.

Do not cancel or overwrite an active opacity gesture with an activation snapshot.

Skipping one refresh in those circumstances is acceptable; no queue/state machine is required.

### 12.4 No focus-within-control spam

Use window activation, not generic control-focus changes.

Tabbing between controls inside the same window must not issue IPC refreshes.

---

## 13. Runtime-unavailable / orphan-window behavior

The final WPF frontend should not remain indefinitely as a useless orphan once an active IPC interaction proves the native ClawHUD runtime is gone or shutting down.

Do **not** add a timer or polling heartbeat.

Use existing active IPC points:

- initial load;
- activation refresh;
- user mutations;
- opacity Preview/Commit.

### 13.1 Terminal runtime-loss outcomes

Treat these as evidence that the current frontend can no longer control its runtime instance:

```text
ControlClientOutcome.TransportUnavailable

ProtocolError with status:
    RuntimeUnavailable
    ShuttingDown
```

A malformed/incompatible protocol response during initial load should also fail closed because the frontend cannot safely project/control the runtime.

Initial explicit protocol version incompatibility also closes the frontend.

### 13.2 Timeout is not automatically terminal

A timeout can be transient.

Do not necessarily close the window solely because one request timed out.

For a timeout:

- keep the last authoritative snapshot;
- clear the busy state;
- keep the UI responsive;
- allow a later activation/user interaction to retry naturally.

Do not add automatic retry loops.

### 13.3 Initial load failure

Since the production Settings frontend is intended to be launched from an existing ClawHUD runtime/tray, a startup failure to establish a valid runtime Control session should not leave a blank, fake, inert window behind.

Preferred final behavior:

```text
GetRuntimeInfo fails terminally / incompatible
    -> close Settings cleanly

GetSettingsSnapshot fails terminally
    -> close Settings cleanly
```

No large error page is required.

Do not fabricate values from defaults or the WPF assembly.

A message box is not required unless implementation finds it materially useful; silent clean close is acceptable for this small transient frontend.

### 13.4 Runtime loss after window is open

If an active operation detects terminal runtime loss:

```text
mark/request frontend close
-> close MainWindow on the WPF Dispatcher
-> process exits through the normal WPF main-window shutdown behavior
```

Closing Settings must never attempt to terminate/restart `ClawHUD.exe`.

Use `Dispatcher.BeginInvoke` / equivalent if a runtime-loss signal can originate off the UI thread.

### 13.5 Keep result classification small

A small frontend-only helper/method/event is enough.

Examples:

```csharp
IsTerminalRuntimeLoss(result)
RuntimeUnavailableDetected event
RequestFrontendClose callback
```

Do not create a generic health-monitor framework.

Because opacity has its own coordinator, ensure Preview/Commit terminal failures are also surfaced to the same frontend-close path rather than being silently swallowed forever.

---

## 14. Window title / version final behavior

PR2 already implemented:

```text
GetRuntimeInfo.applicationVersion
-> Title = "ClawHUD <version>"
```

Keep this as the final behavior.

Do not use:

- WPF assembly version as a user-visible fallback;
- hard-coded product version;
- About page version text.

If runtime info cannot be established, the final PR5 runtime-unavailable policy closes the frontend rather than fabricating a version.

No additional title chrome is required.

---

## 15. XAML / card changes

### 15.1 Intel VRR Range Fix card

Current card is read-only with `IsHitTestVisible="False"`.

Make the toggle interactive.

Keep:

```xml
IsChecked="{Binding IntelVrrRangeFixEnabled, Mode=OneWay}"
```

Add the normal shared `IsEnabled` guard and click handler/command intent.

Do not add a second row/result panel.

### 15.2 Start with Windows card

Likewise make the existing toggle interactive.

Keep:

```xml
IsChecked="{Binding StartWithWindows, Mode=OneWay}"
```

and use the shared discrete setting guard.

### 15.3 Geometry

Do not enlarge the page merely because cards 4/5 become interactive.

No new card, description paragraph, status box, or navigation is required.

Preserve:

- one page;
- no `ScrollViewer`;
- fixed-size window;
- current 1920x1200 @ 150% fit;
- touch-size toggles;
- no About UI.

### 15.4 Remove obsolete PR-stage comments

Clean comments such as:

```text
(read-only in PR3)
```

and other temporary phase wording from production-facing XAML/code comments where useful.

Do not turn this into a broad style rewrite.

---

## 16. Protocol / client tests

Extend the existing WPF test project.

### 16.1 Bool request frames

Add golden/decomposed request-frame coverage for at least:

```text
SetStartWithWindows(false)
SetStartWithWindows(true)
SetIntelVrrRangeFixEnabled(false)
SetIntelVrrRangeFixEnabled(true)
```

Verify:

- operation IDs 10 and 19;
- request kind;
- request ID;
- payload size = 1;
- payload = `0x00` / `0x01`;
- status field = 0.

### 16.2 Missing flag rejection

Both operations must throw/reject before transport when `Flag` is missing.

### 16.3 Response decode

For both operations:

```text
Ok + SettingsSnapshot
-> decoded as Success + authoritative snapshot
```

Also keep non-`Ok` typed protocol-error behavior.

### 16.4 Local Named Pipe transport

At least one transport-level test for each new typed client method, or one parameterized test covering both operations, should verify:

```text
client sends exact bool operation/payload
server returns snapshot
client surfaces returned snapshot
```

No production server is required for this test.

---

## 17. ViewModel tests — merge critical

### 17.1 Start-with-Windows authoritative rollback

Required regression:

```text
initial snapshot: StartWithWindows = false
user requests true
fake runtime returns Success snapshot with StartWithWindows = false
UI/ViewModel remains false
```

This is merge critical because it represents a real normal failure path: startup shortcut creation/removal can fail and native App rolls back internally.

### 17.2 Start-with-Windows success

```text
initial false
request true
response snapshot true
-> projected true
```

and optionally the reverse direction.

### 17.3 Intel VRR toggle

Verify both request directions and returned-snapshot projection.

Do not test/pretend that toggling runs the VRR algorithm immediately.

### 17.4 Whole-snapshot reconciliation

A successful card 4/5 mutation must apply the whole returned snapshot, not only the toggled field.

Example:

```text
request StartWithWindows=true
returned snapshot also changes HudEnabled/alignment/etc.
-> all projected fields follow returned snapshot
```

### 17.5 Mutual exclusion

Verify:

```text
opacity interaction active
-> Intel VRR / Start with Windows mutation not dispatched

discrete mutation in flight
-> opacity cannot start
-> second card 4/5 mutation cannot dispatch
```

The existing one-mutation-at-a-time policy remains.

---

## 18. Activation refresh tests

Add focused frontend/ViewModel tests for:

### 18.1 Refresh applies whole snapshot

```text
initial snapshot A
activation refresh returns snapshot B
-> ViewModel projects B
```

### 18.2 Refresh skipped while busy

Verify no refresh request is sent while:

- discrete mutation is in flight;
- opacity interaction/finalization is active;
- refresh already in flight.

No queued refresh is required.

### 18.3 No polling

There should be no timer-based test because no polling loop should exist.

A code inspection/test seam should make it clear refresh only occurs from explicit lifecycle activation.

---

## 19. Runtime-loss tests

### 19.1 Initial load terminal failure

Test or structure code so the window startup path closes cleanly on terminal runtime unavailability/incompatible protocol instead of leaving fake state.

Window-level UI automation is not required if a small coordinator/result method can be unit-tested.

### 19.2 Activation refresh terminal failure

```text
window had valid snapshot
activation refresh -> TransportUnavailable
-> request frontend close
```

### 19.3 User mutation terminal failure

At least one discrete mutation:

```text
TransportUnavailable or ProtocolError(ShuttingDown)
-> previous authoritative snapshot retained
-> frontend-close request raised
```

### 19.4 Opacity terminal failure propagation

Ensure Preview or Commit terminal runtime loss reaches the same close request path.

Do not leave opacity coordinator as a permanently isolated error sink.

### 19.5 Timeout remains recoverable

```text
mutation/refresh timeout
-> no forced frontend close solely for timeout
-> last authoritative snapshot retained
-> controls recover when operation completes/fails
```

---

## 20. Manual validation

When a Claw device / running runtime is available, validate:

### 20.1 Intel VRR Range Fix

1. Open WPF Settings.
2. Confirm toggle matches legacy/current runtime snapshot.
3. Toggle OFF/ON.
4. Confirm returned state is reflected.
5. Close/reopen Settings or restart as appropriate and confirm persisted preference.
6. Do not expect the toggle click itself to perform a new VRR-fix run.

### 20.2 Start with Windows

1. Toggle OFF/ON.
2. Confirm UI follows actual runtime snapshot.
3. Verify startup shortcut registration/removal through the existing product path.
4. If possible, exercise a registration failure/permission/problem case and confirm the UI follows rollback instead of remaining optimistically toggled.

### 20.3 Activation refresh

1. Open WPF Settings.
2. Change a setting externally while WPF is not active, or use F8 for HUD state where applicable.
3. Return focus/activate WPF Settings.
4. Confirm the page refreshes from the runtime snapshot.
5. Confirm no continuous polling occurs while the window remains idle/active.

### 20.4 Runtime exits

1. Open WPF Settings.
2. Exit/stop ClawHUD runtime.
3. Cause the next active IPC point (for example re-activate Settings or attempt a mutation).
4. Confirm Settings closes cleanly rather than remaining as a permanently inert orphan.

### 20.5 Layout/touch regression

At the primary target:

```text
1920x1200
150% scale
```

confirm:

- all five cards fit;
- no vertical scrollbar;
- no horizontal scrollbar;
- card 4/5 toggles remain easy to touch;
- opacity slider remains touch-usable;
- Close remains available while any mutation is busy.

---

## 21. CI / verification

Required automated verification:

```text
dotnet build src/ClawHUD.Settings/ClawHUD.Settings.csproj -c Release
dotnet test tests/ClawHUD.Settings.Tests/ClawHUD.Settings.Tests.csproj -c Release
```

The existing GitHub `Build Test` workflow should continue to run:

```text
WPF Settings build
WPF Settings tests
native CMake configure/build
native CTest
```

Do not change native test expectations merely because the WPF frontend becomes functionally complete.

No `Build-Release.yml` packaging change is required in PR5.

---

## 22. Expected file scope

Likely WPF changes:

```text
src/ClawHUD.Settings/MainWindow.xaml
src/ClawHUD.Settings/MainWindow.xaml.cs
src/ClawHUD.Settings/Protocol/ControlCodec.cs
src/ClawHUD.Settings/Services/RuntimeControlClient.cs
src/ClawHUD.Settings/ViewModels/MainViewModel.cs
src/ClawHUD.Settings/ViewModels/OpacityInteractionCoordinator.cs   (only if needed for runtime-loss propagation)

tests/ClawHUD.Settings.Tests/ControlCodecTests.cs
tests/ClawHUD.Settings.Tests/RuntimeControlClientTests.cs
tests/ClawHUD.Settings.Tests/MainViewModelTests.cs
tests/ClawHUD.Settings.Tests/OpacityInteractionTests.cs            (only if needed)
tests/ClawHUD.Settings.Tests/FakeRuntimeControlClient.cs
```

A small new frontend-only helper for lifecycle/result classification is acceptable if it reduces duplication.

Expected native source changes:

```text
none
```

Do not modify CMake, native Control IPC, HUD, App runtime logic, or VeloPack release packaging in PR5.

---

## 23. PR size guidance

Keep the production implementation narrow.

Target roughly:

```text
production WPF code: ~150-300 LOC changed/added
tests:               ~150-300 LOC changed/added
```

The exact line count is less important than keeping PR5 limited to the final frontend behavior above.

Do not absorb PR6 production cutover work merely to reduce PR count.

---

## 24. Completion criteria

PR5 is complete when all of the following are true:

- `Intel VRR Range Fix` is interactive through `SetIntelVrrRangeFixEnabled`;
- `Start with Windows` is interactive through `SetStartWithWindows`;
- both use one-byte protocol-v1 bool payloads;
- both successful responses decode/apply the whole authoritative snapshot;
- Start-with-Windows rollback-shaped `Ok + unchanged/reverted snapshot` is handled correctly;
- Intel VRR toggle does not invent immediate apply/revert semantics;
- cards 4/5 share the existing discrete mutation/opacity exclusion policy;
- activation-time snapshot refresh is implemented without polling;
- activation refresh never races an active mutation/opacity interaction;
- terminal runtime loss at an active IPC point requests clean WPF frontend close;
- a simple timeout does not automatically kill the frontend;
- title remains `ClawHUD <runtime applicationVersion>`;
- no About page/localization/navigation/ScrollViewer is introduced;
- 1920x1200 @ 150% one-page/touch layout is preserved;
- native runtime/HUD/presentation source diff is zero;
- WPF build/tests and native CI regression suite pass.

After this PR, the remaining UI refactor work should be only the production cutover / packaging / legacy Win32 removal PR.
