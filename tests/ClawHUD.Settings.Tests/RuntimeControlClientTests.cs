using System.Buffers.Binary;
using System.Diagnostics;
using System.IO.Pipes;
using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;
using Xunit;

namespace ClawHUD.Settings.Tests;

public class RuntimeControlClientTests
{
    [Fact]
    public async Task GetRuntimeInfoAsync_RoundTripsOverLocalMessagePipe()
    {
        string pipeName = $"ClawHUD.Control.Test.{Guid.NewGuid():N}";

        await using var server = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1,
            PipeTransmissionMode.Message, PipeOptions.Asynchronous);

        Task serverTask = Task.Run(async () =>
        {
            await server.WaitForConnectionAsync();

            var buffer = new byte[ControlProtocol.MaxFrameBytes];
            int total = 0;
            do
            {
                int read = await server.ReadAsync(buffer.AsMemory(total));
                if (read == 0) break;
                total += read;
            }
            while (!server.IsMessageComplete);

            uint requestId = BinaryPrimitives.ReadUInt32LittleEndian(buffer.AsSpan(12));
            ushort operation = BinaryPrimitives.ReadUInt16LittleEndian(buffer.AsSpan(10));
            Assert.Equal((ushort)ControlOperation.GetRuntimeInfo, operation);

            byte[] response = WireFixtures.ResponseFrame(ControlOperation.GetRuntimeInfo, requestId,
                ControlStatus.Ok, WireFixtures.RuntimeInfoPayload(version: "9.9.9", launchMode: 2, runtimeState: 2));
            await server.WriteAsync(response);
            await server.FlushAsync();
            server.WaitForPipeDrain();
        });

        var client = new RuntimeControlClient(pipeName, TimeSpan.FromSeconds(5));
        ControlClientResult<RuntimeInfo> result = await client.GetRuntimeInfoAsync();
        await serverTask;

        Assert.Equal(ControlClientOutcome.Success, result.Outcome);
        Assert.Equal("9.9.9", result.Value!.ApplicationVersion);
        Assert.Equal(WireLaunchMode.Managed, result.Value.LaunchMode);
    }

    [Fact]
    public async Task GetSettingsSnapshotAsync_SurfacesTypedProtocolError()
    {
        string pipeName = $"ClawHUD.Control.Test.{Guid.NewGuid():N}";

        await using var server = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1,
            PipeTransmissionMode.Message, PipeOptions.Asynchronous);

        Task serverTask = Task.Run(async () =>
        {
            await server.WaitForConnectionAsync();
            var buffer = new byte[256];
            int total = 0;
            do
            {
                int read = await server.ReadAsync(buffer.AsMemory(total));
                if (read == 0) break;
                total += read;
            }
            while (!server.IsMessageComplete);

            uint requestId = BinaryPrimitives.ReadUInt32LittleEndian(buffer.AsSpan(12));
            byte[] response = WireFixtures.ResponseFrame(ControlOperation.GetSettingsSnapshot, requestId,
                ControlStatus.ShuttingDown, ReadOnlySpan<byte>.Empty);
            await server.WriteAsync(response);
            await server.FlushAsync();
            server.WaitForPipeDrain();
        });

        var client = new RuntimeControlClient(pipeName, TimeSpan.FromSeconds(5));
        ControlClientResult<SettingsSnapshot> result = await client.GetSettingsSnapshotAsync();
        await serverTask;

        Assert.Equal(ControlClientOutcome.ProtocolError, result.Outcome);
        Assert.Equal(ControlStatus.ShuttingDown, result.Status);
        Assert.Null(result.Value);
    }

    [Fact]
    public async Task GetRuntimeInfoAsync_MissingPipe_FailsBoundedWithoutHanging()
    {
        var client = new RuntimeControlClient($"ClawHUD.Control.Missing.{Guid.NewGuid():N}",
            TimeSpan.FromMilliseconds(400));

        var stopwatch = Stopwatch.StartNew();
        ControlClientResult<RuntimeInfo> result = await client.GetRuntimeInfoAsync();
        stopwatch.Stop();

        Assert.Contains(result.Outcome,
            new[] { ControlClientOutcome.TransportUnavailable, ControlClientOutcome.TimedOut });
        Assert.Null(result.Value);
        Assert.True(stopwatch.Elapsed < TimeSpan.FromSeconds(5),
            $"missing-pipe call should return promptly, took {stopwatch.Elapsed}");
    }

    [Fact]
    public async Task SetHudAlignmentAsync_SendsMutationAndSurfacesAuthoritativeSnapshot()
    {
        string pipeName = $"ClawHUD.Control.Test.{Guid.NewGuid():N}";

        await using var server = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1,
            PipeTransmissionMode.Message, PipeOptions.Asynchronous);

        Task serverTask = Task.Run(async () =>
        {
            await server.WaitForConnectionAsync();
            var buffer = new byte[ControlProtocol.MaxFrameBytes];
            int total = 0;
            do
            {
                int read = await server.ReadAsync(buffer.AsMemory(total));
                if (read == 0) break;
                total += read;
            }
            while (!server.IsMessageComplete);

            ushort operation = BinaryPrimitives.ReadUInt16LittleEndian(buffer.AsSpan(10));
            uint payloadSize = BinaryPrimitives.ReadUInt32LittleEndian(buffer.AsSpan(20));
            uint requestId = BinaryPrimitives.ReadUInt32LittleEndian(buffer.AsSpan(12));
            Assert.Equal((ushort)ControlOperation.SetHudAlignment, operation);
            Assert.Equal(1u, payloadSize);
            Assert.Equal((byte)WireAlignment.Right, buffer[24]);

            // Runtime rolls the change back to Left in the authoritative snapshot.
            byte[] response = WireFixtures.ResponseFrame(ControlOperation.SetHudAlignment, requestId,
                ControlStatus.Ok, WireFixtures.SnapshotPayload(alignment: (byte)WireAlignment.Left));
            await server.WriteAsync(response);
            await server.FlushAsync();
            server.WaitForPipeDrain();
        });

        var client = new RuntimeControlClient(pipeName, TimeSpan.FromSeconds(5));
        ControlClientResult<SettingsSnapshot> result = await client.SetHudAlignmentAsync(WireAlignment.Right);
        await serverTask;

        Assert.Equal(ControlClientOutcome.Success, result.Outcome);
        Assert.Equal(WireAlignment.Left, result.Value!.Alignment);
    }

    [Theory]
    [InlineData(true, (ushort)17, (ushort)55)]
    [InlineData(false, (ushort)18, (ushort)90)]
    public async Task OpacityAsync_SendsCorrectOperationAndU16Payload(bool preview, ushort expectedOp, ushort percent)
    {
        string pipeName = $"ClawHUD.Control.Test.{Guid.NewGuid():N}";

        await using var server = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1,
            PipeTransmissionMode.Message, PipeOptions.Asynchronous);

        Task serverTask = Task.Run(async () =>
        {
            await server.WaitForConnectionAsync();
            var buffer = new byte[ControlProtocol.MaxFrameBytes];
            int total = 0;
            do
            {
                int read = await server.ReadAsync(buffer.AsMemory(total));
                if (read == 0) break;
                total += read;
            }
            while (!server.IsMessageComplete);

            Assert.Equal(expectedOp, BinaryPrimitives.ReadUInt16LittleEndian(buffer.AsSpan(10)));
            Assert.Equal(2u, BinaryPrimitives.ReadUInt32LittleEndian(buffer.AsSpan(20)));
            Assert.Equal(percent, BinaryPrimitives.ReadUInt16LittleEndian(buffer.AsSpan(24)));
            uint requestId = BinaryPrimitives.ReadUInt32LittleEndian(buffer.AsSpan(12));

            byte[] response = WireFixtures.ResponseFrame((ControlOperation)expectedOp, requestId,
                ControlStatus.Ok, WireFixtures.SnapshotPayload(opacityPercent: percent));
            await server.WriteAsync(response);
            await server.FlushAsync();
            server.WaitForPipeDrain();
        });

        var client = new RuntimeControlClient(pipeName, TimeSpan.FromSeconds(5));
        ControlClientResult<SettingsSnapshot> result = preview
            ? await client.PreviewHudOpacityAsync(percent)
            : await client.CommitHudOpacityAsync(percent);
        await serverTask;

        Assert.Equal(ControlClientOutcome.Success, result.Outcome);
        Assert.Equal(percent, result.Value!.BackgroundOpacityPercent);
    }

    [Fact]
    public async Task GetRuntimeInfoAsync_HonoursCallerCancellation()
    {
        using var cts = new CancellationTokenSource();
        cts.Cancel();

        var client = new RuntimeControlClient($"ClawHUD.Control.Cancel.{Guid.NewGuid():N}",
            TimeSpan.FromSeconds(30));

        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => client.GetRuntimeInfoAsync(cts.Token));
    }
}
