using System.Buffers.Binary;
using System.Text;
using ClawHUD.Settings.Protocol;

namespace ClawHUD.Settings.Tests;

/// <summary>
/// Builders for protocol-v1 test frames. Combinatorial decode cases use these;
/// correctness is anchored by the hand-written golden byte arrays in the tests.
/// </summary>
internal static class WireFixtures
{
    internal static byte[] ResponseFrame(ControlOperation operation, uint requestId,
        ControlStatus status, ReadOnlySpan<byte> payload)
    {
        var frame = new byte[ControlProtocol.HeaderSize + payload.Length];
        "CHUD"u8.CopyTo(frame);
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(4), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(6), 24);
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(8), 2); // Response
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(10), (ushort)operation);
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(12), requestId);
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(16), (uint)status);
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(20), (uint)payload.Length);
        payload.CopyTo(frame.AsSpan(24));
        return frame;
    }

    internal static byte[] RuntimeInfoPayload(string version = "1.4.2",
        ushort min = 1, ushort max = 1, byte launchMode = 1, byte runtimeState = 2)
    {
        var w = new PayloadWriter();
        w.String(version);
        w.U16(min);
        w.U16(max);
        w.U8(launchMode);
        w.U8(runtimeState);
        return w.ToArray();
    }

    internal static byte[] SnapshotPayload(
        bool startWithWindows = false,
        bool hudEnabled = true,
        int hudSizeOffset = 0,
        byte hudFont = 1,
        byte visibilityMode = 1,
        byte alignment = 2,
        byte backgroundMode = 2,
        ushort opacityPercent = 70,
        bool intelVrrRangeFixEnabled = false,
        (byte Status, string Panel, string Before, string After, string Message, string Timestamp)? vrr = null)
    {
        var w = new PayloadWriter();
        w.U8((byte)(startWithWindows ? 1 : 0));
        w.U8((byte)(hudEnabled ? 1 : 0));
        w.I32(hudSizeOffset);
        w.U8(hudFont);
        w.U8(visibilityMode);
        w.U8(alignment);
        w.U8(backgroundMode);
        w.U16(opacityPercent);
        w.U8((byte)(intelVrrRangeFixEnabled ? 1 : 0));
        w.U8((byte)(vrr is null ? 0 : 1));
        if (vrr is { } v)
        {
            w.U8(v.Status);
            w.String(v.Panel);
            w.String(v.Before);
            w.String(v.After);
            w.String(v.Message);
            w.String(v.Timestamp);
        }
        return w.ToArray();
    }

    internal sealed class PayloadWriter
    {
        private readonly List<byte> _bytes = [];

        internal void U8(byte v) => _bytes.Add(v);

        internal void U16(ushort v)
        {
            Span<byte> b = stackalloc byte[2];
            BinaryPrimitives.WriteUInt16LittleEndian(b, v);
            _bytes.AddRange(b);
        }

        internal void I32(int v)
        {
            Span<byte> b = stackalloc byte[4];
            BinaryPrimitives.WriteInt32LittleEndian(b, v);
            _bytes.AddRange(b);
        }

        internal void String(string s)
        {
            byte[] utf8 = Encoding.UTF8.GetBytes(s);
            U16((ushort)utf8.Length);
            _bytes.AddRange(utf8);
        }

        internal void Raw(ReadOnlySpan<byte> b) => _bytes.AddRange(b);

        internal byte[] ToArray() => _bytes.ToArray();
    }
}
