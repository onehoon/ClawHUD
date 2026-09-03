# UI Refactor PR3 — HUD Setting Mutations with Authoritative Snapshot Reconciliation

**Date:** 2026-09-03  
**Status:** Ready for implementation  
**Reviewed baseline:** `main` at `484b9b29bf914bbb43916454515cb4eb1e85f007`  
**Previous PR:** #224 — Read-Only Control IPC Client and Runtime State Projection  
**Parent plan:** `docs/UI refact/CLAW_HUD_SETTINGS_WPF_UI_REFACTOR_PLAN_2026-09-03.md`

---

## 1. Objective

Enable the HUD controls in the first three WPF Settings cards to mutate the existing ClawHUD runtime through Control IPC.

PR3 is the first write-enabled WPF Settings PR.

At the end of this PR, the WPF frontend must support:

```text
SetHudEnabled
SetHudVisibilityMode
SetHudSizeOffset
SetHudFont
SetHudAlignment
SetHudBackgroundMode
```

The following stay deferred:

```text
PreviewHudOpacity / CommitHudOpacity     -> PR4
SetIntelVrrRangeFixEnabled               -> PR5
SetStartWithWindows                      -> PR5
production tray cutover / packaging      -> PR6
```

The central contract is:

> **The requested value is never the frontend source of truth. The authoritative post-mutation SettingsSnapshot returned by ClawHUD is the source of truth.**

The WPF process must not persist settings directly.

---

## 2. Development-stage freedom

ClawHUD is still under active development and has not shipped this WPF Settings frontend.

Therefore PR3 may refactor the PR2 frontend implementation freely when that produces a cleaner final architecture.

Do not preserve temporary PR2 shapes merely for compatibility.

Specifically, it is acceptable to:

- replace the current immutable/read-only `MainViewModel` with a mutable notification-based ViewModel;
- replace `EncodeReadRequest` with a general typed protocol-v1 request encoder;
- refactor `RuntimeControlClient.ExecuteAsync` so reads and mutations share one transport path;
- change internal DTO/helper names;
- remove obsolete PR2-only comments or test seams;
- reorganize frontend-only code if the result is simpler.

Do **not** add compatibility shims, deprecated aliases, fallback command formats, dual code paths, or temporary migration layers for the WPF frontend.

The current native Control Protocol v1 remains authoritative and must not be changed merely to simplify the C# client.

---

## 3. Current runtime contract

The native runtime already supports every required PR3 operation.

`RuntimeControlWireMapping.cpp` currently handles successful mutations by producing a fresh snapshot through the equivalent of:

```text
mutation
  -> IRuntimeControl / App
  -> runtime applies semantic behavior
  -> possible runtime recreation / rollback
  -> GetSettingsSnapshot()
  -> wire SettingsSnapshot
  -> response
```

This has an important consequence for frontend design.

A successful protocol response can still contain the previous effective value if the runtime operation internally reverted to it.

Examples include settings that may require HUD recreation such as font/background/layout changes.

Therefore this frontend pattern is forbidden:

```text
click Right
-> locally set Alignment = Right
-> assume success
```

Use this instead:

```text
click Right
-> send SetHudAlignment(Right)
-> receive response
-> if response contains authoritative SettingsSnapshot:
       replace ViewModel state from that snapshot
-> render whatever the runtime says is effective
```

`SetHudEnabled` may explicitly return `OperationFailed`; protocol errors must not fabricate state.

---

## 4. Hard scope boundary

### 4.1 In scope

PR3 may modify only the WPF frontend and its tests/CI as needed to implement:

1. mutation request encoding for the six HUD operations;
2. response decoding for mutation responses carrying `SettingsSnapshot`;
3. `RuntimeControlClient` mutation APIs;
4. a mutable authoritative-snapshot ViewModel;
5. click/touch interaction for cards 1-3 except opacity;
6. in-flight interaction protection;
7. runtime response reconciliation;
8. frontend error recovery that restores/retains authoritative state;
9. tests for mutation wire frames, transport, ViewModel reconciliation, and duplicate interaction suppression.

### 4.2 Out of scope

Do not implement in PR3:

- `PreviewHudOpacity`;
- `CommitHudOpacity`;
- opacity slider interaction;
- `SetIntelVrrRangeFixEnabled`;
- `SetStartWithWindows`;
- `RequestShutdown`;
- tray launching of the WPF frontend;
- duplicate Settings process handling;
- release packaging / VeloPack changes;
- .NET prerequisite changes;
- legacy Win32 Settings removal;
- polling;
- runtime -> frontend event subscription;
- localization;
- About UI;
- new pages, tabs, sidebar, NavigationView, or ScrollViewer;
- native Control Protocol changes;
- native pipe server changes;
- `IRuntimeControl` changes;
- `App` changes;
- `HudController` changes;
- `HudSettingsStore` changes;
- HUD rendering/presentation changes;
- telemetry/game-detection/PresentMon/EC changes.

Cards 4 and 5 remain read-only in this PR.

The Background Opacity slider remains read-only in this PR.

---

## 5. HUD / VRR presentation safety — zero touch

Expected native HUD/presentation diff: **zero**.

Do not modify or weaken:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- current `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent flip requirements;
- Presentation API / DirectComposition production path;
- premultiplied-alpha behavior.

PR3 is a frontend Control IPC consumer change only.

---

## 6. Request encoder refactor

PR2 intentionally only implemented `EncodeReadRequest`.

PR3 may replace it with a small general protocol-v1 request encoder.

A recommended shape is:

```csharp
internal sealed record ControlRequest(
    ControlOperation Operation,
    uint RequestId,
    bool? Flag = null,
    byte? WireEnum = null,
    int? SizeOffset = null,
    ushort? OpacityPercent = null);

internal static byte[] EncodeRequest(ControlRequest request)
```

The exact API may differ.

Do not use generic key/value payloads, JSON, reflection-based serialization, raw struct marshaling, or `object` payloads.

### 6.1 Empty payload operations

Existing reads remain:

```text
GetRuntimeInfo
GetSettingsSnapshot
```

Payload size = 0.

### 6.2 Boolean mutation

PR3 uses:

```text
SetHudEnabled = operation 11
```

Payload is exactly one byte:

```text
0 = false
1 = true
```

### 6.3 Enum mutations

PR3 uses:

```text
SetHudVisibilityMode = 12
SetHudFont           = 14
SetHudAlignment      = 15
SetHudBackgroundMode = 16
```

Payload is exactly one validated byte containing the existing protocol-v1 wire enum value.

### 6.4 HUD size mutation

```text
SetHudSizeOffset = 13
```

Payload is exactly one signed little-endian i32.

Valid range remains:

```text
-2 .. +2
```

The C# encoder must reject invalid values before transport.

### 6.5 Do not pre-implement deferred mutation payloads

The operation enum may continue to contain all protocol-v1 IDs, but PR3 does not need send APIs for:

```text
PreviewHudOpacity
CommitHudOpacity
SetIntelVrrRangeFixEnabled
SetStartWithWindows
RequestShutdown
```

Do not add unused public/internal client methods simply because the IDs are known.

---

## 7. Mutation response decoding

The current decoder accepts typed payloads only for the two read operations.

Extend it so a successful response for these PR3 operations carries and decodes the normal authoritative settings snapshot:

```text
SetHudEnabled
SetHudVisibilityMode
SetHudSizeOffset
SetHudFont
SetHudAlignment
SetHudBackgroundMode
```

Conceptually:

```csharp
switch (expectedOperation)
{
    case GetRuntimeInfo:
        return DecodeRuntimeInfo(...);

    case GetSettingsSnapshot:
    case SetHudEnabled:
    case SetHudVisibilityMode:
    case SetHudSizeOffset:
    case SetHudFont:
    case SetHudAlignment:
    case SetHudBackgroundMode:
        return DecodeSnapshot(...);
}
```

Do not duplicate snapshot decoders per operation.

All existing strict validation remains:

- exact v1 header;
- correlated request ID;
- correlated operation ID;
- known status;
- max frame/payload limits;
- exact payload length;
- strict UTF-8;
- bool/enums/product-bound validation;
- no trailing bytes.

For a non-`Ok` response:

- require empty payload;
- return typed protocol error;
- do not update ViewModel state from fabricated/default values.

---

## 8. RuntimeControlClient design

Refactor the client so both reads and writes use one shared transport path.

The transport contract stays:

```text
one method call
  -> one fresh NamedPipeClientStream
  -> one request
  -> one response
  -> close
```

No persistent connection.

No polling.

No automatic retry loop.

### 8.1 Required mutation methods

Expose methods equivalent to:

```csharp
Task<ControlClientResult<SettingsSnapshot>> SetHudEnabledAsync(bool enabled, ...);
Task<ControlClientResult<SettingsSnapshot>> SetHudVisibilityModeAsync(WireVisibilityMode mode, ...);
Task<ControlClientResult<SettingsSnapshot>> SetHudSizeOffsetAsync(int offset, ...);
Task<ControlClientResult<SettingsSnapshot>> SetHudFontAsync(WireFont font, ...);
Task<ControlClientResult<SettingsSnapshot>> SetHudAlignmentAsync(WireAlignment alignment, ...);
Task<ControlClientResult<SettingsSnapshot>> SetHudBackgroundModeAsync(WireBackgroundMode mode, ...);
```

Method names may differ, but keep them typed.

Do not expose a generic `SendCommand(string, object)` API.

### 8.2 Request IDs

Continue non-zero client-generated u32 request IDs.

Since the UI should serialize user mutations while one is in flight, high-concurrency request ID generation is not a product requirement.

Do not add unnecessary synchronization machinery solely for theoretical concurrent calls.

### 8.3 Timeout/cancellation

Preserve bounded behavior.

A mutation must not hang the WPF UI if the runtime disappears while Settings is open.

Continue to use async pipe I/O and cancellation/timeout.

Do not call `.Result`, `.Wait()`, or block the WPF dispatcher.

---

## 9. ViewModel redesign

PR2's immutable projection was appropriate for read-only state but should not be preserved if it complicates mutations.

PR3 should use a mutable ViewModel that can apply a complete authoritative snapshot repeatedly.

Recommended shape:

```csharp
internal sealed class MainViewModel : INotifyPropertyChanged
{
    internal void ApplySnapshot(SettingsSnapshot snapshot)
    {
        // replace every projected setting from the runtime snapshot
        // notify changed properties
    }
}
```

An equivalent implementation is acceptable.

### 9.1 State ownership

The ViewModel owns only a **projection of the latest authoritative runtime snapshot**.

It does not own persisted settings.

It must not write files.

It must not silently modify local state before a successful mutation response arrives.

### 9.2 Keep enough raw state for commands

Store canonical current values needed to derive the next request, particularly:

```text
HudSizeOffset
```

The size buttons must compute from the current authoritative offset, not parse `HudSizeLabel`.

Example:

```text
current offset = +1
user taps +
requested offset = +2
```

At +2, the + button should be disabled.

At -2, the - button should be disabled.

### 9.3 Full snapshot application

After every successful mutation, apply the **whole returned snapshot**, not only the property the user attempted to change.

This preserves correctness if runtime semantics affect more than one projected field now or later.

---

## 10. UI interaction model

Cards 1-3 become interactive except for Background Opacity.

Cards 4-5 remain non-interactive.

### 10.1 Enable HUD

Enable the existing HUD switch.

Interaction:

```text
user requests opposite of current authoritative HudEnabled
  -> SetHudEnabledAsync(requested)
  -> success with snapshot -> ApplySnapshot(snapshot)
  -> protocol/transport failure -> leave/restore previous authoritative state
```

Do not make `IsChecked` TwoWay to the local ViewModel in a way that makes the clicked value authoritative before IPC finishes.

A code-behind event forwarding to ViewModel/service is acceptable for this small app; do not introduce a large MVVM framework solely to avoid code-behind.

### 10.2 Display mode

Enable:

```text
In-game only
Always
```

Map exactly:

```text
In-game only -> WireVisibilityMode.InGameOnly
Always       -> WireVisibilityMode.Always
```

If the selected button is already the authoritative value, do not send a redundant mutation.

### 10.3 HUD size

Enable `-` and `+`.

Use current authoritative `HudSizeOffset`.

Rules:

```text
minimum = -2
maximum = +2
```

- disable `-` at -2;
- disable `+` at +2;
- one tap = one offset step;
- do not allow repeated rapid taps to queue multiple stale requests while a previous mutation is still in flight.

### 10.4 Font

Enable:

```text
Unispace
Segoe UI Variable
```

Map directly to the existing wire enum.

Runtime rollback must be reflected from the returned snapshot.

### 10.5 Alignment

Enable:

```text
Left
Center
Right
```

Map directly to `WireAlignment`.

### 10.6 Background width

Enable:

```text
Full width
Content width
```

Map directly to `WireBackgroundMode`.

### 10.7 Background opacity

Keep this slider non-interactive in PR3.

It must continue to display the authoritative runtime value from the snapshot.

Do not implement a simple value-changed setter as a shortcut. Preview/Commit semantics belong to PR4.

### 10.8 Intel VRR / Start with Windows

Keep both switches read-only in PR3.

They continue to display the authoritative snapshot values.

---

## 11. In-flight mutation policy

The UI is small and mutations are user-driven.

Use the simplest reliable policy:

> **Allow only one Settings mutation request in flight at a time.**

While a mutation is in flight:

- disable the interactive HUD controls in cards 1-3, or otherwise prevent additional mutation events;
- keep displaying the last authoritative snapshot;
- do not show the requested speculative value as authoritative;
- after completion, re-enable controls if the runtime is still usable.

This avoids stale overlapping responses such as:

```text
user taps Left
user immediately taps Right
response Left arrives after local state already changed to Right
```

There is no need for a complex request queue, cancellation supersession state machine, or parallel mutation architecture for this Settings page.

### 11.1 Busy state

A simple ViewModel property such as:

```csharp
bool IsMutationInFlight
```

is sufficient.

Bind/control `IsEnabled` at card content level if convenient.

Do not disable the entire window chrome or Close button.

The user must always be able to close Settings.

---

## 12. Error handling

PR3 does not need a large notification framework.

### 12.1 Protocol/runtime operation failure

If the mutation result is:

```text
ProtocolError
MalformedResponse
TransportUnavailable
TimedOut
```

then:

1. do not apply the requested value locally;
2. preserve the last authoritative snapshot on screen;
3. clear the busy state;
4. keep the window responsive.

### 12.2 Runtime disappears

If ClawHUD exits while Settings is open and a user then changes a setting, the request may fail.

That must not crash or hang the WPF process.

For PR3, it is acceptable to leave the last snapshot visible after that failure.

Do not add periodic runtime-liveness polling in this PR.

A later cutover/lifetime PR may decide stronger orphan-window behavior.

### 12.3 User-facing error UI

Do not add a large modal error system in PR3.

A minimal inline/non-modal indication is acceptable only if implementation remains small, but it is not required for merge if failure safely preserves authoritative state.

The key requirement is correctness, not notification polish.

---

## 13. XAML / visual rules

Preserve the PR1/PR2 visual design and geometry.

Do not resize cards or reintroduce scrolling just because controls become interactive.

Primary target remains:

```text
1920 x 1200
150% scale
fixed 700 x 744 DIP window
no horizontal scrollbar
no vertical scrollbar
```

Touch targets must remain approximately the current 40-46 DIP range.

No tiny radio-button hit targets.

No hover-only state communication.

No layout redesign in PR3 unless an actual interaction defect requires a small frontend-only fix.

---

## 14. XAML event/binding guidance

Do not use default `ToggleButton` TwoWay semantics blindly for server-authoritative state.

The current controls visually represent selected state through `IsChecked`, but a user click normally changes `IsChecked` before async IPC completes.

Implement interaction so the runtime remains authoritative.

Acceptable approaches include:

### Option A — click events + OneWay selected-state binding

Keep:

```text
IsChecked = OneWay authoritative projection
```

Handle `Click` and send the intended wire value.

After response:

```text
ApplySnapshot(response.Snapshot)
```

This is simple and appropriate for this small Settings app.

### Option B — commands with OneWay selected-state projection

Use lightweight `ICommand` objects if already useful for testing.

Do not add CommunityToolkit.Mvvm or another dependency solely for six simple controls unless a concrete benefit is demonstrated.

### Forbidden

Avoid:

```text
TwoWay IsChecked
  -> local property mutates immediately
  -> async request
  -> assume request succeeded
```

because this makes the frontend transiently authoritative and complicates rollback.

---

## 15. Initial load behavior

Preserve PR2 startup flow:

```text
window Loaded
  -> GetRuntimeInfo
  -> protocol compatibility check
  -> title = ClawHUD <runtime version>
  -> GetSettingsSnapshot
  -> ApplySnapshot
  -> enable PR3 interactive HUD controls
```

Before a valid initial snapshot exists, HUD mutation controls must not be interactive.

Do not allow commands against fake/default values.

If initial load fails, the window may remain read-only as in PR2.

---

## 16. Tests required

Keep existing PR2 tests and extend them.

### 16.1 Golden mutation request frames

Add explicit byte-level tests for at least:

```text
SetHudEnabled(true)
SetHudVisibilityMode(InGameOnly)
SetHudSizeOffset(-2 or +2)
SetHudFont(SegoeUiVariable)
SetHudAlignment(Right)
SetHudBackgroundMode(ContentWidth)
```

Verify:

- exact 24-byte header;
- operation ID;
- request ID;
- payload size;
- exact payload bytes;
- little-endian i32 size offset.

### 16.2 Encoder rejection

Verify invalid client-side values are rejected:

- size < -2 / > +2;
- invalid wire enum values;
- zero request ID;
- wrong payload shape for an operation if the API permits construction of an invalid DTO.

### 16.3 Mutation response snapshot decoding

For every PR3 mutation operation, verify an `Ok` response with a snapshot decodes as `SettingsSnapshot`.

Do not duplicate the fixture implementation unnecessarily; a parameterized test is preferred.

### 16.4 Protocol-error mutation response

Verify an `OperationFailed` response:

- is surfaced as protocol error;
- has no fake snapshot;
- does not mutate the ViewModel in the mutation coordinator/viewmodel test.

### 16.5 RuntimeControlClient transport tests

Use local `NamedPipeServerStream` tests to verify at least:

- one mutation request is encoded and sent correctly;
- the returned authoritative snapshot is surfaced to the caller;
- one method call uses one connection;
- timeout/cancellation behavior remains bounded.

### 16.6 Authoritative rollback test

This is important.

Simulate conceptually:

```text
current alignment = Center
user requests Right
server returns Ok snapshot with alignment = Center
```

Verify final ViewModel/UI projection remains **Center**.

Do the same style of test for one boolean or font setting if convenient.

The test proves the frontend does not trust the requested value over the response snapshot.

### 16.7 Size boundaries

Verify:

```text
-2 -> minus disabled, plus enabled
+2 -> plus disabled, minus enabled
0  -> label Default
```

### 16.8 Busy/in-flight behavior

Verify a second mutation is not dispatched while a first mutation is in flight.

Do not attempt to test impossible scheduler interleavings; test the actual intended serialized user interaction policy.

---

## 17. CI / build requirements

Continue existing CI:

```text
dotnet build ClawHUD.Settings Release
dotnet test ClawHUD.Settings.Tests Release
native CMake configure/build/CTest
```

Do not alter `Build-Release.yml` in PR3.

Do not publish/package the WPF frontend yet.

Do not add the .NET runtime to repository artifacts.

---

## 18. Manual verification

When a running ClawHUD runtime is available, verify the following.

### 18.1 Initial projection

Open WPF Settings manually.

Confirm all current HUD settings match the legacy Settings/runtime state.

### 18.2 Enable HUD

Toggle Enable HUD.

Confirm:

- HUD runtime behavior changes;
- WPF control returns to the actual runtime state from the response;
- legacy Settings reflects the same state when refreshed/opened.

### 18.3 Display mode

Change Always <-> In-game only.

Confirm WPF selection matches the authoritative response.

### 18.4 Size

Step from -2 through +2.

Confirm:

- one step per tap;
- labels `-2`, `-1`, `Default`, `+1`, `+2`;
- boundary button disable behavior.

### 18.5 Font / alignment / background width

Change each option.

Confirm the WPF selection is updated from the returned snapshot and no stale optimistic choice remains after a runtime rollback/failure.

### 18.6 Deferred controls

Confirm these still cannot mutate runtime in PR3:

```text
Background opacity
Intel VRR Range Fix
Start with Windows
```

### 18.7 Close behavior

Close the WPF window during idle and after mutations.

Confirm:

- Settings process exits;
- ClawHUD runtime remains running;
- no background WPF process remains.

---

## 19. Expected file scope

Likely modified files:

```text
src/ClawHUD.Settings/Protocol/ControlCodec.cs
src/ClawHUD.Settings/Services/RuntimeControlClient.cs
src/ClawHUD.Settings/ViewModels/MainViewModel.cs
src/ClawHUD.Settings/MainWindow.xaml
src/ClawHUD.Settings/MainWindow.xaml.cs

tests/ClawHUD.Settings.Tests/ControlCodecTests.cs
tests/ClawHUD.Settings.Tests/RuntimeControlClientTests.cs
tests/ClawHUD.Settings.Tests/MainViewModelTests.cs
```

Additional small frontend-only helper/test files are acceptable if they materially simplify the implementation.

Expected native runtime diff:

```text
0 lines
```

Expected release packaging diff:

```text
0 lines
```

---

## 20. Completion criteria

PR3 is complete when all of the following are true:

- [ ] `SetHudEnabled` works from WPF Settings.
- [ ] `SetHudVisibilityMode` works from WPF Settings.
- [ ] `SetHudSizeOffset` works from WPF Settings.
- [ ] `SetHudFont` works from WPF Settings.
- [ ] `SetHudAlignment` works from WPF Settings.
- [ ] `SetHudBackgroundMode` works from WPF Settings.
- [ ] Background Opacity remains read-only.
- [ ] Intel VRR Range Fix remains read-only.
- [ ] Start with Windows remains read-only.
- [ ] every successful mutation applies the full authoritative response snapshot.
- [ ] no optimistic requested value is retained as source of truth.
- [ ] protocol/transport failure leaves the last authoritative UI state intact.
- [ ] only one mutation is allowed in flight at a time.
- [ ] WPF UI remains responsive during pipe I/O.
- [ ] size +/- respects -2..+2 boundaries.
- [ ] current 700x744 no-scroll layout is preserved.
- [ ] touch targets remain usable.
- [ ] existing PR2 read/runtime-info behavior still works.
- [ ] all WPF tests pass.
- [ ] native CMake/CTest remains green.
- [ ] no native HUD/presentation/runtime-control implementation was changed.
- [ ] no release/VeloPack packaging change was made.

---

## 21. Final architecture after PR3

```text
ClawHUD.Settings.exe
  WPF MainWindow
      |
      v
  MainViewModel
  latest authoritative snapshot projection
      |
      v
  RuntimeControlClient
      |
      | one request / one connection
      v
\\.\pipe\ClawHUD.Control.<sessionId>
      |
      v
ClawHUD.exe
  RuntimeControlPipeServer
      |
      v
RuntimeControlDispatchBridge
      |
      v
IRuntimeControl / App
      |
      +-- HudController
      +-- HudSettingsStore
```

The WPF frontend becomes an actual runtime controller in PR3, but **runtime authority and persistence ownership remain entirely inside ClawHUD.exe**.
