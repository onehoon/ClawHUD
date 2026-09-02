#pragma once

// ClawHUD Control IPC — protocol v1 declarations.
//
// This is the byte-level contract between the ClawHUD runtime and any local
// control client (a future standalone frontend, an independently installed
// SteamAddon, possibly in another language). It is transport-independent:
// CH-RTF-4 defines and tests the wire format only. No Named Pipe, no server,
// no runtime dispatch lives here.
//
// Every multi-byte integer on the wire is little-endian and explicitly
// encoded (see ClawHudControlCodec). Native C++ struct/enum/std::string
// layouts are never sent raw.

#include <cstdint>
#include <string>
#include <optional>

namespace clawhud::control
{
// ---- Frame constants -------------------------------------------------------

// Fixed 24-byte frame header:
//   off 0  u8[4]  magic  = 'C''H''U''D'
//   off 4  u16    protocolVersion
//   off 6  u16    headerSize (== kHeaderSize)
//   off 8  u16    messageKind
//   off 10 u16    operation
//   off 12 u32    requestId
//   off 16 u32    status
//   off 20 u32    payloadSize
inline constexpr std::uint8_t kMagic[4] = {'C', 'H', 'U', 'D'};
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::uint16_t kHeaderSize = 24;
inline constexpr std::uint32_t kMaxPayloadBytes = 16u * 1024u;
inline constexpr std::uint32_t kMaxFrameBytes = kHeaderSize + kMaxPayloadBytes;

// Per-string byte cap; the aggregate payload is still bounded by
// kMaxPayloadBytes.
inline constexpr std::uint16_t kMaxStringBytes = 4096;

// ---- Enumerations (explicit stable wire values) --------------------------

enum class MessageKind : std::uint16_t
{
    Request = 1,
    Response = 2,
};

enum class Operation : std::uint16_t
{
    GetRuntimeInfo = 1,
    GetSettingsSnapshot = 2,

    SetStartWithWindows = 10,
    SetHudEnabled = 11,
    SetHudVisibilityMode = 12,
    SetHudSizeOffset = 13,
    SetHudFont = 14,
    SetHudAlignment = 15,
    SetHudBackgroundMode = 16,
    PreviewHudOpacity = 17,
    CommitHudOpacity = 18,
    SetIntelVrrRangeFixEnabled = 19,
    RequestShutdown = 20,
};

enum class ControlStatus : std::uint32_t
{
    Ok = 0,
    InvalidFrame = 1,
    UnsupportedVersion = 2,
    UnknownOperation = 3,
    InvalidPayload = 4,
    InvalidValue = 5,
    RuntimeUnavailable = 6,
    OperationFailed = 7,
    ShuttingDown = 8,
};

enum class WireVisibilityMode : std::uint8_t
{
    Always = 1,
    InGameOnly = 2,
};

enum class WireAlignment : std::uint8_t
{
    Left = 1,
    Center = 2,
    Right = 3,
};

enum class WireFont : std::uint8_t
{
    Unispace = 1,
    SegoeUiVariable = 2,
};

enum class WireBackgroundMode : std::uint8_t
{
    FullWidth = 1,
    ContentWidth = 2,
};

enum class WireIntelVrrStatus : std::uint8_t
{
    Disabled = 1,
    Unavailable = 2,
    UnsupportedPanel = 3,
    AmbiguousDisplay = 4,
    AlreadyCorrect = 5,
    SkippedUserProfile = 6,
    Applied = 7,
    ApplyFailed = 8,
    VerificationFailed = 9,
};

enum class WireLaunchMode : std::uint8_t
{
    Standalone = 1,
    Managed = 2,
};

enum class WireRuntimeState : std::uint8_t
{
    Starting = 1,
    Ready = 2,
    ShuttingDown = 3,
};

// ---- Product value bounds (shared by codec validation) -------------------

inline constexpr std::int32_t kMinHudSizeOffset = -2;
inline constexpr std::int32_t kMaxHudSizeOffset = 2;
inline constexpr std::uint16_t kMinOpacityPercent = 50;
inline constexpr std::uint16_t kMaxOpacityPercent = 100;
inline constexpr std::uint16_t kOpacityStepPercent = 5;

// ---- Payload DTOs -------------------------------------------------------

// A decoded request. Only the field relevant to `operation` is meaningful;
// the others stay default. Empty-payload operations use none of them.
struct ControlRequest
{
    Operation operation{};
    std::uint32_t requestId{};

    bool flag{};                 // Set{StartWithWindows,HudEnabled,IntelVrrRangeFixEnabled}
    std::uint8_t wireEnum{};     // Set{HudVisibilityMode,HudFont,HudAlignment,HudBackgroundMode}
    std::int32_t sizeOffset{};   // SetHudSizeOffset
    std::uint16_t opacityPercent{}; // Preview/CommitHudOpacity
};

struct WireIntelVrrResult
{
    std::uint8_t status{};       // WireIntelVrrStatus value
    std::string panelName;
    std::string rangeBefore;
    std::string rangeAfter;
    std::string message;
    std::string timestampUtc;
};

struct WireSettingsSnapshot
{
    bool startWithWindows{};
    bool hudEnabled{};
    std::int32_t hudSizeOffset{};
    std::uint8_t hudFont{};             // WireFont value
    std::uint8_t visibilityMode{};      // WireVisibilityMode value
    std::uint8_t alignment{};           // WireAlignment value
    std::uint8_t backgroundMode{};      // WireBackgroundMode value
    std::uint16_t backgroundOpacityPercent{};
    bool intelVrrRangeFixEnabled{};
    std::optional<WireIntelVrrResult> intelVrrLastResult;
};

struct WireRuntimeInfo
{
    std::string applicationVersion;
    std::uint16_t minimumProtocolVersion{};
    std::uint16_t maximumProtocolVersion{};
    std::uint8_t launchMode{};    // WireLaunchMode value
    std::uint8_t runtimeState{};  // WireRuntimeState value
};

// A decoded response. `operationId` is the raw wire operation echoed from the
// request, so an error response (e.g. UnknownOperation from a version-skewed
// client) can still be correlated even when the operation is not a known v1
// Operation. On a non-Ok status both optionals are empty. On Ok (which always
// implies a known operation):
//   GetRuntimeInfo      -> runtimeInfo set
//   GetSettingsSnapshot -> snapshot set
//   Set*/Preview/Commit -> snapshot set (authoritative post-mutation state)
//   RequestShutdown     -> both empty (empty payload)
struct ControlResponse
{
    std::uint16_t operationId{};
    std::uint32_t requestId{};
    ControlStatus status{};

    std::optional<WireRuntimeInfo> runtimeInfo;
    std::optional<WireSettingsSnapshot> snapshot;
};
}
