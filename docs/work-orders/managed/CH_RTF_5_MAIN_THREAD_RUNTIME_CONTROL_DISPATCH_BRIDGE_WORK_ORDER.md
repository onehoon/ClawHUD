# CH-RTF-5 — Main-Thread Runtime Control Dispatch Bridge Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** ClawHUD Runtime / Frontend Separation  
> **Architecture source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md`  
> **PR plan source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_PR_PLAN_2026-09-02.md`  
> **Previous PRs:** #209 CH-RTF-1, #210 CH-RTF-2, #211 CH-RTF-3, #212 CH-RTF-4  
> **Analyzed main HEAD:** `644c96611fe456b5dad32ac699325c508625f894`  
> **Scope:** Add the in-process dispatch bridge that moves validated Control requests from a background producer to the ClawHUD main thread and returns authoritative responses  
> **Status:** Ready for implementation

---

## 1. Objective

Connect the protocol contract introduced by CH-RTF-4 to the semantic runtime-control contract introduced by CH-RTF-3 **without adding a Named Pipe yet**.

The required execution model is:

```text
future IPC worker / test producer
    |
    | validated clawhud::control::ControlRequest
    v
RuntimeControlDispatchBridge
    |
    | queue + PostMessage(RuntimeMessageWindow)
    v
ClawHUD main thread
    |
    | explicit wire -> semantic mapping
    v
clawhud::IRuntimeControl
    |
    | existing App product semantics
    v
RuntimeSettingsSnapshot
    |
    | explicit semantic -> wire mapping
    v
clawhud::control::ControlResponse
    |
    `-> signal waiting producer
```

The key invariant is:

> **No future transport/background thread may directly invoke `IRuntimeControl`, `HudController`, HUD presentation, game-session state, SettingsWindow, or runtime shutdown orchestration.**

The ClawHUD main thread remains the sole mutation authority.

This PR must prove the dispatch/lifetime/mapping behavior using an in-process fake producer. It must not create a Named Pipe server/client.

---

## 2. Current production baseline after PR #212

### 2.1 Runtime message window

`RuntimeMessageWindow` currently owns the hidden runtime HWND and directly forwards:

```text
WM_HOTKEY / F8
WM_POWERBROADCAST
WM_TIMER
```

to `App`.

It is already independent of the Tray HWND and is the correct wake-up target for the future Control path.

Do not move the Control path back to the Tray window.

### 2.2 Semantic runtime-control boundary

`App` implements:

```cpp
clawhud::IRuntimeControl
```

with:

```text
GetSettingsSnapshot
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

These methods already contain the required runtime semantics, persistence, recreation, rollback, foreground/game-session side effects, and HUD lifecycle coordination.

Do not duplicate those behaviors in the dispatch layer.

### 2.3 Wire protocol

CH-RTF-4 added:

```text
src/shared/ClawHudControlProtocol.h
src/shared/ClawHudControlCodec.h/.cpp
```

with:

```text
ControlRequest
ControlResponse
WireSettingsSnapshot
WireRuntimeInfo
explicit wire enums
explicit v1 operation IDs
```

The codec already rejects malformed frames and invalid operation payloads.

CH-RTF-5 starts **after successful decode**.

Do not move byte/frame parsing into the dispatch bridge.

---

## 3. Required source shape

Add one small dispatch component under the ClawHUD runtime source tree.

Recommended names:

```text
src/ClawHUD/RuntimeControlDispatchBridge.h
src/ClawHUD/RuntimeControlDispatchBridge.cpp
```

A separate mapping file is acceptable only if the mapping code becomes meaningfully clearer, for example:

```text
src/ClawHUD/RuntimeControlWireMapping.h/.cpp
```

Do not create a generic RPC framework, command bus, service locator, actor system, job scheduler, event bus, or dependency-injection layer.

The bridge has exactly one purpose:

```text
background-safe request submission
    -> main-thread execution
    -> response completion
```

---

## 4. Dispatch queue and request lifetime

Never post a pointer to a stack-owned request through `WPARAM` / `LPARAM`.

The request must remain valid if:

```text
worker submits
-> main thread is temporarily busy
-> shutdown starts
-> runtime window is destroyed
```

Use owned shared state conceptually similar to:

```cpp
struct PendingRuntimeControlRequest
{
    clawhud::control::ControlRequest request;

    std::mutex mutex;
    std::condition_variable completedCv;
    bool completed{};
    clawhud::control::ControlResponse response;
};
```

and a bridge-owned FIFO queue such as:

```text
mutex
queue<shared_ptr<PendingRuntimeControlRequest>>
accepting flag
runtime HWND
main-thread id
```

The exact synchronization primitive may vary. A Win32 event is also acceptable.

Required rules:

1. The queued request is heap/owner-backed; no borrowed request pointer may outlive its caller.
2. The queue is FIFO.
3. The worker waits for completion without polling.
4. The main thread completes each request exactly once.
5. `Stop()` completes/unblocks every pending waiter.
6. New requests after stop begins fail deterministically instead of entering the queue.
7. `Start()` / `Stop()` must be idempotent enough for the current App cleanup paths.
8. Do not add arbitrary retry loops.
9. Do not require a timeout merely to defend against theoretical scheduler interleavings. Correct shutdown cancellation is required; speculative timeout machinery is not.

---

## 5. Runtime wake-up message

Reserve one new App/runtime message ID that does not collide with current runtime messages.

Current known ownership includes:

```text
WM_APP + 1      Settings-destroyed notification
WM_APP + 2/5/6/7/8/9  game-session messages
WM_TIMER IDs    telemetry / resume recovery
```

Use a clearly named dedicated message outside those current values, for example conceptually:

```cpp
constexpr UINT kRuntimeControlDispatchMessage = WM_APP + 10;
```

The exact free value may differ after checking the implementation branch, but it must remain globally non-conflicting.

Required flow:

```text
RuntimeControlDispatchBridge::Dispatch(...)
-> enqueue pending request
-> PostMessage(runtime HWND, kRuntimeControlDispatchMessage, 0, 0)

RuntimeMessageWindow::WindowProc
-> receives kRuntimeControlDispatchMessage
-> calls App::HandleRuntimeControlDispatch()

App::HandleRuntimeControlDispatch()
-> asks the bridge to drain/execute queued requests on this thread
```

Do not reuse:

```text
WM_TIMER
tray callback messages
Settings HWND messages
F8 hotkey messages
```

for Control dispatch.

It is acceptable for multiple worker submissions to produce multiple wake messages. The main-thread handler may drain all currently queued requests in one pass; later redundant wake messages may become no-ops.

---

## 6. Main-thread authority

Capture or otherwise know the thread that starts/binds the bridge.

All calls into:

```text
IRuntimeControl
App runtime-control implementation
```

must occur from the ClawHUD main/UI message-loop thread.

Tests must prove this.

A future pipe worker will call only the thread-safe submission entry point.

It must never call:

```cpp
runtimeControl.SetHudEnabled(...);
runtimeControl.SetHudFont(...);
runtimeControl.GetSettingsSnapshot();
```

directly.

Do not add locks around `HudController` or presentation state as an alternative to main-thread dispatch.

---

## 7. Explicit wire-to-semantic mapping

CH-RTF-4 deliberately uses protocol-specific wire enum values.

CH-RTF-5 must map them explicitly.

Do not use ordinal casts such as:

```cpp
static_cast<clawhud::HudAlignment>(request.wireEnum)
```

unless the function first switches on every supported wire value and returns the explicit semantic value.

Required mappings:

```text
WireVisibilityMode::Always      -> HudVisibilityMode::Always
WireVisibilityMode::InGameOnly  -> HudVisibilityMode::InGameOnly

WireAlignment::Left             -> HudAlignment::Left
WireAlignment::Center           -> HudAlignment::Center
WireAlignment::Right            -> HudAlignment::Right

WireFont::Unispace              -> HudFont::Unispace
WireFont::SegoeUiVariable       -> HudFont::SegoeUiVariable

WireBackgroundMode::FullWidth    -> HudBackgroundMode::FullWidth
WireBackgroundMode::ContentWidth -> HudBackgroundMode::ContentWidth
```

Opacity conversion:

```text
wire u16 percent 50..100
-> semantic float fraction
-> percent / 100.0f
```

The codec already validates the v1 range/step, but the mapper must still fail safely if a manually constructed invalid `ControlRequest` reaches the bridge in a unit test or future internal caller.

Return a stable protocol error such as `InvalidValue`; do not silently default to another enum value.

---

## 8. Explicit semantic-to-wire snapshot mapping

Every successful settings operation that requires a snapshot response must build it from a fresh:

```cpp
IRuntimeControl::GetSettingsSnapshot()
```

after the mutation finishes.

Do not echo the requested value.

Map explicitly:

```text
RuntimeSettingsSnapshot.startWithWindows
RuntimeSettingsSnapshot.hudEnabled
RuntimeSettingsSnapshot.hudSizeOffset
RuntimeSettingsSnapshot.hudFont
RuntimeSettingsSnapshot.hudOptions.visibilityMode
RuntimeSettingsSnapshot.hudOptions.alignment
RuntimeSettingsSnapshot.hudOptions.backgroundMode
RuntimeSettingsSnapshot.hudOptions.backgroundOpacity
RuntimeSettingsSnapshot.intelVrrRangeFixEnabled
RuntimeSettingsSnapshot.intelVrrLastResult
```

into `WireSettingsSnapshot`.

### Opacity

Use the existing product conversion helper where practical, or an equivalent deterministic conversion, so the wire value is one of the supported integer percentages.

Do not expose binary float representation on the wire.

### Intel VRR status

Map every current semantic status explicitly:

```text
Disabled             -> WireIntelVrrStatus::Disabled
Unavailable          -> WireIntelVrrStatus::Unavailable
UnsupportedPanel     -> WireIntelVrrStatus::UnsupportedPanel
AmbiguousDisplay     -> WireIntelVrrStatus::AmbiguousDisplay
AlreadyCorrect       -> WireIntelVrrStatus::AlreadyCorrect
SkippedUserProfile   -> WireIntelVrrStatus::SkippedUserProfile
Applied              -> WireIntelVrrStatus::Applied
ApplyFailed          -> WireIntelVrrStatus::ApplyFailed
VerificationFailed   -> WireIntelVrrStatus::VerificationFailed
```

Copy the existing UTF-8 result strings as semantic values; do not reread or reinterpret the result store in the mapping layer.

If a semantic value cannot be represented by protocol v1, fail the response deterministically rather than casting an unknown value onto the wire.

---

## 9. Operation execution semantics

Handle the currently defined v1 operations explicitly.

### 9.1 `GetSettingsSnapshot`

```text
main thread
-> GetSettingsSnapshot()
-> convert to WireSettingsSnapshot
-> ControlStatus::Ok
```

### 9.2 `SetStartWithWindows`

```text
main thread
-> SetStartWithWindows(request.flag)
-> GetSettingsSnapshot()
-> Ok + authoritative snapshot
```

`SetStartWithWindows()` currently performs its own rollback when shortcut creation/removal fails.

Do not infer success from the requested checkbox value.

The returned snapshot must reflect the actual resulting state.

### 9.3 `SetHudEnabled`

```text
main thread
-> SetHudEnabled(request.flag)
```

If the semantic call returns `false`, return:

```text
ControlStatus::OperationFailed
empty payload
```

If it returns `true`, return:

```text
ControlStatus::Ok
fresh authoritative snapshot
```

Do not bypass `HudController::Ensure()` or existing App orchestration.

### 9.4 Visibility / size / font / alignment / background

Map the validated value explicitly, call the existing semantic method, then return a fresh authoritative snapshot.

Several current setters can normalize or roll back through `HudController`; the snapshot is therefore the result authority.

### 9.5 Opacity preview / commit

Keep the semantic distinction introduced by CH-RTF-3:

```text
PreviewHudOpacity
-> live apply
-> no persistence

CommitHudOpacity
-> apply
-> persist final value
```

Do not collapse them into one setter with a public `persist` flag.

If the semantic opacity operation returns `false`, return `OperationFailed`.

On success return the fresh authoritative snapshot required by protocol v1.

`Background Opacity` remains background-only.

### 9.6 Intel VRR setting

Call:

```text
SetIntelVrrRangeFixEnabled
```

and return the fresh snapshot.

Do not run the VRR tweak immediately. Preserve the current startup-applied behavior.

---

## 10. `GetRuntimeInfo` in CH-RTF-5

CH-RTF-6 needs read-only `GetRuntimeInfo` to be functional when the pipe server is added.

CH-RTF-5 may therefore provide the current truthful runtime metadata now, even though no transport exposes it yet.

For the current application, before `--managed` exists:

```text
applicationVersion      = existing ClawHUD version constant
minimumProtocolVersion  = 1
maximumProtocolVersion  = 1
launchMode              = Standalone
runtimeState             = Ready while dispatch is accepting
```

When shutdown has started, new submissions should fail as `ShuttingDown`; there is no requirement to keep accepting `GetRuntimeInfo` merely to report `ShuttingDown` after the bridge has stopped.

`WireRuntimeState::Starting` is reserved for later lifecycle work and does not need artificial use in this PR.

Do not add `--managed` parsing or persist launch mode in this PR.

CH-RTF-8 will replace the current fixed Standalone source with the real launch-mode state.

---

## 11. `RequestShutdown` remains disabled in this PR

The wire protocol reserves `RequestShutdown`, but **do not connect it to `App::Exit()` yet**.

Reason:

```text
worker sends RequestShutdown
-> main thread calls Exit immediately
-> runtime/pipe infrastructure may disappear
-> worker may not have transmitted the successful response yet
```

The response-before-exit lifecycle must be designed together with the mutation-capable pipe path in CH-RTF-7.

For CH-RTF-5, a validated `RequestShutdown` submitted through the in-process bridge should return a deterministic non-success status, preferably:

```text
ControlStatus::RuntimeUnavailable
```

or an equivalently documented existing v1 status.

Do not add another wire status solely for this temporary implementation stage.

Do not call `PostQuitMessage`, `App::Exit()`, or destroy the runtime window from a Control request in this PR.

---

## 12. Bridge startup

The bridge should not accept requests until the runtime message window exists and normal runtime initialization has reached a safe point.

A suitable current order is:

```text
AcquireSingleInstance
CheckForUpdates
hardware check
ApplyStartupRegistration
RuntimeMessageWindow::Create
TrayIcon::Create
bind/start telemetry + game-session sources
initialize PresentMon provider
start foreground tracking
restore HUD if enabled
start tweak coordinator
start/bind RuntimeControlDispatchBridge
ProcessMessages
```

Starting it immediately before entering the normal message loop is acceptable for CH-RTF-5.

This means every accepted request can be delivered to a live runtime HWND and processed by the main thread.

Do not start a transport thread here; there is no pipe in this PR.

---

## 13. Shutdown and cancellation — mandatory

This is the most important lifetime rule in CH-RTF-5.

Current `App::Exit()` does:

```text
exiting_ = true
StopRuntimeSources()
settings_.reset()
tray_.Destroy()
runtimeMessageWindow_.Destroy()
PostQuitMessage(0)
```

and the destructor also calls `StopRuntimeSources()`.

Integrate bridge shutdown so it occurs **before the runtime message window is destroyed**.

A suitable structure is:

```text
StopRuntimeSources()
-> RuntimeControlDispatchBridge::Stop()
-> cancel/unblock pending requests
-> stop remaining runtime sources
...

then later
-> RuntimeMessageWindow::Destroy()
```

Equivalent ordering is acceptable as long as these invariants hold:

1. Once shutdown starts, `Dispatch()` no longer accepts new queued work.
2. A worker already waiting is completed with `ControlStatus::ShuttingDown` (or a documented deterministic shutdown failure).
3. No waiter depends on receiving another window message after the runtime HWND is destroyed.
4. `Stop()` may be called more than once safely.
5. A queued request that loses its `PostMessage` wake-up because the HWND is unavailable is completed/fails locally and does not wait forever.
6. No `shared_ptr`/pending request remains retained after shutdown completes.

Do not solve this by sleeping or polling.

---

## 14. `PostMessage` failure behavior

`PostMessageW()` can fail if the HWND is already invalid or teardown is underway.

Submission must handle this synchronously.

Required behavior:

```text
enqueue
-> PostMessage fails
-> mark/remove/cancel the pending request
-> complete waiter with RuntimeUnavailable or ShuttingDown
-> no indefinite wait
```

A later harmless wake message for a request already completed/cancelled may be ignored.

Do not assume `PostMessageW()` always succeeds just because the HWND was valid immediately before the call.

This is a normal process-lifecycle case, not a theoretical race.

---

## 15. Main-thread reentrancy / self-dispatch

The normal intended caller is a background transport worker.

However, the bridge must not deadlock if a test or future in-process caller submits from the main thread.

Choose one explicit policy:

### Preferred

If submission occurs on the registered main thread:

```text
execute the validated request synchronously
through the same request-handler path
return the response directly
```

This keeps one semantic implementation and prevents self-wait deadlock.

### Also acceptable

Reject main-thread submission deterministically if the API clearly documents it and tests prove there is no deadlock.

Do not silently enqueue and wait on the same main thread.

---

## 16. Keep transport out of this PR

Explicitly do **not** add:

```text
CreateNamedPipeW
ConnectNamedPipe
CallNamedPipe
WaitNamedPipe
PIPE_REJECT_REMOTE_CLIENTS
pipe ACL / security descriptor
pipe listener thread
SteamAddon client
installation discovery
random/discoverable pipe name
```

Those belong to CH-RTF-6.

The producer in this PR is a test/in-process caller only.

---

## 17. Keep protocol framing out of the main-thread bridge

The future pipe worker will perform:

```text
read bytes
-> DecodeControlRequest
-> validated ControlRequest
-> RuntimeControlDispatchBridge::Dispatch
-> ControlResponse
-> EncodeControlResponse
-> write bytes
```

CH-RTF-5 should operate on the typed request/response DTOs.

Do not pass raw byte spans into `App` or `RuntimeMessageWindow`.

Do not decode wire frames on the main thread.

---

## 18. Expected App / RuntimeMessageWindow changes

### `RuntimeMessageWindow`

Add only the dedicated Control wake handling needed for this PR.

Conceptually:

```cpp
if (message == kRuntimeControlDispatchMessage)
{
    self->app_.HandleRuntimeControlDispatch();
    return 0;
}
```

Do not refactor its existing F8/power/timer behavior.

### `App`

Add the minimum composition/lifecycle entry points needed, conceptually:

```text
RuntimeControlDispatchBridge member
HandleRuntimeControlDispatch()
GetRuntimeInfoForControl() or equivalent narrow metadata source
bridge Start/Bind near end of Run()
bridge Stop during runtime shutdown
```

Do not expose `HudController` or other domain objects to the bridge.

Do not make `RuntimeMessageWindow` aware of protocol payload semantics.

---

## 19. Focused tests

Add focused tests for the bridge and request handler.

Recommended test target name:

```text
ClawHUD.RuntimeControlDispatchTests
```

Use a fake `IRuntimeControl` that records:

```text
calling thread id
received mutation values
returned RuntimeSettingsSnapshot
requested opacity preview/commit values
```

### Required coverage

#### 19.1 Main-thread execution

```text
worker thread submits SetHudEnabled
-> worker does not call fake runtime control directly
-> main thread handles wake/drain
-> fake runtime control records main-thread id
-> worker receives response
```

#### 19.2 Authoritative snapshot

Have the fake runtime control deliberately normalize/roll back a request.

Example:

```text
request SetStartWithWindows(true)
fake keeps effective state false
response snapshot.startWithWindows == false
```

This proves responses come from post-operation state, not request echo.

#### 19.3 Every semantic enum mapping

Cover all values of:

```text
visibility
alignment
font
background mode
Intel VRR status mapping
```

Do not rely on numeric enum ordinal equality in tests.

#### 19.4 Opacity

Prove:

```text
PreviewHudOpacity(70)
-> semantic call receives 0.70f
-> preview method invoked, not commit

CommitHudOpacity(70)
-> semantic call receives 0.70f
-> commit method invoked, not preview
```

and semantic failure returns `OperationFailed`.

#### 19.5 HUD enable failure

```text
SetHudEnabled returns false
-> ControlStatus::OperationFailed
-> no snapshot payload
```

#### 19.6 Runtime info

Prove current CH-RTF-5 metadata maps to:

```text
protocol 1..1
Standalone
Ready
non-empty application version
```

#### 19.7 RequestShutdown disabled

Prove a RequestShutdown submitted in CH-RTF-5 does not invoke a real exit callback and returns the chosen documented non-success status.

#### 19.8 Shutdown cancellation

```text
worker submits
request queued but not yet handled
bridge Stop()
-> worker unblocks
-> response/failure == ShuttingDown
```

No hang.

#### 19.9 Submission after stop

```text
Stop()
-> Dispatch(new request)
-> immediate ShuttingDown
-> no queue growth
-> no PostMessage dependency
```

#### 19.10 PostMessage/wake failure

Use an injectable wake callback or a controlled invalid HWND seam if needed.

Prove the waiter is completed and does not hang when wake delivery fails.

#### 19.11 Main-thread self-dispatch

Prove the chosen policy cannot deadlock.

---

## 20. Existing regression verification

Run the repository normal Debug and Release validation.

At minimum preserve the full existing CTest baseline, including:

```text
ClawHUD.ControlProtocolTests
HUD model/settings tests
HUD presentation contract tests
Intel VRR tests
runtime/game-session tests
```

Do not weaken or delete existing tests.

CH-RTF-5 must not modify HUD rendering output or presentation behavior.

---

## 21. HUD / VRR safety contract — non-negotiable

This PR must not modify, replace, weaken, or work around:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- existing `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirement;
- existing Presentation API / DirectComposition production path;
- premultiplied-alpha presentation behavior.

Do not add synchronization to the HUD presentation path.

Do not call presentation methods from the background producer.

All HUD state mutations must continue through the existing `IRuntimeControl` / App / HudController path on the main thread.

`Background Opacity` remains background-only.

---

## 22. Explicitly out of scope

Do not implement any of the following in CH-RTF-5:

```text
Named Pipe server/client
pipe ACL/security
SteamAddon integration
--managed command-line parsing
Managed launch composition
conditional tray creation
process ownership / Job Object
Addon crash/restart handling
update-mode restart policy
new standalone Settings frontend
WinUI 3 / WPF / Web UI
legacy Win32 Settings removal
Control event subscription / StateChanged bus
telemetry streaming over Control IPC
EC helper sharing
PresentMon redesign
HUD presentation changes
F8 removal
actual RequestShutdown process exit
```

Do not add support for hypothetical future protocol operations beyond v1.

---

## 23. Acceptance criteria

This PR is complete only when all of the following are true:

1. A background-safe typed `ControlRequest` submission API exists.
2. Submission wakes `RuntimeMessageWindow` with a dedicated non-conflicting App message.
3. `IRuntimeControl` execution occurs on the ClawHUD main thread.
4. No transport/background thread directly mutates runtime/HUD/game-session state.
5. Wire enum -> semantic enum mapping is explicit.
6. Semantic snapshot -> wire snapshot mapping is explicit.
7. Intel VRR statuses are mapped explicitly.
8. Opacity preview and commit remain separate semantic calls.
9. Successful mutations return fresh authoritative snapshots.
10. `SetHudEnabled` / opacity semantic failures return `OperationFailed`.
11. `GetRuntimeInfo` returns truthful current Standalone/Ready v1 metadata without adding Managed mode.
12. `RequestShutdown` does not terminate the process in this PR.
13. Pending requests are unblocked when dispatch shutdown starts.
14. Requests submitted after shutdown fail deterministically.
15. A failed runtime-window wake does not leave a waiting producer blocked.
16. Main-thread self-submission cannot deadlock.
17. No Named Pipe APIs are introduced.
18. No HUD presentation/VRR contract file requires behavioral modification.
19. Existing Settings/tray/F8/suspend/resume behavior is unchanged.
20. New focused dispatch tests pass.
21. Existing full CI/test baseline remains green.

---

## 24. Handoff to CH-RTF-6

After CH-RTF-5, the architecture should be:

```text
legacy Win32 Settings
    -> IRuntimeControl directly (same process, existing behavior)

future transport producer
    -> typed validated ControlRequest
    -> RuntimeControlDispatchBridge
    -> RuntimeMessageWindow wake
    -> main thread
    -> IRuntimeControl
    -> ControlResponse
```

CH-RTF-6 can then add only the secure read-only Named Pipe transport:

```text
pipe bytes
-> CH-RTF-4 decoder
-> CH-RTF-5 dispatch bridge
-> response
-> CH-RTF-4 encoder
-> pipe bytes
```

with the initial external command surface restricted to:

```text
GetRuntimeInfo
GetSettingsSnapshot
```

That separation is intentional: CH-RTF-6 should review transport/security, not runtime threading semantics again.
