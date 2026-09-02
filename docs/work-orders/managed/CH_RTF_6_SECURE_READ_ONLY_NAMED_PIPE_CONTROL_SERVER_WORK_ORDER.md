# CH-RTF-6 — Secure Read-Only Named Pipe Control Server Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** ClawHUD Runtime / Frontend Separation  
> **Architecture source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md`  
> **PR plan source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_PR_PLAN_2026-09-02.md`  
> **Previous PRs:** #209 CH-RTF-1, #210 CH-RTF-2, #211 CH-RTF-3, #212 CH-RTF-4, #213 CH-RTF-5  
> **Analyzed main HEAD:** `122dda38a75118267840634250b1f3bff6a9796f`  
> **Scope:** Add the first externally reachable ClawHUD Control endpoint: a secure, local, current-user/session-scoped, read-only Windows Named Pipe server exposing only `GetRuntimeInfo` and `GetSettingsSnapshot`  
> **Status:** Ready for implementation

---

## 1. Objective

CH-RTF-6 is the first PR in this series that exposes the Control protocol outside the ClawHUD process.

The required production flow is:

```text
independently installed compatible frontend
    |
    | local Windows Named Pipe
    | one CHUD protocol request message
    v
RuntimeControlPipeServer worker
    |
    | DecodeControlRequest()
    | read-only operation gate
    v
RuntimeControlDispatchBridge::Dispatch()
    |
    | PostMessage(RuntimeMessageWindow)
    v
ClawHUD main thread
    |
    | ExecuteRuntimeControlRequest()
    v
IRuntimeControl / App
    |
    | authoritative response
    v
RuntimeControlDispatchBridge
    |
    | EncodeControlResponse()
    v
Named Pipe response message
```

This PR must make these two operations externally usable:

```text
GetRuntimeInfo
GetSettingsSnapshot
```

This PR must **not** enable external mutation.

The following operations already exist in protocol v1 but must remain unavailable through the pipe in CH-RTF-6:

```text
SetStartWithWindows
SetHudEnabled
SetHudVisibilityMode
SetHudSizeOffset
SetHudFont
SetHudAlignment
SetHudBackgroundMode
PreviewHudOpacity
CommitHudOpacity
SetIntelVrrRangeFixEnabled
RequestShutdown
```

The key result is:

> A separately installed process in the same Windows user session can deterministically discover a running ClawHUD, query runtime compatibility, and obtain the authoritative settings snapshot without touching `settings.ini`, `HudController`, or any runtime object directly.

---

## 2. Current production baseline after PR #213

Main now contains the complete in-process path required by the server.

### 2.1 Protocol / codec

CH-RTF-4 provides:

```text
src/shared/ClawHudControlProtocol.h
src/shared/ClawHudControlCodec.h
src/shared/ClawHudControlCodec.cpp
```

Important current protocol properties:

```text
magic                    "CHUD"
protocol version         1
fixed frame header       24 bytes
maximum payload          16 KiB
maximum frame            header + payload
wire strings             UTF-8, u16 length, max 4096 bytes
```

The codec already owns:

```text
frame validation
magic/version validation
requestId validation
operation validation
payload/value validation
wire response encoding
correlated error identity after a trustworthy header
```

Do not create a second protocol parser inside the pipe server.

### 2.2 Main-thread dispatch bridge

CH-RTF-5 provides:

```text
RuntimeControlDispatchBridge
RuntimeControlWireMapping
```

The bridge already guarantees:

```text
background producer
 -> queue
 -> RuntimeMessageWindow wake
 -> main-thread handler
 -> authoritative ControlResponse
 -> waiting producer released
```

It also already guarantees:

```text
Stop()
 -> no new requests accepted
 -> pending waiters completed as ShuttingDown

PostMessage wake failure
 -> RuntimeUnavailable

main-thread self dispatch
 -> synchronous, no deadlock
```

The Named Pipe worker added in this PR must use this bridge.

It must **never** call `IRuntimeControl`, `App` setting methods, `HudController`, `SettingsWindow`, `GameSessionController`, or HUD presentation directly.

### 2.3 Current runtime metadata

Until CH-RTF-8 introduces real launch modes, `GetRuntimeInfo` truthfully reports:

```text
launchMode   = Standalone
runtimeState = Ready
protocol     = 1..1
```

Do not add `--managed` in this PR.

---

## 3. Required source shape

Add a small Windows-specific transport component, recommended:

```text
src/ClawHUD/RuntimeControlPipeServer.h
src/ClawHUD/RuntimeControlPipeServer.cpp
```

A focused endpoint/security helper is acceptable if it materially simplifies testing, for example:

```text
src/ClawHUD/RuntimeControlPipeEndpoint.h/.cpp
```

but do not create a generic IPC framework.

Add a focused Windows test target, recommended:

```text
tests/RuntimeControlPipeServerTests.cpp
```

and register it in:

```text
cmake/ClawHUDTests.cmake
```

### Do not add

```text
generic RPC framework
JSON transport
HTTP server
TCP socket
COM server
gRPC
shared-memory command channel
service locator
event bus
plugin abstraction
separate runtime executable
```

This is one local Named Pipe server for the already-defined CHUD protocol.

---

## 4. Stable discoverable pipe endpoint

The endpoint must be derivable independently by ClawHUD and a separately installed frontend.

Use a deterministic per-session pipe name:

```text
\\.\pipe\ClawHUD.Control.<sessionId>
```

Example only:

```text
\\.\pipe\ClawHUD.Control.1
```

where `<sessionId>` is the Windows session ID of the current ClawHUD process.

Resolve it with the current process, conceptually:

```cpp
DWORD sessionId{};
if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId))
    return failure;
```

Then construct:

```cpp
L"\\\\.\\pipe\\ClawHUD.Control." + std::to_wstring(sessionId)
```

### Required endpoint rules

1. The name is deterministic and documented.
2. Do **not** generate a random pipe name.
3. Do **not** store the pipe name in a temporary file, registry rendezvous key, HWND property, or tray state.
4. Do **not** include the ClawHUD PID in the endpoint name.
5. Do **not** include protocol version in the endpoint name; protocol negotiation/version rejection belongs in the CHUD frame contract.
6. If session-ID discovery fails, server startup fails. Do not fall back to an unscoped global name.

The same deterministic naming rule will later be implemented by SteamAddon.

---

## 5. Security contract — mandatory

The Control pipe is a local control surface. Treat its security boundary as part of the product contract.

CH-RTF-6 must enforce all three layers below:

```text
1. local machine only
2. current Windows user only
3. same Windows session only
```

### 5.1 Reject remote clients

Create the pipe with:

```text
PIPE_REJECT_REMOTE_CLIENTS
```

Do not rely only on firewall/network assumptions.

### 5.2 Current-user-only DACL

Create an explicit protected security descriptor granting pipe access to the current process user's SID only.

Resolve the current user SID from the process token, for example through:

```text
OpenProcessToken
GetTokenInformation(TokenUser)
ConvertSidToStringSidW
```

Build a protected DACL equivalent to:

```text
D:P(A;;GA;;;<current-user-sid>)
```

Use that descriptor through `SECURITY_ATTRIBUTES` when creating the pipe.

Required rules:

- do not grant `Everyone`;
- do not grant `Authenticated Users`;
- do not grant `BUILTIN\Users`;
- do not use a null DACL;
- do not use default inherited pipe security as the intended policy;
- do not broaden access merely to make tests easier.

Use RAII for any token handles, `LocalAlloc`/`LocalFree` security descriptor memory, and pipe handles.

`advapi32` is already linked by the main target; only add another link dependency if the actual implementation requires it.

### 5.3 Same-session validation after connect

A user SID can exist in more than one interactive Windows session. Therefore the DACL alone does not complete the session-scope requirement.

After `ConnectNamedPipe` succeeds and before reading a request:

```text
GetNamedPipeClientProcessId
 -> client PID
ProcessIdToSessionId(client PID)
 -> client session ID
compare with server session ID
```

If the client session differs, or client PID/session discovery cannot be validated:

```text
disconnect
no request decode
no dispatch
no response
```

Do not impersonate the client and do not expose any helper/EC privilege through this pipe.

---

## 6. Pipe mode and framing contract

Use Windows Named Pipe **message mode**.

Recommended pipe mode:

```text
PIPE_TYPE_MESSAGE
PIPE_READMODE_MESSAGE
PIPE_WAIT
PIPE_REJECT_REMOTE_CLIENTS
```

Recommended open mode:

```text
PIPE_ACCESS_DUPLEX
```

A first-instance guard is recommended:

```text
FILE_FLAG_FIRST_PIPE_INSTANCE
```

The existing ClawHUD single-instance mutex remains the primary runtime-instance rule. `FILE_FLAG_FIRST_PIPE_INSTANCE` is additional protection against silently attaching to an unexpected pre-existing endpoint.

Use one pipe instance at a time in CH-RTF-6. There is no need to build a multi-client server pool in this PR.

### 6.1 One request per pipe connection

Keep the initial transport contract intentionally simple:

```text
client connects
 -> exactly one complete CHUD request frame in one pipe message
 -> server returns at most one CHUD response frame in one pipe message
 -> connection ends
```

The next query opens a new pipe connection.

This is acceptable for the low-frequency Settings/status use case and prevents an idle client from becoming a long-lived protocol-session dependency.

Do not add handshake/session state beyond `GetRuntimeInfo`.

### 6.2 Complete frame = one pipe message

A compatible client must send the complete CHUD request using one `WriteFile` / one pipe message.

Do not add another transport-level length prefix around the existing CHUD frame.

The pipe message payload is the CHUD frame itself.

The server response must likewise be one complete encoded CHUD frame in one pipe message.

---

## 7. Bounded input — never allocate from untrusted wire lengths

The transport already knows the absolute protocol maximum:

```text
kMaxFrameBytes
```

Read into a fixed/bounded buffer sized to that maximum.

For example conceptually:

```cpp
std::array<std::uint8_t, control::kMaxFrameBytes> buffer{};
DWORD bytesRead{};
const BOOL ok = ReadFile(pipe, buffer.data(),
    static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
```

Message-mode behavior is important:

- if one message fits, pass exactly `bytesRead` bytes to `DecodeControlRequest`;
- if `ReadFile` reports `ERROR_MORE_DATA`, the message exceeded the maximum frame size: reject it and close the connection;
- never allocate `payloadSize` bytes before codec validation;
- never loop accumulating an arbitrarily large client message;
- zero-byte/incomplete/truncated messages are rejected through the codec/connection policy.

This preserves CH-RTF-4's bounded-frame invariant at the actual transport boundary.

---

## 8. Request decode and protocol-error behavior

For a received bounded message:

```cpp
const auto decoded = control::DecodeControlRequest(frame);
```

### 8.1 Valid request

If `decoded.ok` is true, continue to the read-only operation gate below.

### 8.2 Correlatable protocol error

CH-RTF-4 preserves `FrameIdentity` once the fixed header is trustworthy.

If decoding fails and:

```text
decoded.identity.has_value() == true
```

return a correlated error response using:

```text
raw operationId from identity
requestId from identity
decoded.error as ControlStatus
empty payload
```

Then encode it through the existing:

```cpp
EncodeControlResponse(...)
```

Examples that should receive a correlated error response when identity exists:

```text
unknown operation      -> UnknownOperation
invalid known payload  -> InvalidPayload
invalid known value    -> InvalidValue
```

### 8.3 Uncorrelatable malformed frame

If decode fails before a trustworthy identity exists, for example malformed fixed header / bad magic / unsupported framing state:

```text
close the connection
send no fabricated response
```

Do not invent `requestId = 0` or a guessed operation merely to send an error.

### 8.4 Encode failure

If `EncodeControlResponse()` unexpectedly refuses a response DTO:

```text
log locally
close the connection
send no malformed bytes
continue serving later clients
```

Do not bypass the codec to manually serialize a response.

---

## 9. Read-only operation gate — mandatory

CH-RTF-6 must expose only:

```text
GetRuntimeInfo
GetSettingsSnapshot
```

For exactly these two operations:

```text
pipe worker
 -> RuntimeControlDispatchBridge::Dispatch(request)
 -> wait for main-thread authoritative response
 -> EncodeControlResponse
 -> write response
```

### 9.1 Known mutation operations

For every currently known mutation operation, do **not** call the bridge in CH-RTF-6.

Return a deterministic empty-payload response:

```text
ControlStatus::RuntimeUnavailable
```

using the original operation ID and request ID.

This includes:

```text
SetStartWithWindows
SetHudEnabled
SetHudVisibilityMode
SetHudSizeOffset
SetHudFont
SetHudAlignment
SetHudBackgroundMode
PreviewHudOpacity
CommitHudOpacity
SetIntelVrrRangeFixEnabled
RequestShutdown
```

`RuntimeUnavailable` is intentionally reused as the temporary v1 status for a protocol-known operation that the current external control surface does not yet expose.

Do not add a new protocol status in this PR.

CH-RTF-7 will open the mutation path deliberately.

### 9.2 Important test invariant

For a blocked mutation request, tests must prove:

```text
bridge/dispatch callback invocation count == 0
```

The server must not dispatch the mutation and merely discard the result afterward.

---

## 10. Server threading model

Use one dedicated background worker owned by `RuntimeControlPipeServer`.

The worker owns all blocking pipe operations.

The main/UI thread must never block in:

```text
ConnectNamedPipe
ReadFile
WriteFile
FlushFileBuffers
client wait
```

A reasonable shape is conceptually:

```cpp
class RuntimeControlPipeServer
{
public:
    using DispatchCallback = std::function<control::ControlResponse(
        const control::ControlRequest&)>;

    bool Start(DispatchCallback dispatch);
    void Stop();
    bool Running() const noexcept;
    std::wstring PipeName() const;

private:
    void WorkerMain();
};
```

The exact shape may vary.

Do not pass `App&` into the pipe server.

The production callback should be only:

```cpp
[this](const control::ControlRequest& request)
{
    return runtimeControlBridge_.Dispatch(request);
}
```

The pipe server therefore has no authority to call runtime product APIs itself.

---

## 11. Worker blocking I/O cancellation

`Stop()` must be able to terminate a worker blocked in normal pipe I/O.

Do not depend on a future client connecting merely to let ClawHUD exit.

For a simple synchronous worker implementation, using the worker thread's native Windows handle with:

```text
CancelSynchronousIo
```

is acceptable.

An overlapped-I/O implementation with a stop event is also acceptable if kept focused.

Whichever implementation is chosen, these outcomes are mandatory:

```text
server blocked in ConnectNamedPipe
 + Stop()
 -> worker exits and joins

server blocked waiting for client request bytes
 + Stop()
 -> worker exits and joins

server blocked writing/flushing response
 + Stop()
 -> worker exits and joins
```

Do not add recurring polling timers for cancellation.

Treat `ERROR_OPERATION_ABORTED`, broken-pipe, client-disconnect, and equivalent stop-time errors as normal shutdown/connection lifecycle, not fatal application errors.

---

## 12. Response write / connection completion

For a valid encoded response:

```text
WriteFile one complete response message
```

Do not split one CHUD response across multiple pipe messages.

If the implementation uses `FlushFileBuffers` before disconnect so the client can consume the response, that blocking call must be covered by the same `Stop()` cancellation guarantee above.

Client disconnect before/during response is a normal connection failure:

```text
log at Debug/Warn only if useful
release connection
continue listener loop
```

Do not terminate ClawHUD because a frontend disappears.

---

## 13. Server start lifecycle

CH-RTF-6 remains Standalone-only at the launch-mode level.

The pipe server should start only after:

```text
RuntimeMessageWindow exists
runtime initialization succeeded
RuntimeControlDispatchBridge is accepting
```

Current suitable end-of-Run ordering becomes conceptually:

```text
...
start tweak coordinator
RuntimeControlDispatchBridge::Start(...)
RuntimeControlPipeServer::Start(
    request -> runtimeControlBridge_.Dispatch(request))
ProcessMessages()
```

### 13.1 Startup failure policy in CH-RTF-6

If the read-only pipe server cannot be created:

```text
log Error
continue normal Standalone ClawHUD
```

Do not fail HUD/tray startup in this PR merely because external integration is unavailable.

Reason:

- Standalone ClawHUD behavior predates the pipe;
- Managed mode does not exist yet;
- CH-RTF-8/9 may later make Control IPC readiness mandatory for Managed composition.

Do not fall back to:

```text
weaker ACL
random alternate pipe name
TCP/HTTP
another global pipe name
```

A security/configuration failure means IPC unavailable, not security downgraded.

---

## 14. Shutdown ordering — mandatory

Current `StopRuntimeSources()` begins with:

```text
runtimeControlBridge_.Stop()
```

Keep that first.

Add the pipe server immediately after the bridge stop, conceptually:

```text
StopRuntimeSources()
 -> runtimeControlBridge_.Stop()
 -> runtimeControlPipeServer_.Stop()
 -> cancel resume recovery
 -> stop telemetry/game sources
 -> ...
```

This order is intentional.

### Why bridge first

A pipe worker may currently be inside:

```text
runtimeControlBridge_.Dispatch()
```

waiting for the main thread.

Because shutdown itself is running on the main thread, joining the pipe worker **before** stopping the bridge could deadlock:

```text
main thread
 -> pipeServer.Stop()
 -> waits worker join

worker
 -> bridge.Dispatch()
 -> waits main-thread drain
```

Stopping the bridge first guarantees that such a worker is released with:

```text
ShuttingDown
```

Then pipe-server stop can cancel any remaining pipe I/O and join safely.

### Required shutdown invariants

1. `RuntimeControlDispatchBridge::Stop()` remains before runtime HWND destruction.
2. `RuntimeControlPipeServer::Stop()` occurs before runtime HWND destruction.
3. A pending read-only query cannot keep App shutdown blocked forever.
4. A client connecting during the narrow shutdown transition may receive `ShuttingDown` or lose the connection; it must never cause a deadlock or runtime mutation.
5. Destructors remain idempotent after explicit stop.

---

## 15. One runtime authority / pipe creation

The existing single-instance mutex remains:

```text
Local\ClawHUD.SingleInstance
```

Do not replace or redesign it here.

The pipe server should additionally fail rather than silently share an already-owned endpoint.

A first-instance pipe creation flag is recommended:

```text
FILE_FLAG_FIRST_PIPE_INSTANCE
```

If the deterministic pipe endpoint already exists unexpectedly:

```text
server startup fails
log Error
Standalone runtime continues without Control IPC
```

Do not connect to another process's server and do not generate an alternate endpoint.

---

## 16. Logging

Use existing `RuntimeLogger`.

Log useful lifecycle facts without logging raw request payload content.

Suggested events:

```text
Control pipe server started name=... session=...
Control pipe server unavailable error=...
Control pipe client rejected session mismatch
Control pipe oversized message rejected
Control pipe response encode failed
Control pipe server stopped
```

Do not log:

```text
security descriptor binary blobs
access tokens
raw SID as a repeated per-request field
arbitrary incoming bytes
full Intel VRR result strings on every query
```

Normal client disconnects should not spam Info-level logs.

---

## 17. CMake / link scope

Add only the new server source(s) to the existing `ClawHUD` target.

The server uses existing protocol/codec and dispatch code already linked into `ClawHUD.exe` after CH-RTF-5.

A focused test target may link only what it needs.

Do not add a new EXE or DLL.

Do not alter packaging, Velopack, PresentMon MSI bootstrap, or EC helper packaging.

---

## 18. Required focused tests

Add a real Windows Named Pipe test target rather than testing only an in-memory fake transport.

Tests may use a unique test-only pipe suffix/name so parallel local test runs do not collide. Production endpoint construction must remain the deterministic contract in section 4.

### 18.1 Endpoint derivation

Prove:

```text
same session ID -> same production pipe name
pipe name includes session ID
no random/PID component
```

### 18.2 Same-user local round trip — GetRuntimeInfo

Start the server with a fake dispatch callback.

Client:

```text
connect locally
send one encoded GetRuntimeInfo request as one message
read one response message
DecodeControlResponse
```

Verify:

```text
status == Ok
requestId preserved
runtimeInfo present
```

### 18.3 Same-user local round trip — GetSettingsSnapshot

Verify the server dispatches the request and returns the authoritative encoded snapshot supplied by the fake dispatcher.

### 18.4 Mutation gate

For at least:

```text
SetHudEnabled
CommitHudOpacity
RequestShutdown
```

verify:

```text
response.status == RuntimeUnavailable
dispatch callback count does not increase
no mutation side effect occurs
```

Ideally table-drive all known v1 non-read-only operations.

### 18.5 Unknown operation correlation

Craft a header-valid v1 request with an unknown operation ID and non-zero requestId.

Verify response:

```text
UnknownOperation
raw unknown operationId echoed
requestId echoed
```

### 18.6 Invalid known payload/value correlation

Send a header-valid request with a known operation but malformed payload/value.

Verify the correlated protocol error from the codec is returned, for example:

```text
InvalidPayload
or InvalidValue
```

with original requestId preserved.

### 18.7 Uncorrelatable malformed frame

Send a bad-magic or otherwise pre-identity malformed message.

Verify:

```text
no fabricated CHUD response
connection closes/fails
server remains alive for the next valid client
```

### 18.8 Oversized pipe message

Send one message larger than `kMaxFrameBytes`.

Verify:

```text
message rejected
no unbounded allocation
no dispatch
server accepts a later valid client
```

### 18.9 Stop while waiting for a connection

Start the server with no client.

Call `Stop()`.

Verify the worker terminates and joins; test must not hang waiting for a client to connect.

### 18.10 Stop while client is connected but idle

Connect a client and do not send a complete request.

Call `Stop()`.

Verify the worker terminates and joins.

### 18.11 Dispatch wait + shutdown ordering

Use a real `RuntimeControlDispatchBridge` configured so a worker request is pending/not drained.

Send a read-only request through the pipe.

Then perform the production shutdown ordering in the test:

```text
bridge.Stop()
pipeServer.Stop()
```

Verify:

```text
no deadlock
worker releases
server joins
```

The client may receive `ShuttingDown` if response transmission completes, or connection termination if pipe stop wins; either is acceptable during shutdown.

### 18.12 Restart

After `Stop()`, start a new server on the same test endpoint and prove a valid request still succeeds.

This catches leaked pipe handles / stale ownership.

### 18.13 Security descriptor

Add at least one focused assertion around the constructed pipe security policy.

It must verify that the protected DACL grants the current user and does not intentionally grant broad access such as:

```text
Everyone
Authenticated Users
BUILTIN\Users
```

Do not weaken the production DACL because impersonating another Windows account is awkward in CI.

### 18.14 Session gate helper

If same-session validation is factored into a helper, test at minimum:

```text
serverSession == clientSession -> allow
serverSession != clientSession -> reject
```

The production path must still obtain the real client PID/session from the pipe.

---

## 19. Existing regression suite

Run the full normal Debug test suite used by the preceding PRs.

At minimum:

```text
cmake configure/build Debug
cmake configure/build Release
ctest -C Debug -E DiagWinEventTests
```

The new pipe test target must be included in normal CTest registration.

Do not require hardware or a running game for the pipe tests.

---

## 20. HUD / VRR presentation safety contract — NON-NEGOTIABLE

This PR is a control transport change only.

Do not modify, replace, weaken, or work around:

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
existing Presentation API / DirectComposition production path
premultiplied-alpha presentation contract
```

Do not touch `HudPresentation` to implement IPC.

`GetSettingsSnapshot` is read-only and must not cause presentation recreation or rendering side effects beyond the existing snapshot behavior.

`Background Opacity` remains background-only. No window-wide opacity behavior may be introduced.

Existing tests/assertions for:

```text
click-through
no activation
topmost
transparent hit testing
independent flip
premultiplied alpha
production presentation contract
```

must remain intact.

---

## 21. Explicit non-goals

Do **not** implement any of the following in CH-RTF-6:

```text
external setting mutations
RequestShutdown -> App::Exit
--managed parsing
Managed launch mode
tray suppression
SteamAddon process ownership
Job Object ownership
restart/crash recovery
mode-aware Velopack restart
mode-aware Start with Windows semantics
StateChanged subscriptions
event stream
multiple concurrent pipe clients
persistent multi-request pipe sessions
client library in SteamAddon
WinUI3/WPF/Web frontend migration
settings schema redesign
shared EC helper
PresentMon changes
game detection changes
HUD renderer/presentation changes
```

The target is deliberately narrow:

> **secure external read-only visibility into the existing runtime.**

---

## 22. Acceptance checklist

Before marking the PR complete, verify all of the following.

### Endpoint / security

- [ ] Pipe name is deterministically `\\.\pipe\ClawHUD.Control.<sessionId>`.
- [ ] Production name has no random/PID component.
- [ ] `PIPE_REJECT_REMOTE_CLIENTS` is enabled.
- [ ] Pipe uses an explicit protected current-user-only DACL.
- [ ] Same-session client PID/session validation occurs after connect and before request processing.
- [ ] No broad fallback ACL exists.
- [ ] Unexpected pre-existing endpoint causes IPC startup failure, not alternate-name fallback.

### Framing

- [ ] Pipe uses message type/read mode.
- [ ] One complete CHUD request is one pipe message.
- [ ] One complete CHUD response is one pipe message.
- [ ] Input is bounded to `kMaxFrameBytes` before codec dispatch.
- [ ] `ERROR_MORE_DATA` / oversized messages are rejected without unbounded allocation.
- [ ] Existing CH-RTF-4 codec is used for decode/encode.

### Read-only behavior

- [ ] `GetRuntimeInfo` is externally reachable.
- [ ] `GetSettingsSnapshot` is externally reachable.
- [ ] Both execute through `RuntimeControlDispatchBridge` on the main thread.
- [ ] No pipe worker directly calls `IRuntimeControl` or App settings methods.
- [ ] All known mutation operations return `RuntimeUnavailable` in CH-RTF-6.
- [ ] Blocked mutations never reach the dispatch bridge.
- [ ] `RequestShutdown` remains disabled.

### Protocol errors

- [ ] Correlatable decode errors return correlated error responses.
- [ ] Unknown operation preserves raw operation ID + requestId.
- [ ] Pre-identity malformed requests do not receive fabricated responses.
- [ ] Response encoding failure never emits manual/malformed bytes.

### Lifecycle

- [ ] Pipe server starts only after the dispatch bridge is accepting.
- [ ] Pipe server startup failure is non-fatal to Standalone ClawHUD.
- [ ] `runtimeControlBridge_.Stop()` remains before pipe-server stop.
- [ ] Pipe-server stop cancels blocked pipe I/O and joins the worker.
- [ ] No shutdown deadlock when worker is waiting in bridge dispatch.
- [ ] RuntimeMessageWindow remains alive until bridge/server shutdown has completed.
- [ ] Repeated/RAII cleanup is safe.

### Regression

- [ ] Tray behavior unchanged.
- [ ] F8 behavior unchanged.
- [ ] suspend/resume behavior unchanged.
- [ ] telemetry/game detection behavior unchanged.
- [ ] legacy Settings behavior unchanged.
- [ ] HUD/VRR presentation contract untouched.
- [ ] Debug build clean.
- [ ] Release build clean.
- [ ] normal CTest suite passes including new pipe tests.

---

## 23. Expected ownership after CH-RTF-6

```text
App
├─ RuntimeMessageWindow
├─ RuntimeControlDispatchBridge
├─ RuntimeControlPipeServer
│   ├─ deterministic per-session endpoint
│   ├─ current-user DACL
│   ├─ local-only + same-session gate
│   ├─ message-mode bounded frame I/O
│   ├─ CHUD codec
│   └─ read-only operation gate
├─ TrayIcon
├─ HudController
├─ PresentMonTelemetryProvider
├─ ProductionTelemetryController
├─ GameSessionController
├─ HudSettingsStore
├─ DebugObservationController (lazy)
├─ SettingsWindow (legacy)
└─ TweakStartupCoordinator
```

Read request path:

```text
external client
 -> Named Pipe worker
 -> DecodeControlRequest
 -> read-only gate
 -> RuntimeControlDispatchBridge
 -> RuntimeMessageWindow
 -> main thread
 -> IRuntimeControl
 -> authoritative response
 -> EncodeControlResponse
 -> external client
```

No mutation crosses the pipe yet.

---

## 24. Handoff to CH-RTF-7

CH-RTF-6 should leave CH-RTF-7 with a transport that is already proven for:

```text
discovery
security
framing
codec integration
main-thread dispatch
read-only runtime query
shutdown-safe worker lifetime
```

CH-RTF-7 can then focus only on intentionally opening:

```text
setting mutation operations
opacity preview/commit external semantics
RequestShutdown response-before-exit lifecycle
```

without simultaneously reviewing basic pipe security or framing.

That separation is the main purpose of this PR boundary.
