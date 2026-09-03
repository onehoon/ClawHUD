using ClawHUD.Settings.Protocol;
using Xunit;

namespace ClawHUD.Settings.Tests;

public class ControlCodecTests
{
    // ---- Golden request frames (§18.1) ----------------------------------

    [Fact]
    public void EncodeReadRequest_GetSettingsSnapshot_IsExactGoldenFrame()
    {
        byte[] frame = ControlCodec.EncodeReadRequest(ControlOperation.GetSettingsSnapshot, 0x11223344);

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
    public void EncodeReadRequest_GetRuntimeInfo_IsExactGoldenFrame()
    {
        byte[] frame = ControlCodec.EncodeReadRequest(ControlOperation.GetRuntimeInfo, 1);

        Assert.Equal(new byte[]
        {
            0x43, 0x48, 0x55, 0x44,
            0x01, 0x00, 0x18, 0x00,
            0x01, 0x00,             // Request
            0x01, 0x00,             // GetRuntimeInfo
            0x01, 0x00, 0x00, 0x00, // requestId 1
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
        }, frame);
    }

    [Fact]
    public void EncodeReadRequest_RejectsZeroRequestId() =>
        Assert.Throws<ArgumentOutOfRangeException>(
            () => ControlCodec.EncodeReadRequest(ControlOperation.GetRuntimeInfo, 0));

    [Fact]
    public void EncodeReadRequest_RejectsNonReadOperations()
    {
        foreach (var operation in new[]
        {
            ControlOperation.SetHudEnabled, ControlOperation.CommitHudOpacity,
            ControlOperation.SetIntelVrrRangeFixEnabled, ControlOperation.RequestShutdown,
        })
        {
            Assert.Throws<ArgumentOutOfRangeException>(
                () => ControlCodec.EncodeReadRequest(operation, 1));
        }
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
