# CH-RTF-7 — IPC Settings Mutations and Graceful Shutdown Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** ClawHUD Runtime / Frontend Separation  
> **Architecture source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md`  
> **PR plan source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_PR_PLAN_2026-09-02.md`  
> **Previous PRs:** #209 CH-RTF-1, #210 CH-RTF-2, #211 CH-RTF-3, #212 CH-RTF-4, #213 CH-RTF-5, #214 CH-RTF-6  
> **Analyzed main HEAD:** `05f952746abb135bdaf9db09b7badf2771dcd319`  
> **Scope:** Open the existing secure Control Named Pipe to the protocol-v1 settings mutations and implement response-before-exit `RequestShutdown`  
> **Status:** Ready for implementation

---

## 1. Objective

CH-RTF-6 established the externally reachable, current-user/same-session/local-only Named Pipe endpoint, but intentionally exposes only:

```text
GetRuntimeInfo
GetSettingsSnapshot
```

CH-RTF-7 completes the ClawHUD-side external frontend control surface by allowing the already-defined protocol-v1 settings mutations through the same server and the already-proven CH-RTF-5 main-thread dispatch path.

The target external execution model is:

```text
frontend / SteamAddon in the future
    |
    | protocol-v1 request frame
    v
RuntimeControlPipeServer worker
    |
    | decode + security/session checks
    v
RuntimeControlDispatchBridge
    |
    | RuntimeMessageWindow wake
    v
ClawHUD main thread
    |
    | ExecuteRuntimeControlRequest
    v
IRuntimeControl / App existing product semantics
    |
    | fresh authoritative post-mutation snapshot
    v
Control response
    |
    `-> pipe worker encodes + sends response
```

For `RequestShutdown`, the required sequence is different and is the most important lifecycle rule in this PR:

```text
pipe worker
    -> dispatch RequestShutdown to main thread
    -> main thread approves it and returns Ok + internal shutdown-after-response flag
    -> pipe worker sends the Ok response
    -> client consumes response and closes the one-request connection
    -> pipe worker disconnects/closes that pipe instance
    -> pipe worker posts a dedicated shutdown-ready message to RuntimeMessageWindow
    -> main thread receives it
    -> App::Exit()
    -> normal ClawHUD shutdown path
```

The response must be safely delivered **before** normal application teardown starts.

---

## 2. Current production baseline after PR #214

### 2.1 Wire protocol is already complete for CH-RTF-7

`src/shared/ClawHudControlProtocol.h` already defines protocol v1 operations:

```text
GetRuntimeInfo                         1
GetSettingsSnapshot                   2
SetStartWithWindows                   10
SetHudEnabled                         11
SetHudVisibilityMode                  12
SetHudSizeOffset                      13
SetHudFont                            14
SetHudAlignment                       15
SetHudBackgroundMode                  16
PreviewHudOpacity                     17
CommitHudOpacity                      18
SetIntelVrrRangeFixEnabled            19
RequestShutdown                       20
```

The existing response contract already says:

```text
successful settings mutation -> authoritative WireSettingsSnapshot
successful RequestShutdown   -> Ok + empty payload
```

Do **not** change:

```text
protocol version
frame header
operation IDs
status values
payload layouts
pipe name
wire enum values
wire bounds
```

No protocol-v2 work belongs in this PR.

### 2.2 Semantic mutation mapping already exists

CH-RTF-5 already maps every settings mutation through:

```cpp
ExecuteRuntimeControlRequest(...)
```

into the existing `IRuntimeControl` implementation on `App`.

It already preserves:

```text
SetStartWithWindows rollback semantics
SetHudEnabled failure semantics
visibility/game-session side effects
size/font/alignment/background persistence
opacity preview vs commit
Intel VRR setting persistence
authoritative snapshot after mutation
```

Do not duplicate or reimplement those product rules in the pipe server.

### 2.3 CH-RTF-6 server is intentionally read-only

The current server performs:

```text
decoded request
    -> IsReadOnlyOperation?
       -> yes: dispatch bridge
       -> no: RuntimeUnavailable without dispatch
```

CH-RTF-7 removes this temporary read-only gate for known protocol-v1 operations.

The server must still have no direct authority over:

```text
HudController
HudPresentation
SettingsWindow
HudSettingsStore
game-session state
telemetry
VRR tweak execution
App mutation methods
```

Every runtime operation still goes through the dispatch bridge and therefore the main thread.

### 2.4 Current server response drain matters for shutdown

After writing a response, CH-RTF-6 currently waits for the client to consume the response and close its end before disconnecting the server pipe instance.

This behavior exists because `DisconnectNamedPipe` may discard unread response data.

Do not remove this protection merely to make shutdown easier.

---

## 3. Non-negotiable architectural rules

1. The Named Pipe worker must never directly invoke `IRuntimeControl` or `App::Exit()`.
2. Every settings mutation must execute on the ClawHUD main thread through `RuntimeControlDispatchBridge`.
3. `RequestShutdown` must not call `App::Exit()` while its dispatch request is still waiting for completion.
4. The shutdown success response must be delivered before teardown is initiated.
5. The existing CH-RTF-6 pipe security model stays unchanged.
6. The existing CH-RTF-4 wire format stays unchanged.
7. Do not create a generic RPC framework, deferred-action system, event bus, command bus, task scheduler, or DI layer.
8. Do not add a generic `StateChanged` subscription system.
9. Do not add `--managed` in this PR.
10. Do not redesign startup/update/single-instance lifecycle in this PR.

---

## 4. Required internal execution-result shape

A plain `ControlResponse` is no longer quite enough internally because `RequestShutdown` must carry one piece of **non-wire** lifecycle information:

```text
the response is Ok
AND
shutdown must be initiated only after that response has been delivered
```

Use one small internal result type. Recommended shape:

```cpp
struct RuntimeControlExecutionResult
{
    control::ControlResponse response;
    bool shutdownAfterResponse{};
};
```

The exact type/file name may vary, but the semantics must remain this narrow.

Required rules:

- `shutdownAfterResponse == false` for every operation except a successful `RequestShutdown`.
- terminal bridge failures such as `ShuttingDown` / `RuntimeUnavailable` always carry `false`.
- the boolean is **not serialized** and is not added to `ControlResponse`.
- do not add a general `DeferredAction` vector/list/variant framework.
- do not add a generic callback payload system.

A `None/Shutdown` two-value enum is acceptable if it keeps the implementation clearer, but it must not evolve into an action bus in this PR.

Recommended ownership is near the runtime-control mapping/dispatch code, not under `src/shared`, because it is process-internal behavior rather than the public wire contract.

---

## 5. Update the main-thread mapping result

Change the internal execution function conceptually from:

```cpp
control::ControlResponse ExecuteRuntimeControlRequest(...);
```

to:

```cpp
RuntimeControlExecutionResult ExecuteRuntimeControlRequest(...);
```

All existing operations should simply wrap their current response with:

```text
shutdownAfterResponse = false
```

### `RequestShutdown`

Change the current CH-RTF-5 temporary behavior:

```text
RequestShutdown -> RuntimeUnavailable
```

to:

```text
RequestShutdown
-> main-thread execution reaches ExecuteRuntimeControlRequest
-> response.status = Ok
-> response payload = empty
-> shutdownAfterResponse = true
```

Important:

- Do not call `App::Exit()` here.
- Do not call `PostQuitMessage()` here.
- Do not stop the bridge/server here.
- Do not destroy any HWND here.

This call represents **main-thread approval** of the shutdown request, not teardown itself.

Because the request reached this function through the dispatch bridge, a bridge that has already entered `Stop()` will return `ShuttingDown` before this point and will not arm another shutdown.

---

## 6. Update `RuntimeControlDispatchBridge`

The bridge must carry the new internal execution result instead of only `ControlResponse`.

Conceptually update:

```text
Handler
Pending.result
Dispatch() return value
Complete(...)
terminal result creation
```

from `ControlResponse` to `RuntimeControlExecutionResult`.

### Terminal results

For:

```text
bridge not accepting
wake failure
missing handler
Stop() cancellation
```

return the same existing wire statuses as today, with:

```text
shutdownAfterResponse = false
```

### Lifetime invariants remain unchanged

Preserve all CH-RTF-5 guarantees:

```text
FIFO queue
main-thread execution
no stack pointer posting
condition-variable/event wait without polling
Stop() releases pending waiters
new submissions after Stop() fail deterministically
PostMessage failure releases waiter
main-thread self-dispatch cannot deadlock
```

Do not weaken any existing dispatch tests while changing the result type.

---

## 7. Open the Named Pipe mutation surface

Remove the CH-RTF-6 temporary read-only gate.

After a request has:

```text
passed pipe security/session checks
passed bounded message read
successfully decoded as a known protocol-v1 operation
```

it should be sent to the existing dispatch callback regardless of whether it is a read or mutation operation.

Conceptually:

```cpp
if (decoded.ok)
{
    execution = dispatch_(decoded.value);
}
```

Do not maintain two copies of operation policy such as:

```text
pipe-server mutation allowlist
AND
runtime-control mapping operation switch
```

The codec + runtime-control mapping are the operation authorities.

The pipe server still owns only:

```text
transport security
message bounds
framing/codec invocation
request/response I/O
post-response shutdown handoff
```

---

## 8. External mutation semantics

The following protocol operations now become externally functional.

### 8.1 `SetStartWithWindows`

Allow the existing protocol-v1 operation.

Execution remains:

```text
main thread
-> App::SetStartWithWindows(request.flag)
-> existing shortcut apply/remove logic
-> existing rollback if registration fails
-> fresh GetSettingsSnapshot()
-> Ok + authoritative snapshot
```

Do not return the requested boolean as an acknowledgement.

If the runtime rolls the value back, the returned snapshot must show the rolled-back state.

`--managed` does not exist yet, so do not add Managed-specific startup semantics here. CH-RTF-9 will harden mode-aware startup behavior later.

### 8.2 `SetHudEnabled`

Preserve existing CH-RTF-5 behavior:

```text
IRuntimeControl::SetHudEnabled(false/true)
```

If the semantic method returns `false`:

```text
OperationFailed
empty payload
```

On success:

```text
Ok
fresh authoritative snapshot
```

Do not bypass existing HUD initialization/recreation/rollback logic.

### 8.3 `SetHudVisibilityMode`

Keep explicit wire-to-semantic mapping.

The mutation must continue to reuse App's existing:

```text
foreground adoption
production telemetry visibility sync
game-session reevaluation
FPS sampling reconciliation
HUD visibility reconciliation
persistence
```

Do not reproduce any of that in the IPC layer.

### 8.4 `SetHudSizeOffset`

Only the existing protocol range is valid:

```text
-2 .. +2
```

Codec/mapping validation remains authoritative.

Return a fresh snapshot.

### 8.5 `SetHudFont`

Use the existing explicit wire enum mapping and App/HudController path.

A runtime rollback/failure that leaves the previous font active must be visible through the returned authoritative snapshot.

### 8.6 `SetHudAlignment`

Use the existing explicit mapping and return the fresh snapshot.

### 8.7 `SetHudBackgroundMode`

Use the existing explicit mapping and return the fresh snapshot.

Do not change any HUD presentation/window contract while enabling this IPC operation.

### 8.8 `SetIntelVrrRangeFixEnabled`

Keep the current semantic meaning:

```text
change/persist the setting
```

Do **not** run the Intel VRR tweak immediately from IPC.

The existing startup-applied tweak behavior remains unchanged.

---

## 9. Opacity preview / commit — preserve the distinction exactly

This is a public protocol contract, not an implementation detail.

### Preview

```text
PreviewHudOpacity(percent)
-> main thread
-> existing PreviewHudOpacity(float)
-> live background-only opacity change
-> no settings persistence
-> fresh authoritative snapshot
```

### Commit

```text
CommitHudOpacity(percent)
-> main thread
-> existing CommitHudOpacity(float)
-> apply final background-only opacity
-> persist final value
-> fresh authoritative snapshot
```

Required wire range remains:

```text
50, 55, 60, ... 100
```

Do not:

- collapse preview and commit into one externally visible setter;
- persist every preview event;
- implement opacity at the window/visual level;
- change foreground text/outline/separator opacity;
- alter any HUD click-through/activation/presentation behavior.

`Background Opacity` continues to mean **background only**.

---

## 10. `RequestShutdown` response-before-exit handoff

This is the core new lifecycle behavior.

### 10.1 Why direct exit is forbidden

The following implementation is invalid:

```text
pipe worker Dispatch(RequestShutdown)
-> main thread ExecuteRuntimeControlRequest
-> App::Exit()
-> StopRuntimeSources()
-> runtimeControlBridge_.Stop()
-> runtimeControlPipeServer_.Stop()
-> join pipe worker
```

At that moment the pipe worker is still blocked waiting for the dispatch result, while the main thread is trying to join that same worker.

That is a real deadlock.

Do not call `Exit()` from inside the dispatch handler.

### 10.2 Required two-phase shutdown

Use this exact semantic order:

```text
PHASE A — approve and acknowledge

pipe worker
-> Dispatch(RequestShutdown)
-> main thread returns:
     response = Ok / empty payload
     shutdownAfterResponse = true
-> bridge completes pending request
-> pipe worker encodes response
-> pipe worker writes response
-> client reads response
-> client closes one-request connection
-> server completes its existing response drain
-> server disconnects + closes current pipe instance

PHASE B — execute normal shutdown

pipe worker
-> invoke one narrow "shutdown response delivered" callback
-> callback only PostMessage()s RuntimeMessageWindow
-> pipe worker returns to normal server loop / is cancellable

RuntimeMessageWindow main thread
-> receives dedicated runtime-control shutdown-ready message
-> App handler
-> App::Exit()
-> existing normal cleanup
```

### 10.3 Callback rule

The server may receive one narrow callback such as conceptually:

```cpp
using ShutdownReadyCallback = std::function<bool()>;
```

Production implementation:

```cpp
[this]
{
    return PostMessageW(runtimeMessageWindow_.Window(),
        kRuntimeControlShutdownReadyMessage, 0, 0) != FALSE;
}
```

The callback runs on the pipe worker and may **only post the message**.

It must not:

```text
call App::Exit directly
PostQuitMessage directly
destroy RuntimeMessageWindow
stop the bridge
stop the pipe server
```

If the `PostMessage` fails:

- log the failure;
- do not fall back to worker-thread teardown;
- do not add retry machinery solely for this case.

The successful wire response has already been delivered. If the runtime remains alive, a later control request may still be made.

### 10.4 Invoke callback only after successful response delivery

`shutdownAfterResponse` must be discarded if any of these occur:

```text
response encoding fails
response write fails
connection is aborted before response delivery completes
server Stop() cancels the active response path
```

Only a successfully delivered/consumed response may arm the post-response shutdown message.

### 10.5 Close the current pipe before posting shutdown

Prefer this worker sequence:

```text
ServeClient() -> returns shutdownAfterResponse flag
DisconnectNamedPipe(current instance)
CloseHandle(current instance)
if (shutdownAfterResponse)
    shutdownReadyCallback()
```

This ensures App teardown cannot destroy the server while the acknowledged shutdown connection still owns unread response bytes.

The exact factoring may differ, but this ordering invariant must hold.

---

## 11. Dedicated RuntimeMessageWindow shutdown-ready message

Reserve one new free runtime message ID.

Current known private/runtime IDs include:

```text
WM_APP + 1       Settings destroyed
WM_APP + 2/5/6/7/8/9  game-session messages
WM_APP + 11      runtime-control dispatch wake
```

`WM_APP + 12` is acceptable if it is still free on the implementation branch.

Use a clearly named constant, conceptually:

```cpp
constexpr UINT kRuntimeControlShutdownReadyMessage = WM_APP + 12;
```

Required routing:

```text
RuntimeMessageWindow::WindowProc
-> kRuntimeControlShutdownReadyMessage
-> App::HandleRuntimeControlShutdownReady()
-> App::Exit()
```

`App::Exit()` already has an `exiting_` guard; preserve idempotency.

Do not overload:

```text
WM_CLOSE
WM_QUIT
tray callback messages
WM_TIMER
kRuntimeControlDispatchMessage
```

for this handoff.

---

## 12. Normal shutdown ordering remains valid

Current production cleanup after CH-RTF-6 starts with:

```text
runtimeControlBridge_.Stop()
runtimeControlPipeServer_.Stop()
...
runtimeMessageWindow_.Destroy()
```

Keep that ordering for ordinary Exit/destructor paths.

Why:

```text
bridge.Stop()
-> releases pipe worker if it is waiting inside Dispatch()

pipeServer.Stop()
-> cancels pipe I/O and joins worker

then runtime message window can be destroyed
```

Do not reverse server/bridge shutdown order.

The post-response `RequestShutdown` path joins this same ordinary cleanup only **after** the shutdown-ready message reaches the main thread.

---

## 13. Server security contract is unchanged

CH-RTF-7 increases authority available to an authenticated local client, so weakening the CH-RTF-6 security boundary is forbidden.

Preserve all of the following unchanged:

```text
\\.\pipe\ClawHUD.Control.<sessionId>
protected current-user-only DACL
PIPE_REJECT_REMOTE_CLIENTS
GetNamedPipeClientProcessId
ProcessIdToSessionId
same-session check before decode/dispatch
FILE_FLAG_FIRST_PIPE_INSTANCE
bounded kMaxFrameBytes read
one pipe instance at a time
one request / one response / one connection
CH-RTF-4 codec as the only parser
```

Do not add:

```text
Everyone access
Authenticated Users access
BUILTIN\Users access
remote pipe support
fallback unscoped pipe name
random alternate endpoint
weaker ACL fallback
```

---

## 14. Correlated protocol failures remain transport-level behavior

Preserve CH-RTF-6 behavior for malformed/version-skewed input.

### Correlatable decode failure

If the fixed frame identity is trustworthy:

```text
UnknownOperation
InvalidPayload
InvalidValue
...
```

return the correlated error response with original raw operation ID + request ID.

### Uncorrelatable malformed frame

If magic/header identity is not trustworthy:

```text
close connection
send no fabricated response
```

A decode failure must never execute a mutation or arm shutdown.

---

## 15. No state-change event bus yet

CH-RTF-7 completes request/response mutation control only.

Do not add:

```text
StateChanged events
long-lived subscription connections
push notifications
telemetry streaming
F8 notification messages over IPC
Settings change broadcasts
```

Frontend synchronization remains:

```text
frontend open/activation
-> GetSettingsSnapshot

frontend mutation
-> authoritative snapshot in mutation response

F8 or other external local change
-> frontend refreshes snapshot when it next activates/needs state
```

This remains the agreed first-protocol model.

---

## 16. Expected source changes

Primary files expected:

```text
src/ClawHUD/RuntimeControlWireMapping.h
src/ClawHUD/RuntimeControlWireMapping.cpp
src/ClawHUD/RuntimeControlDispatchBridge.h
src/ClawHUD/RuntimeControlDispatchBridge.cpp
src/ClawHUD/RuntimeControlPipeServer.h
src/ClawHUD/RuntimeControlPipeServer.cpp
src/ClawHUD/App.h
src/ClawHUD/App.cpp
src/ClawHUD/RuntimeMessageWindow.cpp
```

Tests likely:

```text
tests/RuntimeControlDispatchTests.cpp
tests/RuntimeControlPipeServerTests.cpp
cmake/ClawHUDTests.cmake only if target dependencies need adjustment
```

Possible small internal header if desired:

```text
src/ClawHUD/RuntimeControlExecutionResult.h
```

Avoid changing:

```text
src/shared/ClawHudControlProtocol.h
src/shared/ClawHudControlCodec.*
```

unless a test-only compile dependency requires a mechanical include change. The v1 wire contract itself must not change.

No HUD presentation source should need modification.

---

## 17. Required tests

Extend the existing focused test suites. Do not replace or weaken current CH-RTF-4/5/6 tests.

### 17.1 Bridge result propagation

Verify:

```text
normal request -> response + shutdownAfterResponse=false
RequestShutdown on main thread -> Ok + empty payload + shutdownAfterResponse=true
worker RequestShutdown -> executes mapping on registered main thread
Stop cancellation -> ShuttingDown + shutdownAfterResponse=false
wake failure -> RuntimeUnavailable + shutdownAfterResponse=false
main-thread self-dispatch -> no deadlock
```

### 17.2 Real Named Pipe mutation round trips

Using the real test Named Pipe endpoint, verify successful external round trips for:

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
```

Prefer an end-to-end test path where practical:

```text
real pipe
-> real codec
-> RuntimeControlDispatchBridge
-> ExecuteRuntimeControlRequest
-> fake IRuntimeControl
```

rather than a pipe callback that simply echoes a canned response for every mutation.

The fake should record thread ID and semantic arguments so the tests prove mutations still execute on the drain/main thread.

### 17.3 Authoritative rollback/result test

At least one mutation must prove that the response is not request echo.

Recommended existing scenario:

```text
client requests SetStartWithWindows(true)
fake semantic runtime simulates rollback and remains false
response = Ok + snapshot.startWithWindows=false
```

Also retain:

```text
SetHudEnabled semantic failure -> OperationFailed / no snapshot
opacity semantic failure -> OperationFailed / no snapshot
```

### 17.4 Opacity preview vs commit

Through the external path verify:

```text
PreviewHudOpacity(70)
-> fake PreviewHudOpacity(0.70f)
-> CommitHudOpacity not called

CommitHudOpacity(70)
-> fake CommitHudOpacity(0.70f)
-> PreviewHudOpacity not called
```

The fake may separately record a "persisted" value to prove only commit changes it.

Retain invalid step/range coverage:

```text
53% -> InvalidValue
<50 / >100 -> rejected by codec or mapper
```

### 17.5 `RequestShutdown` response-before-callback test

Use a real Named Pipe client.

Prove this ordering:

```text
client writes RequestShutdown
client successfully reads/decodes Ok response
client closes connection
only after response path completes does shutdown-ready callback fire
```

The test callback must not terminate the test process; it should record/order events.

Assert:

```text
response.status == Ok
response.requestId preserved
response payload empty
shutdown callback exactly once
shutdown callback is not invoked before successful response delivery
```

### 17.6 Failed response does not arm shutdown

Force or simulate at least one failed response-delivery path and prove:

```text
shutdownAfterResponse result existed
BUT response was not successfully delivered
-> shutdown-ready callback not invoked
```

Use a realistic test mechanism; do not add production fault-injection architecture merely for this test.

### 17.7 Shutdown-ready message main-thread path

Add a focused App/message-window seam test if practical, or make the App wiring statically obvious and cover the lower-level callback ordering with the pipe test.

The production code must clearly show:

```text
worker callback -> PostMessage only
RuntimeMessageWindow -> App handler
App handler -> Exit()
```

There must be no worker-thread direct `Exit()` call.

### 17.8 Existing CH-RTF-6 security/regression tests

All existing tests must continue to pass, including:

```text
endpoint determinism
protected DACL assertions
same-session gate
remote/local pipe flags
oversized message rejection
malformed frame handling
unknown-operation correlation
stop while waiting for connection
stop with idle client
restart same endpoint
bridge-stop-then-server-stop pending-dispatch shutdown
```

### 17.9 Full project regression

Run the repository-supported Release build and the full applicable CTest suite.

Run Debug as well when available in the normal project workflow.

Do not exclude or weaken existing HUD/VRR regression assertions.

---

## 18. HUD / VRR presentation contract — NON-NEGOTIABLE

This PR is runtime-control/IPC work only.

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
existing Presentation API / DirectComposition production presentation path
premultiplied-alpha presentation contract
```

`Background Opacity` remains background-only.

Do not implement external opacity by applying window-wide or visual-wide opacity.

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

must remain enabled and passing.

---

## 19. Explicit non-goals

Do not include any of the following in CH-RTF-7:

```text
--managed parsing
Standalone/Managed composition
conditional tray creation
SteamAddon process ownership
Job Object ownership
Addon installation detection
mode persistence
mode-aware startup shortcut behavior
mode-aware Velopack restart behavior
single-instance mode conversion
SteamAddon IPC client
WinUI3/WPF/Web frontend
legacy Settings removal
persistent multi-request pipe sessions
multiple simultaneous pipe clients
StateChanged/event subscription
telemetry streaming
EC helper sharing
PresentMon redesign
game detection redesign
HUD renderer/presentation changes
Intel VRR algorithm changes
```

These belong to later PRs or separate projects.

---

## 20. Acceptance criteria

CH-RTF-7 is complete only when all of the following are true:

1. The existing secure protocol-v1 Named Pipe accepts all defined settings mutation operations.
2. Every mutation executes through `RuntimeControlDispatchBridge` on the main thread.
3. The pipe worker still has no direct `IRuntimeControl`, `HudController`, or App mutation authority.
4. Successful mutations return a fresh authoritative `WireSettingsSnapshot`.
5. Runtime mutation failures preserve existing `OperationFailed` behavior.
6. `SetStartWithWindows` rollback is observable through the returned authoritative snapshot.
7. `PreviewHudOpacity` changes runtime opacity without persistence.
8. `CommitHudOpacity` persists the final opacity.
9. Opacity remains background-only.
10. `RequestShutdown` returns protocol-v1 `Ok` with an empty payload.
11. `RequestShutdown` does not call `App::Exit()` from the dispatch handler.
12. The shutdown response is delivered and the current pipe instance is closed before shutdown execution is posted.
13. The pipe worker's shutdown-ready callback only posts a message to `RuntimeMessageWindow`.
14. `RuntimeMessageWindow` routes that dedicated message to a main-thread App handler.
15. The App handler enters the existing idempotent `App::Exit()` path.
16. A failed/aborted shutdown response does not trigger shutdown-ready callback execution.
17. Ordinary App cleanup preserves `bridge.Stop()` before `pipeServer.Stop()`.
18. CH-RTF-6 current-user/session/local-only security behavior is unchanged.
19. Protocol v1 wire bytes/IDs/version are unchanged.
20. Existing malformed/oversized/version-skew behavior remains unchanged.
21. No generic event bus/deferred-action framework is introduced.
22. No Managed mode is introduced.
23. No HUD presentation contract code is changed.
24. Focused and full applicable tests pass.

---

## 21. Expected ownership after CH-RTF-7

```text
ClawHUD.exe (still Standalone only)
|
+-- RuntimeMessageWindow
|    +-- F8
|    +-- power
|    +-- timers
|    +-- runtime-control dispatch wake
|    `-- runtime-control shutdown-ready message
|
+-- RuntimeControlPipeServer
|    +-- secure local current-user/same-session endpoint
|    +-- all protocol-v1 request/response I/O
|    +-- no runtime mutation authority
|    `-- post-response shutdown-ready PostMessage callback
|
+-- RuntimeControlDispatchBridge
|    +-- worker -> main-thread queue
|    +-- cancellation/lifetime
|    `-- internal response + shutdownAfterResponse result
|
+-- ExecuteRuntimeControlRequest
|    +-- explicit wire/semantic mapping
|    +-- authoritative mutation responses
|    `-- RequestShutdown approval only
|
+-- App / IRuntimeControl
|    +-- existing settings/product semantics
|    `-- normal Exit lifecycle
|
+-- TrayIcon
`-- legacy SettingsWindow
```

No frontend technology or Managed launch composition changes yet.

---

## 22. Next PR

After CH-RTF-7 is merged and externally controlled mutations/shutdown are proven, proceed to:

```text
CH-RTF-8 — Add explicit Standalone / Managed launch composition
```

That PR will finally introduce:

```text
ClawHUD.exe            -> Standalone
ClawHUD.exe --managed  -> Managed / no Tray
```

using the same runtime/control server implemented through CH-RTF-7.
