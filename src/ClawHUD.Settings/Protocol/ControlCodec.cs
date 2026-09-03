using System.Buffers.Binary;
using System.Text;

namespace ClawHUD.Settings.Protocol;

internal enum ResponseDecodeOutcome
{
    /// <summary>Frame is a valid, correlated <c>Ok</c> response with a decoded payload.</summary>
    Ok,

    /// <summary>Frame is valid and correlated but carries a non-<c>Ok</c> protocol status.</summary>
    ProtocolError,

    /// <summary>Frame is structurally invalid, uncorrelated, or has an undecodable payload.</summary>
    Malformed,
}

internal sealed record ResponseDecodeResult(
    ResponseDecodeOutcome Outcome,
    ControlStatus Status,
    RuntimeInfo? RuntimeInfo = null,
    SettingsSnapshot? Snapshot = null)
{
    internal static ResponseDecodeResult Malformed { get; } =
        new(ResponseDecodeOutcome.Malformed, ControlStatus.InvalidFrame);

    internal static ResponseDecodeResult ProtocolError(ControlStatus status) =>
        new(ResponseDecodeOutcome.ProtocolError, status);
}

internal static class ControlCodec
{
    private static readonly UTF8Encoding StrictUtf8 = new(encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);

    /// <summary>
    /// Encodes one of the two empty-payload read requests as an exact 24-byte
    /// little-endian v1 frame. Throws for a zero request id or any operation
    /// other than the two reads (mutation encoders are deferred to a later PR).
    /// </summary>
    internal static byte[] EncodeReadRequest(ControlOperation operation, uint requestId)
    {
        if (requestId == 0)
            throw new ArgumentOutOfRangeException(nameof(requestId), "Request id must be non-zero.");
        if (operation is not (ControlOperation.GetRuntimeInfo or ControlOperation.GetSettingsSnapshot))
            throw new ArgumentOutOfRangeException(nameof(operation),
                "PR2 only encodes GetRuntimeInfo / GetSettingsSnapshot requests.");

        var frame = new byte[ControlProtocol.HeaderSize];
        ControlProtocol.Magic.CopyTo(frame.AsSpan(0, 4));
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(4), ControlProtocol.ProtocolVersion);
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(6), ControlProtocol.HeaderSize);
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(8), (ushort)ControlMessageKind.Request);
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(10), (ushort)operation);
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(12), requestId);
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(16), 0); // status
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(20), 0); // payloadSize
        return frame;
    }

    /// <summary>
    /// Strictly decodes a response frame and verifies it correlates to the
    /// request that was sent. Any deviation from the v1 contract yields
    /// <see cref="ResponseDecodeOutcome.Malformed"/>.
    /// </summary>
    internal static ResponseDecodeResult DecodeResponse(ReadOnlySpan<byte> bytes,
        uint expectedRequestId, ControlOperation expectedOperation)
    {
        if (bytes.Length < ControlProtocol.HeaderSize)
            return ResponseDecodeResult.Malformed;
        if (!bytes[..4].SequenceEqual(ControlProtocol.Magic))
            return ResponseDecodeResult.Malformed;

        ushort protocolVersion = BinaryPrimitives.ReadUInt16LittleEndian(bytes[4..]);
        ushort headerSize = BinaryPrimitives.ReadUInt16LittleEndian(bytes[6..]);
        ushort messageKind = BinaryPrimitives.ReadUInt16LittleEndian(bytes[8..]);
        ushort operation = BinaryPrimitives.ReadUInt16LittleEndian(bytes[10..]);
        uint requestId = BinaryPrimitives.ReadUInt32LittleEndian(bytes[12..]);
        uint status = BinaryPrimitives.ReadUInt32LittleEndian(bytes[16..]);
        uint payloadSize = BinaryPrimitives.ReadUInt32LittleEndian(bytes[20..]);

        if (protocolVersion != ControlProtocol.ProtocolVersion) return ResponseDecodeResult.Malformed;
        if (headerSize != ControlProtocol.HeaderSize) return ResponseDecodeResult.Malformed;
        if (messageKind != (ushort)ControlMessageKind.Response) return ResponseDecodeResult.Malformed;
        if (requestId == 0 || requestId != expectedRequestId) return ResponseDecodeResult.Malformed;
        if (operation != (ushort)expectedOperation) return ResponseDecodeResult.Malformed;
        if (!WireValue.IsKnownStatus(status)) return ResponseDecodeResult.Malformed;
        if (payloadSize > ControlProtocol.MaxPayloadBytes) return ResponseDecodeResult.Malformed;
        if (bytes.Length != ControlProtocol.HeaderSize + (int)payloadSize)
            return ResponseDecodeResult.Malformed;

        ReadOnlySpan<byte> payload = bytes.Slice(ControlProtocol.HeaderSize, (int)payloadSize);
        var controlStatus = (ControlStatus)status;

        if (controlStatus != ControlStatus.Ok)
        {
            // v1 error responses always carry an empty payload.
            return payload.Length == 0
                ? ResponseDecodeResult.ProtocolError(controlStatus)
                : ResponseDecodeResult.Malformed;
        }

        var reader = new ByteReader(payload);
        return expectedOperation switch
        {
            ControlOperation.GetRuntimeInfo => DecodeRuntimeInfo(ref reader),
            ControlOperation.GetSettingsSnapshot => DecodeSnapshot(ref reader),
            _ => ResponseDecodeResult.Malformed,
        };
    }

    private static ResponseDecodeResult DecodeRuntimeInfo(ref ByteReader r)
    {
        if (!r.TryReadString(out string version)) return ResponseDecodeResult.Malformed;
        if (!r.TryReadUInt16(out ushort minVersion)) return ResponseDecodeResult.Malformed;
        if (!r.TryReadUInt16(out ushort maxVersion)) return ResponseDecodeResult.Malformed;
        if (!r.TryReadByte(out byte launchMode) || !WireValue.IsLaunchMode(launchMode))
            return ResponseDecodeResult.Malformed;
        if (!r.TryReadByte(out byte runtimeState) || !WireValue.IsRuntimeState(runtimeState))
            return ResponseDecodeResult.Malformed;
        if (!r.AtEnd) return ResponseDecodeResult.Malformed;

        var info = new RuntimeInfo(version, minVersion, maxVersion,
            (WireLaunchMode)launchMode, (WireRuntimeState)runtimeState);
        return new ResponseDecodeResult(ResponseDecodeOutcome.Ok, ControlStatus.Ok, RuntimeInfo: info);
    }

    private static ResponseDecodeResult DecodeSnapshot(ref ByteReader r)
    {
        if (!r.TryReadBool(out bool startWithWindows)) return ResponseDecodeResult.Malformed;
        if (!r.TryReadBool(out bool hudEnabled)) return ResponseDecodeResult.Malformed;
        if (!r.TryReadInt32(out int hudSizeOffset) || !WireValue.IsHudSizeOffset(hudSizeOffset))
            return ResponseDecodeResult.Malformed;
        if (!r.TryReadByte(out byte hudFont) || !WireValue.IsFont(hudFont))
            return ResponseDecodeResult.Malformed;
        if (!r.TryReadByte(out byte visibilityMode) || !WireValue.IsVisibilityMode(visibilityMode))
            return ResponseDecodeResult.Malformed;
        if (!r.TryReadByte(out byte alignment) || !WireValue.IsAlignment(alignment))
            return ResponseDecodeResult.Malformed;
        if (!r.TryReadByte(out byte backgroundMode) || !WireValue.IsBackgroundMode(backgroundMode))
            return ResponseDecodeResult.Malformed;
        if (!r.TryReadUInt16(out ushort opacityPercent) || !WireValue.IsOpacityPercent(opacityPercent))
            return ResponseDecodeResult.Malformed;
        if (!r.TryReadBool(out bool intelVrrEnabled)) return ResponseDecodeResult.Malformed;
        if (!r.TryReadByte(out byte hasResult) || hasResult > 1) return ResponseDecodeResult.Malformed;

        IntelVrrResult? vrrResult = null;
        if (hasResult == 1)
        {
            if (!r.TryReadByte(out byte vrrStatus) || !WireValue.IsIntelVrrStatus(vrrStatus))
                return ResponseDecodeResult.Malformed;
            if (!r.TryReadString(out string panel)) return ResponseDecodeResult.Malformed;
            if (!r.TryReadString(out string before)) return ResponseDecodeResult.Malformed;
            if (!r.TryReadString(out string after)) return ResponseDecodeResult.Malformed;
            if (!r.TryReadString(out string message)) return ResponseDecodeResult.Malformed;
            if (!r.TryReadString(out string timestamp)) return ResponseDecodeResult.Malformed;
            vrrResult = new IntelVrrResult((WireIntelVrrStatus)vrrStatus, panel, before, after,
                message, timestamp);
        }

        if (!r.AtEnd) return ResponseDecodeResult.Malformed;

        var snapshot = new SettingsSnapshot(startWithWindows, hudEnabled, hudSizeOffset,
            (WireFont)hudFont, (WireVisibilityMode)visibilityMode, (WireAlignment)alignment,
            (WireBackgroundMode)backgroundMode, opacityPercent, intelVrrEnabled, vrrResult);
        return new ResponseDecodeResult(ResponseDecodeOutcome.Ok, ControlStatus.Ok, Snapshot: snapshot);
    }

    /// <summary>Bounds-checked little-endian reader over a payload span.</summary>
    private ref struct ByteReader(ReadOnlySpan<byte> bytes)
    {
        private readonly ReadOnlySpan<byte> _bytes = bytes;
        private int _pos;

        internal readonly bool AtEnd => _pos == _bytes.Length;

        internal bool TryReadByte(out byte value)
        {
            if (_pos + 1 > _bytes.Length) { value = 0; return false; }
            value = _bytes[_pos++];
            return true;
        }

        internal bool TryReadBool(out bool value)
        {
            value = false;
            if (!TryReadByte(out byte b) || b > 1) return false;
            value = b == 1;
            return true;
        }

        internal bool TryReadUInt16(out ushort value)
        {
            if (_pos + 2 > _bytes.Length) { value = 0; return false; }
            value = BinaryPrimitives.ReadUInt16LittleEndian(_bytes[_pos..]);
            _pos += 2;
            return true;
        }

        internal bool TryReadInt32(out int value)
        {
            if (_pos + 4 > _bytes.Length) { value = 0; return false; }
            value = BinaryPrimitives.ReadInt32LittleEndian(_bytes[_pos..]);
            _pos += 4;
            return true;
        }

        internal bool TryReadString(out string value)
        {
            value = string.Empty;
            if (!TryReadUInt16(out ushort length)) return false;
            if (length > ControlProtocol.MaxStringBytes) return false;
            if (_pos + length > _bytes.Length) return false;

            ReadOnlySpan<byte> raw = _bytes.Slice(_pos, length);
            try
            {
                value = StrictUtf8.GetString(raw);
            }
            catch (DecoderFallbackException)
            {
                return false;
            }
            if (value.Contains('\0')) return false; // native contract forbids embedded NUL

            _pos += length;
            return true;
        }
    }
}
