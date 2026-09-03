using System.Buffers.Binary;
using ClawHUD.Settings.Protocol;
using Xunit;

namespace ClawHUD.Settings.Tests;

public class ControlCodecTests
{
    // ---- Golden request frames (§16.1, §18.1) --------------------------

    [Fact]
    public void EncodeRequest_GetSettingsSnapshot_IsExactGoldenFrame()
    {
        byte[] frame = ControlCodec.EncodeRequest(
            new ControlRequest(ControlOperation.GetSettingsSnapshot, 0x11223344));

        Assert.Equal(new byte[]
        {
            0x43, 0x48, 0x55, 0x44, // CHUD
            0x01, 0x00,             // protocol v1
            0x18, 0x00,             // header size 24
            0x01, 0x00,             // Request
            0x02, 0x00,             // GetSettingsSnapshot
            0x44, 0x33, 0x22, 0x11, // requestId LE
            0x00, 0x00, 0x00, 0x00, // status 0
            0x00, 0x00, 0x00, 0x00, // payload size 0
        }, frame);
    }

    [Fact]
    public void EncodeRequest_SetHudSizeOffset_IsExactGoldenFrameWithLittleEndianI32()
    {
        byte[] frame = ControlCodec.EncodeRequest(
            new ControlRequest(ControlOperation.SetHudSizeOffset, 0x00000003, SizeOffset: -2));

        Assert.Equal(new byte[]
        {
            0x43, 0x48, 0x55, 0x44,
            0x01, 0x00, 0x18, 0x00,
            0x01, 0x00,             // Request
            0x0D, 0x00,             // SetHudSizeOffset (13)
            0x03, 0x00, 0x00, 0x00, // requestId 3
            0x00, 0x00, 0x00, 0x00, // status 0
            0x04, 0x00, 0x00, 0x00, // payload size 4
            0xFE, 0xFF, 0xFF, 0xFF, // i32 LE -2
        }, frame);
    }

    public static TheoryData<string, ControlRequest, ushort, int, byte[]> MutationRequests() => new()
    {
        { "SetHudEnabled(true)", new ControlRequest(ControlOperation.SetHudEnabled, 1, Flag: true), 11, 1, new byte[] { 0x01 } },
        { "SetHudVisibilityMode(InGameOnly)", new ControlRequest(ControlOperation.SetHudVisibilityMode, 2, WireEnum: (byte)WireVisibilityMode.InGameOnly), 12, 1, new byte[] { 0x02 } },
        { "SetHudSizeOffset(+2)", new ControlRequest(ControlOperation.SetHudSizeOffset, 3, SizeOffset: 2), 13, 4, new byte[] { 0x02, 0x00, 0x00, 0x00 } },
        { "SetHudFont(SegoeUiVariable)", new ControlRequest(ControlOperation.SetHudFont, 4, WireEnum: (byte)WireFont.SegoeUiVariable), 14, 1, new byte[] { 0x02 } },
        { "SetHudAlignment(Right)", new ControlRequest(ControlOperation.SetHudAlignment, 5, WireEnum: (byte)WireAlignment.Right), 15, 1, new byte[] { 0x03 } },
        { "SetHudBackgroundMode(ContentWidth)", new ControlRequest(ControlOperation.SetHudBackgroundMode, 6, WireEnum: (byte)WireBackgroundMode.ContentWidth), 16, 1, new byte[] { 0x02 } },
    };

    [Theory]
    [MemberData(nameof(MutationRequests))]
    public void EncodeRequest_MutationFrameFields(string _, ControlRequest request,
        ushort expectedOperation, int expectedPayloadLength, byte[] expectedPayload)
    {
        byte[] frame = ControlCodec.EncodeRequest(request);

        Assert.Equal("CHUD"u8.ToArray(), frame[..4]);
        Assert.Equal(1, BinaryPrimitives.ReadUInt16LittleEndian(frame.AsSpan(4)));  // protocol v1
        Assert.Equal(24, BinaryPrimitives.ReadUInt16LittleEndian(frame.AsSpan(6))); // header size
        Assert.Equal(1, BinaryPrimitives.ReadUInt16LittleEndian(frame.AsSpan(8)));  // Request
        Assert.Equal(expectedOperation, BinaryPrimitives.ReadUInt16LittleEndian(frame.AsSpan(10)));
        Assert.Equal(request.RequestId, BinaryPrimitives.ReadUInt32LittleEndian(frame.AsSpan(12)));
        Assert.Equal(0u, BinaryPrimitives.ReadUInt32LittleEndian(frame.AsSpan(16))); // status
        Assert.Equal((uint)expectedPayloadLength, BinaryPrimitives.ReadUInt32LittleEndian(frame.AsSpan(20)));
        Assert.Equal(24 + expectedPayloadLength, frame.Length);
        Assert.Equal(expectedPayload, frame[24..]);
    }

    [Fact]
    public void EncodeRequest_RejectsZeroRequestId() =>
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ControlCodec.EncodeRequest(new ControlRequest(ControlOperation.GetRuntimeInfo, 0)));

    [Theory]
    [InlineData(-3)]
    [InlineData(3)]
    [InlineData(int.MinValue)]
    public void EncodeRequest_RejectsHudSizeOffsetOutOfRange(int offset) =>
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ControlCodec.EncodeRequest(new ControlRequest(ControlOperation.SetHudSizeOffset, 1, SizeOffset: offset)));

    [Fact]
    public void EncodeRequest_RejectsInvalidWireEnum() =>
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ControlCodec.EncodeRequest(new ControlRequest(ControlOperation.SetHudAlignment, 1, WireEnum: 9)));

    [Fact]
    public void EncodeRequest_RejectsMissingPayloadField()
    {
        Assert.Throws<ArgumentException>(() =>
            ControlCodec.EncodeRequest(new ControlRequest(ControlOperation.SetHudEnabled, 1)));
        Assert.Throws<ArgumentException>(() =>
            ControlCodec.EncodeRequest(new ControlRequest(ControlOperation.SetHudFont, 1)));
        Assert.Throws<ArgumentException>(() =>
            ControlCodec.EncodeRequest(new ControlRequest(ControlOperation.SetHudSizeOffset, 1)));
    }

    [Theory]
    [InlineData(ControlOperation.PreviewHudOpacity)]
    [InlineData(ControlOperation.CommitHudOpacity)]
    [InlineData(ControlOperation.SetIntelVrrRangeFixEnabled)]
    [InlineData(ControlOperation.SetStartWithWindows)]
    [InlineData(ControlOperation.RequestShutdown)]
    public void EncodeRequest_RejectsDeferredOperations(ControlOperation operation) =>
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ControlCodec.EncodeRequest(new ControlRequest(operation, 1)));

    // ---- Mutation response snapshot decoding (§16.3) -------------------

    [Theory]
    [InlineData(ControlOperation.SetHudEnabled)]
    [InlineData(ControlOperation.SetHudVisibilityMode)]
    [InlineData(ControlOperation.SetHudSizeOffset)]
    [InlineData(ControlOperation.SetHudFont)]
    [InlineData(ControlOperation.SetHudAlignment)]
    [InlineData(ControlOperation.SetHudBackgroundMode)]
    public void DecodeResponse_MutationOk_DecodesAuthoritativeSnapshot(ControlOperation operation)
    {
        byte[] frame = WireFixtures.ResponseFrame(operation, 42, ControlStatus.Ok,
            WireFixtures.SnapshotPayload(hudSizeOffset: 1, alignment: (byte)WireAlignment.Right));

        ResponseDecodeResult result = ControlCodec.DecodeResponse(frame, 42, operation);

        Assert.Equal(ResponseDecodeOutcome.Ok, result.Outcome);
        SettingsSnapshot snapshot = Assert.IsType<SettingsSnapshot>(result.Snapshot);
        Assert.Equal(1, snapshot.HudSizeOffset);
        Assert.Equal(WireAlignment.Right, snapshot.Alignment);
    }

    [Fact]
    public void DecodeResponse_MutationOperationFailed_SurfacesTypedErrorWithNoSnapshot()
    {
        byte[] frame = WireFixtures.ResponseFrame(ControlOperation.SetHudEnabled, 7,
            ControlStatus.OperationFailed, ReadOnlySpan<byte>.Empty);

        ResponseDecodeResult result = ControlCodec.DecodeResponse(frame, 7, ControlOperation.SetHudEnabled);

        Assert.Equal(ResponseDecodeOutcome.ProtocolError, result.Outcome);
        Assert.Equal(ControlStatus.OperationFailed, result.Status);
        Assert.Null(result.Snapshot);
    }

    // ---- Golden runtime-info response decode (§18.2) --------------------

    [Fact]
    public void DecodeResponse_RuntimeInfo_GoldenFixture()
    {
        byte[] frame =
        {
            0x43, 0x48, 0x55, 0x44, 0x01, 0x00, 0x18, 0x00, // header
            0x02, 0x00,                                     // Response
            0x01, 0x00,                                     // GetRuntimeInfo
            0x01, 0x00, 0x00, 0x00,                         // requestId 1
            0x00, 0x00, 0x00, 0x00,                         // status Ok
            0x0D, 0x00, 0x00, 0x00,                         // payload size 13
            0x05, 0x00, 0x31, 0x2E, 0x34, 0x2E, 0x32,       // "1.4.2"
            0x01, 0x00,                                     // min protocol 1
            0x01, 0x00,                                     // max protocol 1
            0x01,                                           // launch mode Standalone
            0x02,                                           // runtime state Ready
        };

        ResponseDecodeResult result = ControlCodec.DecodeResponse(frame, 1, ControlOperation.GetRuntimeInfo);

        Assert.Equal(ResponseDecodeOutcome.Ok, result.Outcome);
        RuntimeInfo info = Assert.IsType<RuntimeInfo>(result.RuntimeInfo);
        Assert.Equal("1.4.2", info.ApplicationVersion);
        Assert.Equal(1, info.MinimumProtocolVersion);
        Assert.Equal(1, info.MaximumProtocolVersion);
        Assert.Equal(WireLaunchMode.Standalone, info.LaunchMode);
        Assert.Equal(WireRuntimeState.Ready, info.RuntimeState);
    }

    [Fact]
    public void DecodeResponse_RuntimeInfo_RejectsInvalidLaunchMode()
    {
        byte[] payload = WireFixtures.RuntimeInfoPayload(launchMode: 9);
        byte[] frame = WireFixtures.ResponseFrame(ControlOperation.GetRuntimeInfo, 7, ControlStatus.Ok, payload);

        Assert.Equal(ResponseDecodeOutcome.Malformed,
            ControlCodec.DecodeResponse(frame, 7, ControlOperation.GetRuntimeInfo).Outcome);
    }

    // ---- Golden settings-snapshot response decode (§18.3) --------------

    [Fact]
    public void DecodeResponse_Snapshot_GoldenFixtureWithoutVrr()
    {
        byte[] frame =
        {
            0x43, 0x48, 0x55, 0x44, 0x01, 0x00, 0x18, 0x00,
            0x02, 0x00,                                     // Response
            0x02, 0x00,                                     // GetSettingsSnapshot
            0x09, 0x00, 0x00, 0x00,                         // requestId 9
            0x00, 0x00, 0x00, 0x00,                         // status Ok
            0x0E, 0x00, 0x00, 0x00,                         // payload size 14
            0x01,                                           // startWithWindows
            0x01,                                           // hudEnabled
            0xFF, 0xFF, 0xFF, 0xFF,                         // hudSizeOffset -1
            0x02,                                           // font SegoeUiVariable
            0x02,                                           // visibility InGameOnly
            0x03,                                           // alignment Right
            0x01,                                           // background FullWidth
            0x55, 0x00,                                     // opacity 85
            0x01,                                           // intelVrrRangeFixEnabled
            0x00,                                           // hasIntelVrrLastResult = 0
        };

        ResponseDecodeResult result = ControlCodec.DecodeResponse(frame, 9, ControlOperation.GetSettingsSnapshot);

        Assert.Equal(ResponseDecodeOutcome.Ok, result.Outcome);
        SettingsSnapshot s = Assert.IsType<SettingsSnapshot>(result.Snapshot);
        Assert.True(s.StartWithWindows);
        Assert.True(s.HudEnabled);
        Assert.Equal(-1, s.HudSizeOffset);
        Assert.Equal(WireFont.SegoeUiVariable, s.HudFont);
        Assert.Equal(WireVisibilityMode.InGameOnly, s.VisibilityMode);
        Assert.Equal(WireAlignment.Right, s.Alignment);
        Assert.Equal(WireBackgroundMode.FullWidth, s.BackgroundMode);
        Assert.Equal(85, s.BackgroundOpacityPercent);
        Assert.True(s.IntelVrrRangeFixEnabled);
        Assert.Null(s.IntelVrrLastResult);
    }

    [Fact]
    public void DecodeResponse_Snapshot_DecodesIntelVrrResultWithMultiByteUtf8()
    {
        byte[] payload = WireFixtures.SnapshotPayload(
            vrr: (Status: 7, Panel: "패널 ™", Before: "48-60", After: "40-60",
                Message: "applied", Timestamp: "2026-09-03T10:00:00Z"));
        byte[] frame = WireFixtures.ResponseFrame(ControlOperation.GetSettingsSnapshot, 3, ControlStatus.Ok, payload);

        ResponseDecodeResult result = ControlCodec.DecodeResponse(frame, 3, ControlOperation.GetSettingsSnapshot);

        Assert.Equal(ResponseDecodeOutcome.Ok, result.Outcome);
        IntelVrrResult vrr = Assert.IsType<IntelVrrResult>(result.Snapshot!.IntelVrrLastResult);
        Assert.Equal(WireIntelVrrStatus.Applied, vrr.Status);
        Assert.Equal("패널 ™", vrr.PanelName);
        Assert.Equal("40-60", vrr.RangeAfter);
    }

    [Theory]
    [InlineData(-2)]
    [InlineData(0)]
    [InlineData(2)]
    public void DecodeResponse_Snapshot_AcceptsHudSizeOffsetBounds(int offset)
    {
        byte[] payload = WireFixtures.SnapshotPayload(hudSizeOffset: offset);
        byte[] frame = WireFixtures.ResponseFrame(ControlOperation.GetSettingsSnapshot, 4, ControlStatus.Ok, payload);

        ResponseDecodeResult result = ControlCodec.DecodeResponse(frame, 4, ControlOperation.GetSettingsSnapshot);

        Assert.Equal(ResponseDecodeOutcome.Ok, result.Outcome);
        Assert.Equal(offset, result.Snapshot!.HudSizeOffset);
    }

    [Theory]
    [InlineData(50)]
    [InlineData(75)]
    [InlineData(100)]
    public void DecodeResponse_Snapshot_AcceptsOpacityStepsInRange(int opacity)
    {
        byte[] payload = WireFixtures.SnapshotPayload(opacityPercent: (ushort)opacity);
        byte[] frame = WireFixtures.ResponseFrame(ControlOperation.GetSettingsSnapshot, 5, ControlStatus.Ok, payload);

        Assert.Equal(opacity,
            ControlCodec.DecodeResponse(frame, 5, ControlOperation.GetSettingsSnapshot).Snapshot!.BackgroundOpacityPercent);
    }

    // ---- Error responses (§9.2) ----------------------------------------

    [Fact]
    public void DecodeResponse_ProtocolError_SurfacesTypedStatus()
    {
        byte[] frame = WireFixtures.ResponseFrame(ControlOperation.GetSettingsSnapshot, 2,
            ControlStatus.RuntimeUnavailable, ReadOnlySpan<byte>.Empty);

        ResponseDecodeResult result = ControlCodec.DecodeResponse(frame, 2, ControlOperation.GetSettingsSnapshot);

        Assert.Equal(ResponseDecodeOutcome.ProtocolError, result.Outcome);
        Assert.Equal(ControlStatus.RuntimeUnavailable, result.Status);
        Assert.Null(result.Snapshot);
    }

    [Fact]
    public void DecodeResponse_ErrorStatusWithNonEmptyPayload_IsMalformed()
    {
        byte[] frame = WireFixtures.ResponseFrame(ControlOperation.GetSettingsSnapshot, 2,
            ControlStatus.OperationFailed, new byte[] { 0x00 });

        Assert.Equal(ResponseDecodeOutcome.Malformed,
            ControlCodec.DecodeResponse(frame, 2, ControlOperation.GetSettingsSnapshot).Outcome);
    }

    // ---- Malformed frame rejection (§18.4) ----------------------------

    public static TheoryData<string, byte[]> MalformedFrames()
    {
        byte[] okPayload = WireFixtures.RuntimeInfoPayload();
        byte[] Base() => WireFixtures.ResponseFrame(ControlOperation.GetRuntimeInfo, 1, ControlStatus.Ok, okPayload);

        var truncatedHeader = Base()[..10];

        var wrongMagic = Base();
        wrongMagic[0] = 0x58;

        var wrongVersion = Base();
        wrongVersion[4] = 0x02;

        var wrongHeaderSize = Base();
        wrongHeaderSize[6] = 0x20;

        var wrongKind = Base();
        wrongKind[8] = 0x01; // Request

        var zeroRequestId = Base();
        Array.Clear(zeroRequestId, 12, 4);

        var unknownStatus = Base();
        unknownStatus[16] = 0x63;

        var oversizePayloadField = Base();
        oversizePayloadField[20] = 0x01;
        oversizePayloadField[21] = 0x00;
        oversizePayloadField[22] = 0x01; // 65537 > 16 KiB

        var declaredLargerThanActual = Base();
        declaredLargerThanActual[20] = 0xFF;

        var trailingBytes = Base().Append((byte)0x00).ToArray();

        var invalidBoolByte = WireFixtures.ResponseFrame(ControlOperation.GetSettingsSnapshot, 1, ControlStatus.Ok,
            Patch(WireFixtures.SnapshotPayload(), 0, 0x02));

        var invalidEnum = WireFixtures.ResponseFrame(ControlOperation.GetSettingsSnapshot, 1, ControlStatus.Ok,
            Patch(WireFixtures.SnapshotPayload(), 7, 0x09)); // visibilityMode byte out of range

        var invalidSize = WireFixtures.ResponseFrame(ControlOperation.GetSettingsSnapshot, 1, ControlStatus.Ok,
            Patch(WireFixtures.SnapshotPayload(), 2, 0x05)); // hudSizeOffset = 5

        var badOpacityStep = WireFixtures.ResponseFrame(ControlOperation.GetSettingsSnapshot, 1, ControlStatus.Ok,
            WireFixtures.SnapshotPayload(opacityPercent: 73));

        var malformedUtf8 = WireFixtures.ResponseFrame(ControlOperation.GetRuntimeInfo, 1, ControlStatus.Ok,
            new WireFixtures.PayloadWriter().Also(w =>
            {
                w.U16(2);
                w.Raw(new byte[] { 0xFF, 0xFE });
                w.U16(1); w.U16(1); w.U8(1); w.U8(2);
            }).ToArray());

        var embeddedNul = WireFixtures.ResponseFrame(ControlOperation.GetRuntimeInfo, 1, ControlStatus.Ok,
            new WireFixtures.PayloadWriter().Also(w =>
            {
                w.U16(2);
                w.Raw(new byte[] { 0x41, 0x00 });
                w.U16(1); w.U16(1); w.U8(1); w.U8(2);
            }).ToArray());

        var stringOverruns = WireFixtures.ResponseFrame(ControlOperation.GetRuntimeInfo, 1, ControlStatus.Ok,
            new WireFixtures.PayloadWriter().Also(w =>
            {
                w.U16(50); // claims 50 bytes, none follow
            }).ToArray());

        var payloadTrailingBytes = WireFixtures.ResponseFrame(ControlOperation.GetRuntimeInfo, 1, ControlStatus.Ok,
            okPayload.Append((byte)0x00).ToArray());

        return new TheoryData<string, byte[]>
        {
            { "empty", Array.Empty<byte>() },
            { "truncated header", truncatedHeader },
            { "wrong magic", wrongMagic },
            { "wrong protocol version", wrongVersion },
            { "wrong header size", wrongHeaderSize },
            { "wrong message kind", wrongKind },
            { "request id zero", zeroRequestId },
            { "unknown status", unknownStatus },
            { "payload field over 16 KiB", oversizePayloadField },
            { "declared payload larger than actual", declaredLargerThanActual },
            { "frame trailing bytes", trailingBytes },
            { "invalid bool byte", invalidBoolByte },
            { "invalid wire enum", invalidEnum },
            { "invalid hud size", invalidSize },
            { "non-5% opacity", badOpacityStep },
            { "malformed utf-8", malformedUtf8 },
            { "embedded nul", embeddedNul },
            { "string length beyond payload", stringOverruns },
            { "trailing bytes after typed payload", payloadTrailingBytes },
        };
    }

    [Theory]
    [MemberData(nameof(MalformedFrames))]
    public void DecodeResponse_RejectsMalformedFrame(string _, byte[] frame)
    {
        ControlOperation op = frame.Length >= 12 && frame[10] == 0x02
            ? ControlOperation.GetSettingsSnapshot
            : ControlOperation.GetRuntimeInfo;

        Assert.Equal(ResponseDecodeOutcome.Malformed,
            ControlCodec.DecodeResponse(frame, 1, op).Outcome);
    }

    [Fact]
    public void DecodeResponse_RejectsRequestIdMismatch()
    {
        byte[] frame = WireFixtures.ResponseFrame(ControlOperation.GetRuntimeInfo, 4,
            ControlStatus.Ok, WireFixtures.RuntimeInfoPayload());

        Assert.Equal(ResponseDecodeOutcome.Malformed,
            ControlCodec.DecodeResponse(frame, 5, ControlOperation.GetRuntimeInfo).Outcome);
    }

    [Fact]
    public void DecodeResponse_RejectsOperationMismatch()
    {
        byte[] frame = WireFixtures.ResponseFrame(ControlOperation.GetRuntimeInfo, 4,
            ControlStatus.Ok, WireFixtures.RuntimeInfoPayload());

        Assert.Equal(ResponseDecodeOutcome.Malformed,
            ControlCodec.DecodeResponse(frame, 4, ControlOperation.GetSettingsSnapshot).Outcome);
    }

    private static byte[] Patch(byte[] payload, int index, byte value)
    {
        payload[index] = value;
        return payload;
    }
}

internal static class PayloadWriterExtensions
{
    internal static WireFixtures.PayloadWriter Also(this WireFixtures.PayloadWriter w,
        Action<WireFixtures.PayloadWriter> configure)
    {
        configure(w);
        return w;
    }
}
