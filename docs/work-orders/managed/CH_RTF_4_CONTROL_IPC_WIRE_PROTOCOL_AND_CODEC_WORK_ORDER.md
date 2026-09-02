# CH-RTF-4 — Control IPC Wire Protocol and Codec Work Order

> **Repository:** `onehoon/ClawHUD`  
> **Series:** ClawHUD Runtime / Frontend Separation  
> **Architecture source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_ARCHITECTURE_2026-09-02.md`  
> **PR plan source:** `docs/work-orders/managed/CLAW_HUD_RUNTIME_FRONTEND_SEPARATION_PR_PLAN_2026-09-02.md`  
> **Previous PRs:** #209 CH-RTF-1 Runtime Message Window Extraction, #210 CH-RTF-2 Tray Shell Callback Decoupling, #211 CH-RTF-3 Runtime-Control Contract + Legacy Settings Migration  
> **Analyzed main HEAD:** `14883da8174f564c7aebff21f58cfc69ac5a6607`  
> **Scope:** Define and test the versioned, transport-independent ClawHUD Control IPC wire protocol and codecs only  
> **Status:** Ready for implementation

---

## 1. Objective

Define the byte-level contract that future ClawHUD control clients and the ClawHUD runtime will use across a local IPC transport.

This PR must stop at the protocol/codec boundary.

It must **not** create a Named Pipe, start a server thread, dispatch requests to `App`, mutate runtime state, add `--managed`, or change current Settings behavior.

The intended sequence is:

```text
CH-RTF-3
    in-process semantic boundary
    IRuntimeControl / RuntimeSettingsSnapshot

CH-RTF-4   <-- this PR
    stable versioned bytes-on-the-wire contract
    + pure encode/decode/validation tests

CH-RTF-5
    decoded request -> main-thread runtime-control dispatch

CH-RTF-6
    secure read-only Named Pipe transport
```

The critical result of this PR is:

> A client implemented independently from ClawHUD — including a future SteamAddon client written in a different language — must be able to implement the protocol from the documented fixed field definitions without depending on native C++ object layout.

---

## 2. Current production baseline after PR #211

PR #211 introduced:

```cpp
clawhud::IRuntimeControl
clawhud::RuntimeSettingsSnapshot
```

The semantic control interface currently exposes:

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

This is deliberately an **in-process C++ semantic API**, not a wire ABI.

Current semantic domain types include:

```text
HudVisibilityMode
- Always
- InGameOnly

HudAlignment
- Left
- Center
- Right

HudFont
- Unispace
- SegoeUiVariable

HudBackgroundMode
- FullWidth
- ContentWidth

IntelVrrRunStatus
- Disabled
- Unavailable
- UnsupportedPanel
- AmbiguousDisplay
- AlreadyCorrect
- SkippedUserProfile
- Applied
- ApplyFailed
- VerificationFailed
```

Do not serialize these native enum representations directly.

Their current ordinal values are an implementation detail and are not the IPC contract.

---

## 3. Important distinction from the existing EC helper protocol

The repository already has `src/shared/EcHelperProtocol.h`, which uses packed native structs for a tightly controlled internal helper pair.

Do **not** copy that ABI strategy for the public ClawHUD Control IPC.

The Control protocol has different requirements:

```text
ClawHUD runtime
    <-> future standalone frontend
    <-> independently installed SteamAddon
    <-> potentially different implementation language
    <-> versions that may not update at exactly the same instant
```

Therefore the Control protocol must use explicit byte encoding and decoding.

Do not send or receive the raw memory representation of:

```text
C++ structs
std::string / std::wstring
std::optional
std::variant
HudLayoutOptions
RuntimeSettingsSnapshot
IntelVrrRunResult
native enums
bool
float
pointers / HWND / HANDLE
```

across the wire.

---

## 4. Recommended source layout

Use shared, transport-neutral files so future pipe/server/client code can consume the same codec without introducing runtime dependencies.

Recommended files:

```text
src/shared/ClawHudControlProtocol.h
src/shared/ClawHudControlCodec.h
src/shared/ClawHudControlCodec.cpp

tests/ClawHudControlCodecTests.cpp
```

Add the test target to:

```text
cmake/ClawHUDTests.cmake
```

The codec must not depend on:

```text
App.h
RuntimeControl.h
SettingsWindow
RuntimeMessageWindow
TrayIcon
HudController
HudPresentation
PresentMon
EC telemetry
game detection
Velopack
Named Pipe APIs
```

It may use only standard-library facilities and the protocol declarations required for pure byte processing.

Do not add Windows IPC dependencies merely for this PR.

---

## 5. Protocol v1 frame format

Use a fixed v1 frame header with explicit little-endian integer encoding.

Recommended header:

```text
Offset  Size  Field
0       4     magic bytes: ASCII "CHUD"
4       2     protocolVersion
6       2     headerSize
8       2     messageKind
10      2     operation
12      4     requestId
16      4     status
20      4     payloadSize

Total header size: 24 bytes
```

Use constants equivalent to:

```text
Magic                  = "CHUD"
ProtocolVersion         = 1
HeaderSize              = 24
MaximumPayloadBytes     = 16 KiB
MaximumFrameBytes       = HeaderSize + MaximumPayloadBytes
```

The exact C++ constant names may vary, but the numeric/on-wire contract must be explicit and tested.

### 5.1 Endianness

All multi-byte integer fields are encoded **little-endian**.

Do not rely on host endianness or `reinterpret_cast` of a packed header.

Provide small helpers such as conceptually:

```text
WriteU16LE
WriteU32LE
WriteI32LE
ReadU16LE
ReadU32LE
ReadI32LE
```

with bounds checking.

### 5.2 Message kind

Use explicit fixed values:

```text
Request  = 1
Response = 2
```

Zero is invalid.

Unknown values must be rejected.

### 5.3 Request ID

`requestId` correlates one response with one request.

Rules:

- request IDs are client-generated;
- zero is reserved/invalid for normal request/response traffic;
- a response must preserve the request ID of its request;
- this PR only defines/validates the field; no concurrency policy is required yet.

### 5.4 Status field

Requests must carry:

```text
status = 0
```

Responses use an explicit fixed status enum.

Recommended v1 values:

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

Do not encode HRESULTs, Win32 error codes, C++ exceptions, or implementation-specific error values as the primary protocol status.

A later implementation may log internal detail locally while returning one stable protocol status externally.

---

## 6. Operation IDs

Define explicit fixed wire operation values independent of C++ enum ordinals.

Recommended v1 values:

```text
GetRuntimeInfo                = 1
GetSettingsSnapshot           = 2

SetStartWithWindows          = 10
SetHudEnabled                = 11
SetHudVisibilityMode         = 12
SetHudSizeOffset             = 13
SetHudFont                   = 14
SetHudAlignment              = 15
SetHudBackgroundMode         = 16
PreviewHudOpacity            = 17
CommitHudOpacity             = 18
SetIntelVrrRangeFixEnabled   = 19
RequestShutdown              = 20
```

Do not create generic `SetProperty`, string command names, key/value maps, or free-form JSON commands.

The protocol should remain a small typed operation set.

Unknown operation IDs must decode as an error, not silently map to a default.

---

## 7. Fixed wire enum values

Create protocol-specific wire enums with explicit underlying integer values.

Do not reuse native enum ordinals by cast.

### 7.1 HUD visibility

```text
Always       = 1
InGameOnly   = 2
```

### 7.2 HUD alignment

```text
Left         = 1
Center       = 2
Right        = 3
```

### 7.3 HUD font

```text
Unispace          = 1
SegoeUiVariable   = 2
```

### 7.4 HUD background mode

```text
FullWidth     = 1
ContentWidth  = 2
```

### 7.5 Intel VRR last-result status

```text
Disabled             = 1
Unavailable          = 2
UnsupportedPanel     = 3
AmbiguousDisplay     = 4
AlreadyCorrect       = 5
SkippedUserProfile   = 6
Applied              = 7
ApplyFailed          = 8
VerificationFailed   = 9
```

Zero is invalid unless a specific optional-presence field says no result exists.

CH-RTF-5 will perform explicit semantic mapping between these wire enums and the existing `Hud*` / `IntelVrrRunStatus` types.

Do not add that runtime mapping in this PR.

---

## 8. Request payload definitions

Payloads must be operation-specific and strictly length-validated.

### 8.1 Empty requests

These operations carry zero payload bytes:

```text
GetRuntimeInfo
GetSettingsSnapshot
RequestShutdown
```

A non-zero payload for an operation defined as empty must be rejected as `InvalidPayload`.

### 8.2 Boolean setters

These operations carry exactly one byte:

```text
SetStartWithWindows
SetHudEnabled
SetIntelVrrRangeFixEnabled
```

Encoding:

```text
0 = false
1 = true
```

Any other byte is invalid.

Do not serialize native C++ `bool`.

### 8.3 Enum setters

These operations carry exactly one byte containing the explicit wire enum value:

```text
SetHudVisibilityMode
SetHudFont
SetHudAlignment
SetHudBackgroundMode
```

Unknown enum values must be rejected before dispatch.

### 8.4 HUD size

`SetHudSizeOffset` carries exactly one signed 32-bit little-endian integer.

The codec should validate against the currently supported product range:

```text
-2 through +2
```

Do not rely on later HudController clamping to accept malformed external input.

### 8.5 HUD opacity

Both:

```text
PreviewHudOpacity
CommitHudOpacity
```

carry an unsigned 16-bit integer opacity percentage rather than a binary floating-point value.

Valid values are the current product values:

```text
50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100
```

This preserves the existing 50–100%, 5% step UI/product semantics while avoiding cross-language float encoding ambiguity.

CH-RTF-5 may convert this validated percentage to the semantic fraction expected by `IRuntimeControl`.

Do not change renderer opacity behavior in this PR.

---

## 9. Response payload definitions

The protocol must support **authoritative post-operation state**.

A successful mutation must not be designed around an `OK`-only response, because current runtime operations can reject, roll back, or normalize requested state.

### 9.1 `GetRuntimeInfo` response

Define a transport-neutral payload containing at least:

```text
applicationVersion      UTF-8 length-prefixed string
minimumProtocolVersion  u16
maximumProtocolVersion  u16
launchMode              u8
runtimeState            u8
```

Wire launch mode values:

```text
Standalone = 1
Managed    = 2
```

Wire runtime state values:

```text
Starting      = 1
Ready         = 2
ShuttingDown  = 3
```

CH-RTF-4 only defines and tests the payload.

Do not add `--managed` or runtime-mode state to `App` in this PR.

Until later PRs use these fields, codec tests can construct synthetic values.

### 9.2 Settings snapshot response

Define a wire settings snapshot that can represent the current `RuntimeSettingsSnapshot` without copying its C++ memory layout.

Required v1 fields:

```text
startWithWindows             bool byte
hudEnabled                   bool byte
hudSizeOffset                i32
hudFont                      wire enum byte
visibilityMode               wire enum byte
alignment                    wire enum byte
backgroundMode               wire enum byte
backgroundOpacityPercent     u16
intelVrrRangeFixEnabled      bool byte
hasIntelVrrLastResult        bool byte
```

If `hasIntelVrrLastResult == 1`, append:

```text
intelVrrStatus   wire enum byte
panelName        length-prefixed UTF-8
rangeBefore      length-prefixed UTF-8
rangeAfter       length-prefixed UTF-8
message          length-prefixed UTF-8
timestampUtc     length-prefixed UTF-8
```

If `hasIntelVrrLastResult == 0`, no VRR-result fields follow.

### 9.3 Mutation responses

For successful setting mutations, design v1 so the response can carry the same authoritative settings snapshot payload used by `GetSettingsSnapshot`.

This will allow later behavior such as:

```text
client requests StartWithWindows = ON
-> runtime fails shortcut creation and rolls back
-> response snapshot says StartWithWindows = OFF
-> frontend renders authoritative OFF state
```

`RequestShutdown` may use an empty successful response because the process is transitioning out and no settings snapshot is necessary.

### 9.4 Error responses

For a non-`Ok` status, payload may be empty in protocol v1.

Do not require arbitrary error strings on the wire for this initial protocol.

This keeps error behavior deterministic and avoids exposing implementation details.

---

## 10. UTF-8 string encoding

Use UTF-8 for wire strings.

Each string must use a length prefix, for example:

```text
u16 byteLength
byteLength bytes of UTF-8 data
```

Rules:

- length counts bytes, not characters;
- no terminating NUL is sent;
- embedded NUL bytes should be rejected;
- malformed UTF-8 must be rejected;
- the decoder must reject a declared length that exceeds remaining payload bytes;
- define a conservative per-string maximum, e.g. 4096 bytes;
- total frame size remains bounded by `MaximumPayloadBytes`.

Do not use native `wchar_t` encoding or UTF-16 on this wire protocol.

The current Intel VRR result store already represents these result strings as `std::string`; nevertheless, CH-RTF-4 is codec-only and must not read the store.

---

## 11. Codec API expectations

The exact C++ API can vary, but it must expose clear pure operations for:

```text
encode request
encode response
decode request
decode response
```

A reasonable shape is conceptually:

```cpp
DecodeResult DecodeControlRequest(std::span<const std::uint8_t> bytes);
DecodeResult DecodeControlResponse(std::span<const std::uint8_t> bytes);

std::vector<std::uint8_t> EncodeControlRequest(const ControlRequest& request);
std::vector<std::uint8_t> EncodeControlResponse(const ControlResponse& response);
```

Use typed protocol DTOs / variants internally if useful.

Requirements:

1. Decoding must never read past the provided span.
2. Frame size must be checked before allocating based on wire values.
3. `payloadSize` must exactly match the bytes remaining after the header.
4. Trailing unconsumed bytes are invalid.
5. Header validation happens before payload decode.
6. Operation-specific payload length/value validation happens before returning a valid decoded request.
7. Encode functions must refuse invalid DTO values rather than emitting malformed frames.
8. The codec must not throw for ordinary malformed remote input if the repository style can reasonably return a structured decode error instead.

Do not create a generic serialization framework.

Small explicit reader/writer helpers are preferred.

---

## 12. Protocol validation order

Use deterministic validation order so future server behavior is predictable.

Recommended order:

```text
1. minimum bytes for fixed header
2. magic
3. protocolVersion
4. headerSize
5. messageKind
6. requestId validity
7. request status field must be zero
8. payloadSize <= MaximumPayloadBytes
9. exact frame length == headerSize + payloadSize
10. known operation
11. operation-specific exact payload length
12. operation-specific enum/range/string validation
```

Do not attempt partial recovery from a malformed frame.

One malformed frame is one failed request.

Connection-lifetime policy belongs to CH-RTF-6, not this PR.

---

## 13. Versioning policy

Protocol v1 is the initial stable contract.

Rules:

- explicit protocol version is mandatory in every frame;
- incompatible version is rejected explicitly;
- wire enum numeric values already assigned in v1 must not be renumbered later;
- operation IDs already assigned in v1 must not be reused for a different meaning;
- additive operations/fields are preferred for future evolution;
- do not infer compatibility from ClawHUD application version alone;
- `GetRuntimeInfo` exposes supported protocol range so independently updated clients can make compatibility decisions later.

Do not implement multi-version fallback machinery in this PR.

Only define v1 cleanly.

---

## 14. Required tests

Add a dedicated pure test target, preferably:

```text
ClawHUD.ControlProtocolTests
```

or a similarly clear name.

The tests must not require a desktop session, supported Claw hardware, PresentMon, EC access, admin rights, or a Named Pipe.

### 14.1 Header/frame round-trip

Cover:

```text
valid request header + empty payload
valid response header + payload
requestId preserved
operation preserved
payload byte count preserved
```

### 14.2 Header rejection

Cover at least:

```text
0-byte input
truncated header
wrong magic
unsupported version
wrong headerSize
invalid messageKind
requestId = 0
request carrying non-zero status
payloadSize larger than maximum
payloadSize smaller than actual bytes
payloadSize larger than actual bytes
trailing bytes after declared frame
```

### 14.3 Operation rejection

Cover:

```text
unknown operation
non-empty payload for empty request
wrong payload size for bool / enum / i32 / opacity operations
```

### 14.4 Value validation

Cover:

```text
bool = 0 valid
bool = 1 valid
bool = 2 invalid

all defined visibility values valid
unknown visibility value invalid

all defined font values valid
unknown font value invalid

all defined alignment values valid
unknown alignment value invalid

all defined background-mode values valid
unknown background value invalid

HUD size -2 and +2 valid
HUD size outside range invalid

opacity 50 and 100 valid
opacity valid 5% steps round-trip
opacity 49 invalid
opacity 101 invalid
opacity 53 invalid
```

### 14.5 Settings snapshot round-trip

Construct a complete synthetic snapshot and prove exact encode/decode for:

```text
all basic fields
opacity percent
no Intel VRR result
Intel VRR result present
all Intel VRR result strings
all Intel VRR status values
```

### 14.6 String rejection

Cover:

```text
truncated length prefix
length greater than remaining payload
string greater than maximum allowed bytes
embedded NUL
malformed UTF-8
aggregate payload greater than MaximumPayloadBytes
```

### 14.7 Runtime info round-trip

Cover:

```text
application version string
protocol min/max
Standalone / Managed values
Starting / Ready / ShuttingDown values
invalid launch mode
invalid runtime state
```

---

## 15. CMake integration

Add the new pure codec test target to `cmake/ClawHUDTests.cmake` following the repository's existing small test-target style.

Conceptually:

```cmake
add_executable(ClawHUD.ControlProtocolTests
    tests/ClawHudControlCodecTests.cpp
    src/shared/ClawHudControlCodec.cpp)

target_compile_features(ClawHUD.ControlProtocolTests PRIVATE cxx_std_20)
target_include_directories(ClawHUD.ControlProtocolTests PRIVATE src/shared)
set_target_properties(ClawHUD.ControlProtocolTests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME ClawHUD.ControlProtocolTests COMMAND ClawHUD.ControlProtocolTests)
```

Adjust exact source names to the implementation.

No Windows system library should be necessary for the codec itself.

Do not add the codec to production `ClawHUD.exe` yet unless compilation/link organization genuinely requires it. CH-RTF-5/6 will consume it in production.

A header-only implementation is acceptable only if it remains clearer than a `.cpp`; do not force header-only code merely to reduce file count.

---

## 16. Explicit out of scope

Do not implement any of the following in CH-RTF-4:

```text
CreateNamedPipe / ConnectNamedPipe
pipe names
pipe ACL / security descriptor
PIPE_REJECT_REMOTE_CLIENTS
server or client threads
connection lifecycle
RuntimeMessageWindow dispatch
PostMessage-based runtime-control dispatch
App mutations from decoded requests
IRuntimeControl changes unless a compile-only protocol-independent fix is essential
GetRuntimeInfo runtime implementation
--managed parsing
Standalone / Managed composition
process ownership / Job Objects
SteamAddon code
installation discovery
update/restart lifecycle
Settings UI changes
F8 changes
HUD renderer/presentation changes
PresentMon changes
EC changes
```

Do not create a fake Named Pipe just to test the codec.

This PR is intentionally transport-free.

---

## 17. HUD / VRR safety contract — non-negotiable

This PR should have no reason to touch any HUD presentation implementation.

Do not modify, replace, weaken, or work around:

- HUD `windowExStyle`;
- `WS_EX_TRANSPARENT`;
- `WS_EX_NOACTIVATE`;
- `WS_EX_TOPMOST`;
- existing `WS_EX_LAYERED` behavior;
- `WM_NCHITTEST -> HTTRANSPARENT`;
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`;
- `ProductionHudPresentationContract()`;
- independent-flip requirement;
- production Presentation API / DirectComposition path;
- premultiplied-alpha presentation behavior.

`Background Opacity` still means **background only**.

The wire protocol's opacity percentage is only a control value. It must not introduce any new window-wide/visual-wide opacity concept.

---

## 18. Completion criteria

CH-RTF-4 is complete when all of the following are true:

```text
[ ] protocol v1 constants and fixed numeric values are defined
[ ] no raw native C++ memory layout is used as the wire format
[ ] explicit little-endian encode/decode exists
[ ] frame size is bounded
[ ] UTF-8 strings are length-bounded and validated
[ ] all required operation IDs are defined
[ ] all required wire enums have explicit stable values
[ ] request payloads have exact length/value validation
[ ] GetRuntimeInfo response payload is defined
[ ] authoritative Settings snapshot response payload is defined
[ ] successful mutation responses can carry the authoritative Settings snapshot
[ ] malformed/unknown frames fail deterministically
[ ] dedicated pure protocol/codec tests cover success and failure cases
[ ] normal Debug/Release build and CTest baseline remain green
[ ] no Named Pipe/server/client/runtime-dispatch code was added
[ ] no Settings UX changed
[ ] no HUD/VRR presentation contract file or behavior changed
```

---

## 19. Handoff to CH-RTF-5

After this PR the architecture should be:

```text
Frontend / future IPC client
        |
        | protocol v1 bytes
        v
ClawHudControlCodec
        |
        | validated typed wire request
        v
        X   no runtime connection yet

IRuntimeControl
        ^
        |
App ----+
```

CH-RTF-5 can then add the missing bridge:

```text
validated Control request
    -> queue/post to RuntimeMessageWindow
    -> main-thread dispatch
    -> IRuntimeControl
    -> authoritative response model
    -> encode through the already-tested codec
```

Do not pull any Named Pipe concerns backward into CH-RTF-5 unless required by its later work order.
