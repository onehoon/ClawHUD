namespace ClawHUD.Settings.Protocol;

// C# mirror of the authoritative native ClawHUD Control IPC protocol v1
// (src/shared/ClawHudControlProtocol.h + ClawHudControlCodec.cpp). The frontend
// is an independent implementation: it never marshals native struct/enum layout,
// only the explicit little-endian wire values reproduced here.

public static class ControlProtocol
{
    internal static readonly byte[] Magic = "CHUD"u8.ToArray();
    internal const ushort ProtocolVersion = 1;
    internal const ushort HeaderSize = 24;
    internal const int MaxPayloadBytes = 16 * 1024;
    internal const int MaxFrameBytes = HeaderSize + MaxPayloadBytes;
    internal const int MaxStringBytes = 4096;

    // Product value bounds — decoded snapshots are validated against these even
    // though PR2 never sends a mutation value.
    internal const int MinHudSizeOffset = -2;
    internal const int MaxHudSizeOffset = 2;
    internal const int MinOpacityPercent = 50;
    internal const int MaxOpacityPercent = 100;
    internal const int OpacityStepPercent = 5;
}

public enum ControlMessageKind : ushort
{
    Request = 1,
    Response = 2,
}

public enum ControlOperation : ushort
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
}

public enum ControlStatus : uint
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
}

public enum WireVisibilityMode : byte
{
    Always = 1,
    InGameOnly = 2,
}

public enum WireAlignment : byte
{
    Left = 1,
    Center = 2,
    Right = 3,
}

public enum WireFont : byte
{
    Unispace = 1,
    SegoeUiVariable = 2,
}

public enum WireBackgroundMode : byte
{
    FullWidth = 1,
    ContentWidth = 2,
}

public enum WireIntelVrrStatus : byte
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
}

public enum WireLaunchMode : byte
{
    Standalone = 1,
    Managed = 2,
}

public enum WireRuntimeState : byte
{
    Starting = 1,
    Ready = 2,
    ShuttingDown = 3,
}

/// <summary>
/// A protocol-v1 request to encode. Only the field the operation needs is set;
/// <see cref="ControlCodec.EncodeRequest"/> validates the operation/field pairing
/// and every value bound before it produces a frame.
/// </summary>
public sealed record ControlRequest(
    ControlOperation Operation,
    uint RequestId,
    bool? Flag = null,
    byte? WireEnum = null,
    int? SizeOffset = null);

public sealed record RuntimeInfo(
    string ApplicationVersion,
    ushort MinimumProtocolVersion,
    ushort MaximumProtocolVersion,
    WireLaunchMode LaunchMode,
    WireRuntimeState RuntimeState);

public sealed record IntelVrrResult(
    WireIntelVrrStatus Status,
    string PanelName,
    string RangeBefore,
    string RangeAfter,
    string Message,
    string TimestampUtc);

public sealed record SettingsSnapshot(
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

public static class WireValue
{
    internal static bool IsKnownStatus(uint value) => value <= (uint)ControlStatus.ShuttingDown;

    internal static bool IsVisibilityMode(byte v) => v is >= 1 and <= 2;
    internal static bool IsAlignment(byte v) => v is >= 1 and <= 3;
    internal static bool IsFont(byte v) => v is >= 1 and <= 2;
    internal static bool IsBackgroundMode(byte v) => v is >= 1 and <= 2;
    internal static bool IsIntelVrrStatus(byte v) => v is >= 1 and <= 9;
    internal static bool IsLaunchMode(byte v) => v is >= 1 and <= 2;
    internal static bool IsRuntimeState(byte v) => v is >= 1 and <= 3;

    internal static bool IsHudSizeOffset(int v) =>
        v is >= ControlProtocol.MinHudSizeOffset and <= ControlProtocol.MaxHudSizeOffset;

    internal static bool IsOpacityPercent(ushort v) =>
        v >= ControlProtocol.MinOpacityPercent && v <= ControlProtocol.MaxOpacityPercent &&
        v % ControlProtocol.OpacityStepPercent == 0;
}
