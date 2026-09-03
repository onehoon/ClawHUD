# UI Refactor PR2 — Read-Only Control IPC Client and Runtime State Projection

**Date:** 2026-09-03  
**Status:** Ready for implementation  
**Reviewed baseline:** `main` at `726a93c672d2f5448ff0126ec11cb99b0e307288`  
**Previous PR:** #223 — WPF Settings Shell and Visual Foundation  
**Parent plan:** `docs/UI refact/CLAW_HUD_SETTINGS_WPF_UI_REFACTOR_PLAN_2026-09-03.md`

---

## 1. Objective

Connect the new WPF `ClawHUD.Settings.exe` shell to the existing ClawHUD Control IPC **read-only** and project the live runtime state into the already-created Settings page.

PR2 is deliberately narrower than a full Settings-control implementation.

At the end of PR2:

- `ClawHUD.Settings.exe` can connect to the existing per-session ClawHUD Control Named Pipe;
- the client implements protocol-v1 framing/validation required for read operations;
- the client can issue `GetRuntimeInfo`;
- the client can issue `GetSettingsSnapshot`;
- the window title becomes `ClawHUD <runtime-version>` when runtime metadata is available;
- the five existing cards display the **actual runtime snapshot** rather than representative PR1 values;
- all Settings controls remain read-only / non-interactive;
- no setting mutation is sent;
- no persistence is performed by the WPF process;
- the legacy Win32 Settings frontend remains the production frontend;
- tray launch/cutover and VeloPack release packaging remain unchanged.

This PR proves the cross-language frontend/runtime boundary before any write operation is enabled.

The desired progression is:

```text
PR1
  WPF visual/process shell

PR2   <-- this PR
  read-only Control IPC client
  + protocol-v1 response decoding
  + runtime version/snapshot projection

PR3
  HUD setting mutations except opacity commit semantics

PR4
  opacity Preview/Commit interaction

PR5
  Intel VRR + Start with Windows final setting wiring

PR6
  production tray cutover + packaging + legacy Win32 removal
```

---

## 2. Current baseline relevant to PR2

### 2.1 WPF frontend after PR #223

`src/ClawHUD.Settings/` now contains:

```text
ClawHUD.Settings.csproj
App.xaml
App.xaml.cs
MainWindow.xaml
MainWindow.xaml.cs
app.manifest
Styles/SettingsStyles.xaml
```

The current WPF shell is:

- `net10.0-windows`;
- framework-dependent;
- x64;
- built-in Fluent theme through `ThemeMode="System"`;
- Per-Monitor V2 DPI aware;
- fixed `700 x 744 DIP`;
- no resize/minimize/maximize operating mode;
- no `ScrollViewer`;
- one page;
- five headerless cards;
- touch-sized visual controls;
- all card content currently `IsHitTestVisible="False"`;
- no runtime client, persistence, ViewModel, or IPC code.

Preserve those decisions in PR2.

### 2.2 Existing runtime Control endpoint

The current native runtime already owns the production endpoint:

```text
\\.\pipe\ClawHUD.Control.<sessionId>
```

where `<sessionId>` is the Windows session ID of the ClawHUD runtime.

The server is:

- local only;
- current-user protected by DACL;
- same-session checked;
- `PIPE_TYPE_MESSAGE` / message-mode;
- remote clients rejected;
- one server pipe instance;
- one request / one response per client connection.

The WPF client must adapt to the existing server contract. Do **not** change the server to make the client easier to implement.

### 2.3 Existing transport lifetime is one request per connection

`RuntimeControlPipeServer::ServeClient()` currently performs:

```text
accept client
  -> read exactly one request message
  -> decode
  -> dispatch on ClawHUD main thread
  -> encode one response
  -> write one response
  -> wait for client to consume response and close
  -> disconnect
```

Therefore the WPF client must use:

```text
open pipe
  -> send one request
  -> read one response
  -> close/dispose pipe
```

for each operation.

Do not create a persistent long-lived RPC connection in PR2.

---

## 3. Hard scope boundary

### 3.1 In scope

PR2 may implement:

1. C# protocol-v1 constants/enums/DTOs required by the WPF client;
2. strict little-endian request encoding for the two read operations;
3. strict protocol-v1 response decoding;
4. current-session pipe-name resolution;
5. asynchronous one-request-per-connection Named Pipe transport;
6. bounded connection/read/write behavior;
7. `GetRuntimeInfoAsync()`;
8. `GetSettingsSnapshotAsync()`;
9. basic typed client error/result mapping;
10. a small read-only `MainViewModel` or equivalent projection object;
11. one-time runtime load when the WPF window opens;
12. binding the existing visual controls to the actual runtime snapshot;
13. runtime version in the title;
14. automated C# protocol/client tests;
15. CI execution of those tests.

### 3.2 Explicitly out of scope

Do **not** implement any of the following in PR2:

- `SetStartWithWindows` sending;
- `SetHudEnabled` sending;
- `SetHudVisibilityMode` sending;
- `SetHudSizeOffset` sending;
- `SetHudFont` sending;
- `SetHudAlignment` sending;
- `SetHudBackgroundMode` sending;
- `PreviewHudOpacity` sending;
- `CommitHudOpacity` sending;
- `SetIntelVrrRangeFixEnabled` sending;
- `RequestShutdown` sending from the Settings UI;
- click/touch handlers that mutate the runtime;
- optimistic local Settings state;
- writing `settings.ini`;
- reading `settings.ini` directly;
- reading native Settings store files;
- tray launch integration;
- duplicate Settings-process handling;
- bring-existing-window-to-front logic;
- production cutover from Win32 Settings;
- VeloPack release staging changes;
- `.NET Desktop Runtime` prerequisite changes;
- `Build-Release.yml` changes;
- `CMakeLists.txt` changes for the WPF frontend;
- native Control protocol changes;
- native pipe-server changes;
- `IRuntimeControl` changes;
- `App` changes;
- `HudController` changes;
- `HudSettingsStore` changes;
- telemetry/game-detection/PresentMon/EC changes;
- UI localization;
- About UI;
- additional pages/tabs/navigation;
- a new polling loop or runtime event channel.

If a read-only client appears to require changing the native Control server or HUD/runtime design, stop and diagnose the client implementation instead.

---

## 4. HUD / VRR safety contract — zero-touch requirement

PR2 remains a frontend/IPC-client change only.

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
- Presentation API / DirectComposition production path;
- premultiplied-alpha behavior.

Expected production HUD presentation/rendering diff: **zero**.

---

## 5. Protocol v1 is already authoritative

Do not redesign the protocol in C#.

The C# client must implement the existing contract from:

```text
src/shared/ClawHudControlProtocol.h
src/shared/ClawHudControlCodec.h
src/shared/ClawHudControlCodec.cpp
```

The historical implementation work order can be used as byte-level reference:

```text
docs/work-orders/managed/CH_RTF_4_CONTROL_IPC_WIRE_PROTOCOL_AND_CODEC_WORK_ORDER.md
```

The current source code is authoritative if documentation and source ever differ.

### 5.1 Fixed frame header

Protocol v1 header is exactly 24 bytes:

```text
Offset  Size  Field
0       4     magic = ASCII "CHUD"
4       2     protocolVersion
6       2     headerSize
8       2     messageKind
10      2     operation
12      4     requestId
16      4     status
20      4     payloadSize
```

Constants:

```text
Magic                  = CHUD
ProtocolVersion         = 1
HeaderSize              = 24
MaxPayloadBytes         = 16 KiB
MaxFrameBytes           = 24 + 16 KiB
MaxStringBytes          = 4096
```

All multi-byte integer values are little-endian.

Do not marshal C# structs directly and do not depend on runtime object layout.

### 5.2 Message kinds

```text
Request  = 1
Response = 2
```

### 5.3 Control statuses

Mirror the existing explicit values:

```text
Ok                  = 0
InvalidFrame        = 1
UnsupportedVersion  = 2
UnknownOperation    = 3
InvalidPayload      = 4
InvalidValue        = 5
RuntimeUnavailable  = 6
OperationFailed     = 7
ShuttingDown        = 8
```

Do not expose raw Win32 exceptions, HRESULTs, or pipe error codes as protocol statuses.

Internal transport errors can be represented separately in the C# client result type.

### 5.4 Operations

Mirror all existing operation IDs in the C# protocol declarations so the client has one complete v1 operation table:

```text
GetRuntimeInfo               = 1
GetSettingsSnapshot          = 2
SetStartWithWindows         = 10
SetHudEnabled               = 11
SetHudVisibilityMode        = 12
SetHudSizeOffset            = 13
SetHudFont                  = 14
SetHudAlignment             = 15
SetHudBackgroundMode        = 16
PreviewHudOpacity           = 17
CommitHudOpacity            = 18
SetIntelVrrRangeFixEnabled  = 19
RequestShutdown             = 20
```

However, PR2 public client methods must only send:

```text
GetRuntimeInfo
GetSettingsSnapshot
```

Do not pre-implement hidden mutation methods just because their IDs are known.

### 5.5 Wire enums

Mirror explicit v1 values, not native C++ enum ordinals:

```text
Visibility
  Always      = 1
  InGameOnly  = 2

Alignment
  Left    = 1
  Center  = 2
  Right   = 3

Font
  Unispace         = 1
  SegoeUiVariable  = 2

BackgroundMode
  FullWidth     = 1
  ContentWidth  = 2

IntelVrrStatus
  Disabled            = 1
  Unavailable         = 2
  UnsupportedPanel    = 3
  AmbiguousDisplay    = 4
  AlreadyCorrect      = 5
  SkippedUserProfile  = 6
  Applied             = 7
  ApplyFailed         = 8
  VerificationFailed  = 9

LaunchMode
  Standalone = 1
  Managed    = 2

RuntimeState
  Starting      = 1
  Ready         = 2
  ShuttingDown  = 3
```

### 5.6 Product bounds

Keep the C# mirror explicit:

```text
HUD size offset: -2 .. +2
Opacity percent: 50 .. 100
Opacity step:    5
```

Even though PR2 does not send mutation values, decoded snapshots must still be validated against these bounds.

---

## 6. Recommended C# source layout

Keep the frontend small.

Recommended additions:

```text
src/ClawHUD.Settings/
  Protocol/
    ControlProtocol.cs
    ControlCodec.cs
  Services/
    RuntimeControlClient.cs
  ViewModels/
    MainViewModel.cs
```

Do not create a generic networking framework or shared application framework.

The protocol/client code belongs to the standalone frontend and must not reference:

```text
HudController
HudPresentation
HudSettingsStore
App native types
settings.ini
telemetry types
game detection types
```

No P/Invoke should be necessary merely to obtain the session ID or use Named Pipes.

---

## 7. C# protocol DTO shape

The exact naming may vary, but keep a small typed model equivalent to:

```csharp
internal enum ControlOperation : ushort { ... }
internal enum ControlStatus : uint { ... }
internal enum WireVisibilityMode : byte { ... }
internal enum WireAlignment : byte { ... }
internal enum WireFont : byte { ... }
internal enum WireBackgroundMode : byte { ... }
internal enum WireIntelVrrStatus : byte { ... }
internal enum WireLaunchMode : byte { ... }
internal enum WireRuntimeState : byte { ... }

internal sealed record RuntimeInfo(
    string ApplicationVersion,
    ushort MinimumProtocolVersion,
    ushort MaximumProtocolVersion,
    WireLaunchMode LaunchMode,
    WireRuntimeState RuntimeState);

internal sealed record IntelVrrResult(...);

internal sealed record SettingsSnapshot(
    bool StartWithWindows,
    bool HudEnabled,
    int HudSizeOffset,
    WireFont HudFont,
    WireVisibilityMode VisibilityMode,
    WireAlignment Alignment,
    WireBackgroundMode BackgroundMode,
    ushort BackgroundOpacityPercent,
    bool IntelVrrRangeFixEnabled,
    IntelVrrResult? IntelVrrLastResult);
```

These are frontend wire/domain projection types only.

Do not attempt to share binary/native C++ type definitions with the C# compiler.

---

## 8. Request encoding required in PR2

Only the two read operations are sent in this PR, and both have an empty payload.

A valid request must encode:

```text
magic           CHUD
version         1
headerSize      24
kind            Request (1)
operation       GetRuntimeInfo (1) OR GetSettingsSnapshot (2)
requestId       non-zero client-generated u32
status          0
payloadSize     0
payload         none
```

A helper may conceptually be:

```csharp
byte[] EncodeReadRequest(ControlOperation operation, uint requestId)
```

Requirements:

- reject `requestId == 0`;
- reject any operation other than `GetRuntimeInfo` / `GetSettingsSnapshot` from this PR2 encoder;
- use `BinaryPrimitives.WriteUInt16LittleEndian` / `WriteUInt32LittleEndian` or equivalent explicit little-endian code;
- produce exactly 24 bytes;
- do not use `BinaryFormatter`, JSON, protobuf, MessagePack, unsafe struct casts, or raw marshaling.

Mutation request payload encoders are deferred to the PR that actually uses them.

---

## 9. Response decoding required in PR2

The decoder must be strict because the C# frontend is an independent protocol implementation.

### 9.1 Header validation

Validate in a bounded manner:

1. at least 24 header bytes;
2. magic exactly `CHUD`;
3. `protocolVersion == 1`;
4. `headerSize == 24`;
5. `messageKind == Response`;
6. `requestId != 0`;
7. status value is a known v1 status;
8. `payloadSize <= 16 KiB`;
9. total message length exactly equals `24 + payloadSize`;
10. operation ID is known if an `Ok` response requires a typed payload.

Do not accept trailing bytes.

Do not allocate based on an unvalidated unbounded payload length.

### 9.2 Error responses

For any non-`Ok` protocol status:

- accept the correlated response only if the fixed header/frame itself is valid;
- require the v1 error payload to be empty;
- surface the typed `ControlStatus` to `RuntimeControlClient`;
- do not attempt to decode a settings/runtime-info payload;
- do not turn a protocol error into a successful placeholder snapshot.

### 9.3 `GetRuntimeInfo` payload

Decode exactly:

```text
applicationVersion      u16 byte-length + UTF-8 bytes
minimumProtocolVersion  u16
maximumProtocolVersion  u16
launchMode              u8
runtimeState            u8
```

Reject:

- malformed/truncated strings;
- strings over 4096 bytes;
- malformed UTF-8;
- embedded NUL;
- invalid launch-mode value;
- invalid runtime-state value;
- trailing payload bytes.

Use strict UTF-8 decoding, e.g. a `UTF8Encoding` configured to throw on malformed byte sequences.

### 9.4 Settings snapshot payload

Decode exactly:

```text
startWithWindows             bool byte
hudEnabled                   bool byte
hudSizeOffset                i32 LE
hudFont                      u8
visibilityMode               u8
alignment                    u8
backgroundMode               u8
backgroundOpacityPercent     u16 LE
intelVrrRangeFixEnabled      bool byte
hasIntelVrrLastResult        bool byte
```

Validate:

- bool bytes are only `0` or `1`;
- size offset is `-2..+2`;
- enum values are in the explicit v1 ranges;
- opacity is `50..100` and a multiple of `5`.

If `hasIntelVrrLastResult == 1`, decode:

```text
intelVrrStatus  u8
panelName       wire string
rangeBefore     wire string
rangeAfter      wire string
message         wire string
timestampUtc    wire string
```

If `hasIntelVrrLastResult == 0`, no result bytes may follow.

Reject any trailing bytes after the expected payload.

### 9.5 Future mutation responses

It is acceptable for the response decoder to recognize that the existing protocol uses the same `SettingsSnapshot` payload shape for successful mutation responses, because that is part of protocol v1.

However PR2 must not expose or call mutation APIs.

Do not let this become an excuse to implement all mutation request encoders in advance.

---

## 10. Request correlation

Every client request must use a non-zero request ID.

The received response must match both:

```text
response.requestId == request.requestId
response.operation == request.operation
```

Mismatch means the client call fails.

Do not silently accept a response for another operation/request.

A small monotonically increasing per-process `uint` request ID is sufficient.

Handle wraparound so zero is skipped.

Do not add a complex concurrent request registry: the server itself is one-request-per-connection and PR2 performs simple sequential startup reads.

---

## 11. Named Pipe client behavior

Use `System.IO.Pipes.NamedPipeClientStream`.

### 11.1 Endpoint

Resolve the current Windows session with managed APIs, for example:

```csharp
Process.GetCurrentProcess().SessionId
```

Then connect to:

```text
serverName = "."
pipeName   = "ClawHUD.Control.<sessionId>"
```

Do not include `\\.\pipe\` inside the `pipeName` parameter when using `NamedPipeClientStream`.

Do not scan processes to discover the pipe.

Do not use a global fixed pipe name without the session suffix.

### 11.2 One connection per request

`RuntimeControlClient` must conceptually do:

```csharp
await using var pipe = new NamedPipeClientStream(...);
await pipe.ConnectAsync(...);
pipe.ReadMode = PipeTransmissionMode.Message;
await WriteRequestAsync(...);
var response = await ReadResponseAsync(...);
return ValidateCorrelationAndDecode(...);
// dispose closes the client endpoint
```

The client must close/dispose the connection after consuming each response because the native server waits for the client to consume/close before recycling its single instance.

Do not keep a connected pipe field alive between calls.

### 11.3 Bounded waits

No UI operation may wait forever on a dead/missing pipe.

Provide bounded async behavior using cancellation/timeout.

A simple local IPC budget in the low-seconds range is appropriate; keep it centralized rather than scattering arbitrary timeout values.

Requirements:

- connect is bounded;
- write is cancellable/bounded;
- response read is cancellable/bounded;
- cancellation/timeout becomes a typed transport failure;
- ordinary pipe absence does not crash the WPF process.

Do not add retries or a reconnect state machine in PR2.

The runtime is local. One bounded attempt per explicit read is sufficient.

### 11.4 Bounded response read

Do not allocate an arbitrary frame from a wire-controlled length.

Preferred sequence:

```text
read exactly 24 bytes header
  -> validate fixed fields and payloadSize bound
  -> allocate at most MaxPayloadBytes
  -> read exactly payloadSize bytes
  -> confirm message is complete / no trailing bytes
  -> decode payload
```

`ReadAsync` may return partial data. Implement a small `ReadExactAsync` loop rather than assuming one read fills the buffer.

Because the server uses a message-mode pipe, confirm the response message has ended after the declared frame length. A message containing extra bytes must be rejected rather than silently leaving them unread.

---

## 12. Public client surface for PR2

Keep the service small.

Recommended shape:

```csharp
internal sealed class RuntimeControlClient
{
    Task<ControlClientResult<RuntimeInfo>> GetRuntimeInfoAsync(
        CancellationToken cancellationToken = default);

    Task<ControlClientResult<SettingsSnapshot>> GetSettingsSnapshotAsync(
        CancellationToken cancellationToken = default);
}
```

The result type should distinguish at least:

```text
Success
Protocol status error
Transport unavailable/error
Malformed response
Timeout/cancel
```

Exact type naming is flexible.

Do not expose `IOException`, `Win32Exception`, or raw pipe handles to `MainWindow`.

Do not show exception detail in the normal UI.

---

## 13. Startup/read-only loading sequence

When `MainWindow` opens, perform one bounded asynchronous load.

Recommended sequence:

```text
Window Loaded
  -> GetRuntimeInfoAsync()
  -> validate protocol compatibility
  -> GetSettingsSnapshotAsync()
  -> build read-only MainViewModel
  -> assign/project snapshot to existing controls
```

Use sequential calls. There is only one native pipe instance and there is no benefit in racing two startup reads.

Do not block the WPF UI thread with `.Result`, `.Wait()`, synchronous pipe waits, or `Task.Run` wrappers around blocking code.

Use normal async WPF event handling.

### 13.1 Protocol compatibility check

After a successful `GetRuntimeInfo`, require protocol v1 to fall within:

```text
minimumProtocolVersion <= 1 <= maximumProtocolVersion
```

If not compatible, do not proceed as though the snapshot contract is valid.

This is a client compatibility failure, not a reason to modify the native runtime protocol.

### 13.2 Runtime state

Decode and expose `Starting / Ready / ShuttingDown` faithfully.

Do not invent additional runtime states.

PR2 does not need a retry loop waiting for `Ready`.

---

## 14. Read-only ViewModel projection

Add a small ViewModel/projection object for the actual snapshot.

Do not add a third-party MVVM framework.

Because PR2 performs a one-time load and keeps controls read-only, the implementation can remain simple.

Recommended projected properties include:

```text
HudEnabled
IsVisibilityAlways
IsVisibilityInGameOnly
HudSizeLabel
IsFontUnispace
IsFontSegoeUiVariable
IsAlignmentLeft
IsAlignmentCenter
IsAlignmentRight
IsBackgroundFullWidth
IsBackgroundContentWidth
BackgroundOpacityPercent
IntelVrrRangeFixEnabled
StartWithWindows
```

`HudSizeLabel` may display:

```text
-2
-1
Default
+1
+2
```

based on the runtime snapshot.

Do not create setter commands in PR2.

Do not make the ViewModel an alternate source of truth.

It is only a projection of the runtime snapshot.

---

## 15. MainWindow binding changes

Replace PR1 representative values with one-way bindings to the read-only projection.

Examples conceptually:

```xml
<ToggleButton IsChecked="{Binding HudEnabled, Mode=OneWay}" ... />
<ToggleButton IsChecked="{Binding IsVisibilityAlways, Mode=OneWay}" ... />
<TextBlock Text="{Binding HudSizeLabel}" ... />
<Slider Value="{Binding BackgroundOpacityPercent, Mode=OneWay}" ... />
```

Keep:

```text
IsHitTestVisible = false
```

for the setting cards/controls in PR2.

The page must display live state but remain incapable of mutating it.

Do not enable one control early just because the client exists.

### 15.1 Title

After `GetRuntimeInfo` succeeds:

```text
Title = "ClawHUD " + ApplicationVersion
```

The version must come from the ClawHUD runtime's `GetRuntimeInfo.applicationVersion`.

Do not use the Settings assembly version as the normal displayed app version.

---

## 16. Runtime-unavailable behavior in PR2

PR2 is still not the production Settings path, so keep failure UX restrained.

Required behavior:

- no crash;
- no infinite wait;
- no fake runtime snapshot;
- no settings mutation;
- controls remain non-interactive;
- title may remain simply `ClawHUD` when runtime info is unavailable.

Do not add a large error page, retry UI, reconnect loop, polling timer, or modal exception dump in this PR.

A later production-cutover PR can finalize user-facing runtime-unavailable behavior if needed.

---

## 17. No polling / no event channel in PR2

The current Control protocol is request/response only.

Do not add:

- periodic `GetSettingsSnapshot` polling;
- a timer;
- a background reconnect loop;
- native runtime push notifications;
- a second IPC channel.

PR2 needs only the initial snapshot load.

The final separate frontend may later refresh on activation/focus or through another explicitly designed mechanism, but that is not part of this foundation PR.

---

## 18. Test strategy

Cross-language byte compatibility is the main risk in PR2, so protocol tests are mandatory.

Add a small C# test project under a clear test path, for example:

```text
tests/ClawHUD.Settings.Tests/
  ClawHUD.Settings.Tests.csproj
  ControlCodecTests.cs
  RuntimeControlClientTests.cs
```

A Microsoft test stack or another lightweight test-only framework is acceptable.

Rules:

- test dependencies must remain test-only;
- do not add a production UI/MVVM/network package merely for testing;
- the framework-dependent Settings publish payload must not gain test assemblies/packages.

### 18.1 Golden request frames

Assert exact request bytes for both read operations.

For example, a `GetSettingsSnapshot` request with a known request ID must be exactly 24 bytes and contain:

```text
43 48 55 44             # CHUD
01 00                   # protocol v1
18 00                   # header size 24
01 00                   # Request
02 00                   # GetSettingsSnapshot
<requestId u32 LE>
00 00 00 00             # status = 0
00 00 00 00             # payload size = 0
```

Do not test only C# encode->C# decode round trips. Golden byte tests are needed so two identical C# mistakes cannot validate each other.

### 18.2 Runtime info response decode

Use a known protocol-v1 byte fixture and verify:

- UTF-8 version string;
- min/max version;
- launch mode;
- runtime state;
- request/operation correlation.

### 18.3 Settings snapshot response decode

Cover at least:

1. snapshot without Intel VRR result;
2. snapshot with Intel VRR result;
3. multi-byte UTF-8 in a VRR string;
4. each enum boundary/value family used by the page;
5. opacity 50 and 100 plus a middle 5% step;
6. size `-2`, `0`, `+2`.

### 18.4 Malformed frame rejection

Cover at least:

- empty/truncated header;
- wrong magic;
- wrong protocol version;
- wrong header size;
- wrong message kind;
- request ID zero;
- unknown status;
- payload over 16 KiB;
- declared payload larger than actual;
- declared payload smaller than actual / trailing bytes;
- invalid bool byte (`2`);
- invalid wire enum;
- invalid HUD size;
- invalid opacity / non-5% step;
- malformed UTF-8;
- embedded NUL;
- string length beyond available payload;
- trailing bytes after a valid typed payload.

### 18.5 Client transport test

Add at least one local Named Pipe integration test using a unique test pipe name / injectable endpoint.

Verify:

```text
client connects
  -> writes one valid request
  -> receives one known response
  -> returns typed data
  -> closes the connection
```

The production pipe name remains session-derived. An internal test-only constructor/factory that accepts a pipe name is acceptable if it does not leak into normal UI behavior.

Also verify that a missing pipe returns a bounded transport failure rather than hanging indefinitely.

Do not require a real running ClawHUD process for automated unit tests.

---

## 19. CI changes

`Build-Test.yml` already sets up .NET 10 and builds the WPF project from PR1.

Extend it narrowly to run the new C# tests.

Conceptually:

```yaml
- name: Test WPF Settings
  shell: pwsh
  run: dotnet test tests/ClawHUD.Settings.Tests/ClawHUD.Settings.Tests.csproj --configuration Release
```

Avoid redundant restore/build work where practical, but keep CI easy to understand.

Do not remove or weaken the existing native steps:

```text
CMake configure
native Release build
CTest
```

Both frontend tests and native regression tests must pass.

Do not touch `Build-Release.yml` in PR2.

---

## 20. Files expected to change

Expected new files, approximately:

```text
src/ClawHUD.Settings/Protocol/ControlProtocol.cs
src/ClawHUD.Settings/Protocol/ControlCodec.cs
src/ClawHUD.Settings/Services/RuntimeControlClient.cs
src/ClawHUD.Settings/ViewModels/MainViewModel.cs

tests/ClawHUD.Settings.Tests/ClawHUD.Settings.Tests.csproj
tests/ClawHUD.Settings.Tests/ControlCodecTests.cs
tests/ClawHUD.Settings.Tests/RuntimeControlClientTests.cs
```

Expected modified files:

```text
src/ClawHUD.Settings/MainWindow.xaml
src/ClawHUD.Settings/MainWindow.xaml.cs
.github/workflows/Build-Test.yml
```

A smaller equivalent layout is acceptable.

Expected native runtime source diff:

```text
none
```

Expected release workflow diff:

```text
none
```

---

## 21. PR size discipline

Keep PR2 reviewable.

Target roughly `<= 500` meaningful implementation LOC where practical, excluding straightforward tests/fixtures when necessary.

If full mutation request encoding pushes the PR beyond this boundary, that is evidence it does not belong here — it is already explicitly deferred.

Do not create abstraction layers merely to reduce apparent line count.

---

## 22. Manual verification

In addition to automated tests, perform these smoke checks on Windows when possible.

### 22.1 No-runtime launch

With `ClawHUD.exe` not running:

1. manually launch `ClawHUD.Settings.exe`;
2. it must not hang indefinitely;
3. it must not crash;
4. it must not start ClawHUD itself;
5. controls remain non-interactive;
6. close the window;
7. Settings process exits completely.

### 22.2 Runtime-connected launch

With `ClawHUD.exe` running:

1. manually launch `ClawHUD.Settings.exe`;
2. title changes to `ClawHUD <runtime version>`;
3. Enable HUD matches current runtime state;
4. Display mode matches runtime state;
5. HUD size matches runtime state;
6. Font matches runtime state;
7. Alignment matches runtime state;
8. Background width matches runtime state;
9. Background opacity matches runtime state;
10. Intel VRR Range Fix matches runtime state;
11. Start with Windows matches runtime state;
12. clicking/touching the visible controls does **nothing** in PR2;
13. close the window;
14. WPF process exits while ClawHUD runtime remains running.

### 22.3 Snapshot proof

Before opening WPF Settings, change at least two values through the still-production Win32 Settings window.

Then manually launch the WPF shell and verify those values are projected correctly.

This proves PR2 is reading runtime authority rather than displaying hard-coded defaults.

### 22.4 Production regression

Confirm:

- launching ClawHUD does not auto-launch WPF Settings;
- tray Settings still opens the existing Win32 Settings frontend;
- tray Exit behavior is unchanged;
- no WPF process exists during normal ClawHUD idle operation;
- existing HUD behavior remains unchanged.

---

## 23. Acceptance criteria

PR2 is complete only when all are true:

### Architecture

- [ ] C# frontend implements protocol v1 independently from native memory layout.
- [ ] No native runtime/server/protocol change is required.
- [ ] `RuntimeControlClient` uses current-session Named Pipe naming.
- [ ] One request uses one connection and closes after its response.
- [ ] All waits are bounded/cancellable.
- [ ] No polling/retry state machine was added.

### Protocol

- [ ] `GetRuntimeInfo` request encodes correctly.
- [ ] `GetSettingsSnapshot` request encodes correctly.
- [ ] Response header is strictly validated.
- [ ] Runtime-info payload is strictly decoded.
- [ ] Settings snapshot payload is strictly decoded.
- [ ] Intel VRR optional result payload is strictly decoded.
- [ ] malformed UTF-8 / invalid enum / invalid bounds / trailing bytes are rejected.
- [ ] response request ID and operation must match the request.

### UI

- [ ] title displays the runtime's application version when connected.
- [ ] all five cards display the runtime snapshot.
- [ ] all setting controls remain non-interactive.
- [ ] no layout/card/scroll redesign was introduced.
- [ ] no About/localization/navigation was added.

### Process

- [ ] Settings is still not launched with ClawHUD startup.
- [ ] Settings Close/Alt+F4 still exits the WPF process.
- [ ] closing Settings does not terminate ClawHUD.
- [ ] missing runtime does not hang or crash the WPF process.

### Regression

- [ ] existing Win32 Settings remains the production tray Settings target.
- [ ] existing tray Exit remains unchanged.
- [ ] native CMake build passes.
- [ ] native CTest suite passes.
- [ ] WPF Release build passes.
- [ ] new C# tests pass.
- [ ] HUD presentation/VRR contract files are untouched.
- [ ] `Build-Release.yml` is untouched.

---

## 24. Explicit stop point

When PR2 is complete, **stop**.

Do not continue by making the controls interactive.

The expected product state after merge is intentionally:

```text
production user path
  Tray -> existing Win32 Settings

new WPF frontend
  manually launchable
  connects to Control IPC
  displays real runtime version/state
  remains read-only
```

That is the correct handoff point for PR3.

PR3 will build on this proven read-only boundary and introduce the first HUD setting mutation path while preserving the rule that every successful mutation is followed by projection of the runtime's authoritative returned snapshot.
