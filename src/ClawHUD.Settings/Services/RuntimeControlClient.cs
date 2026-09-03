using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using ClawHUD.Settings.Protocol;

namespace ClawHUD.Settings.Services;

public enum ControlClientOutcome
{
    Success,
    ProtocolError,
    TransportUnavailable,
    MalformedResponse,
    TimedOut,
}

public sealed record ControlClientResult<T>(
    ControlClientOutcome Outcome,
    T? Value = default,
    ControlStatus Status = ControlStatus.Ok)
{
    internal bool IsSuccess => Outcome == ControlClientOutcome.Success && Value is not null;

    internal static ControlClientResult<T> Success(T value) => new(ControlClientOutcome.Success, value);
    internal static ControlClientResult<T> Protocol(ControlStatus status) =>
        new(ControlClientOutcome.ProtocolError, Status: status);
    internal static ControlClientResult<T> Transport { get; } = new(ControlClientOutcome.TransportUnavailable);
    internal static ControlClientResult<T> Malformed { get; } = new(ControlClientOutcome.MalformedResponse);
    internal static ControlClientResult<T> Timeout { get; } = new(ControlClientOutcome.TimedOut);
}

/// <summary>
/// Client for the ClawHUD Control Named Pipe. Reads and mutations share one
/// transport path: every call opens a fresh connection, sends one request, reads
/// one response and closes — matching the native server's single-instance,
/// one-request-per-connection lifetime. No persistent connection, no polling, no
/// retry/reconnect state machine.
/// </summary>
public interface IRuntimeControlClient
{
    Task<ControlClientResult<RuntimeInfo>> GetRuntimeInfoAsync(CancellationToken cancellationToken = default);
    Task<ControlClientResult<SettingsSnapshot>> GetSettingsSnapshotAsync(CancellationToken cancellationToken = default);
    Task<ControlClientResult<SettingsSnapshot>> SetHudEnabledAsync(bool enabled, CancellationToken cancellationToken = default);
    Task<ControlClientResult<SettingsSnapshot>> SetHudVisibilityModeAsync(WireVisibilityMode mode, CancellationToken cancellationToken = default);
    Task<ControlClientResult<SettingsSnapshot>> SetHudSizeOffsetAsync(int offset, CancellationToken cancellationToken = default);
    Task<ControlClientResult<SettingsSnapshot>> SetHudFontAsync(WireFont font, CancellationToken cancellationToken = default);
    Task<ControlClientResult<SettingsSnapshot>> SetHudAlignmentAsync(WireAlignment alignment, CancellationToken cancellationToken = default);
    Task<ControlClientResult<SettingsSnapshot>> SetHudBackgroundModeAsync(WireBackgroundMode mode, CancellationToken cancellationToken = default);
    Task<ControlClientResult<SettingsSnapshot>> PreviewHudOpacityAsync(ushort opacityPercent, CancellationToken cancellationToken = default);
    Task<ControlClientResult<SettingsSnapshot>> CommitHudOpacityAsync(ushort opacityPercent, CancellationToken cancellationToken = default);
}

public sealed class RuntimeControlClient : IRuntimeControlClient
{
    private static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(3);

    private readonly string _pipeName;
    private readonly TimeSpan _timeout;
    private uint _nextRequestId;

    internal RuntimeControlClient()
        : this($"ClawHUD.Control.{Process.GetCurrentProcess().SessionId}", DefaultTimeout)
    {
    }

    // Test seam: a caller-supplied pipe name / shorter budget. Never used by the UI.
    internal RuntimeControlClient(string pipeName, TimeSpan? timeout = null)
    {
        _pipeName = pipeName;
        _timeout = timeout ?? DefaultTimeout;
    }

    public Task<ControlClientResult<RuntimeInfo>> GetRuntimeInfoAsync(
        CancellationToken cancellationToken = default) =>
        ExecuteAsync(ControlOperation.GetRuntimeInfo,
            id => new ControlRequest(ControlOperation.GetRuntimeInfo, id),
            r => r.RuntimeInfo, cancellationToken);

    public Task<ControlClientResult<SettingsSnapshot>> GetSettingsSnapshotAsync(
        CancellationToken cancellationToken = default) =>
        Snapshot(ControlOperation.GetSettingsSnapshot,
            id => new ControlRequest(ControlOperation.GetSettingsSnapshot, id), cancellationToken);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudEnabledAsync(bool enabled,
        CancellationToken cancellationToken = default) =>
        Snapshot(ControlOperation.SetHudEnabled,
            id => new ControlRequest(ControlOperation.SetHudEnabled, id, Flag: enabled), cancellationToken);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudVisibilityModeAsync(WireVisibilityMode mode,
        CancellationToken cancellationToken = default) =>
        Snapshot(ControlOperation.SetHudVisibilityMode,
            id => new ControlRequest(ControlOperation.SetHudVisibilityMode, id, WireEnum: (byte)mode), cancellationToken);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudSizeOffsetAsync(int offset,
        CancellationToken cancellationToken = default) =>
        Snapshot(ControlOperation.SetHudSizeOffset,
            id => new ControlRequest(ControlOperation.SetHudSizeOffset, id, SizeOffset: offset), cancellationToken);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudFontAsync(WireFont font,
        CancellationToken cancellationToken = default) =>
        Snapshot(ControlOperation.SetHudFont,
            id => new ControlRequest(ControlOperation.SetHudFont, id, WireEnum: (byte)font), cancellationToken);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudAlignmentAsync(WireAlignment alignment,
        CancellationToken cancellationToken = default) =>
        Snapshot(ControlOperation.SetHudAlignment,
            id => new ControlRequest(ControlOperation.SetHudAlignment, id, WireEnum: (byte)alignment), cancellationToken);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudBackgroundModeAsync(WireBackgroundMode mode,
        CancellationToken cancellationToken = default) =>
        Snapshot(ControlOperation.SetHudBackgroundMode,
            id => new ControlRequest(ControlOperation.SetHudBackgroundMode, id, WireEnum: (byte)mode), cancellationToken);

    public Task<ControlClientResult<SettingsSnapshot>> PreviewHudOpacityAsync(ushort opacityPercent,
        CancellationToken cancellationToken = default) =>
        Snapshot(ControlOperation.PreviewHudOpacity,
            id => new ControlRequest(ControlOperation.PreviewHudOpacity, id, OpacityPercent: opacityPercent), cancellationToken);

    public Task<ControlClientResult<SettingsSnapshot>> CommitHudOpacityAsync(ushort opacityPercent,
        CancellationToken cancellationToken = default) =>
        Snapshot(ControlOperation.CommitHudOpacity,
            id => new ControlRequest(ControlOperation.CommitHudOpacity, id, OpacityPercent: opacityPercent), cancellationToken);

    private Task<ControlClientResult<SettingsSnapshot>> Snapshot(ControlOperation operation,
        Func<uint, ControlRequest> build, CancellationToken cancellationToken) =>
        ExecuteAsync(operation, build, r => r.Snapshot, cancellationToken);

    private uint NextRequestId()
    {
        uint id = unchecked(++_nextRequestId);
        if (id == 0)
            id = unchecked(++_nextRequestId);
        return id;
    }

    private async Task<ControlClientResult<T>> ExecuteAsync<T>(ControlOperation operation,
        Func<uint, ControlRequest> build, Func<ResponseDecodeResult, T?> select,
        CancellationToken cancellationToken)
        where T : class
    {
        uint requestId = NextRequestId();
        byte[] request = ControlCodec.EncodeRequest(build(requestId));

        using var timeoutSource = new CancellationTokenSource(_timeout);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken, timeoutSource.Token);
        CancellationToken token = linked.Token;

        try
        {
            await using var pipe = new NamedPipeClientStream(".", _pipeName,
                PipeDirection.InOut, PipeOptions.Asynchronous);
            await pipe.ConnectAsync(token).ConfigureAwait(false);
            pipe.ReadMode = PipeTransmissionMode.Message;

            await pipe.WriteAsync(request, token).ConfigureAwait(false);
            await pipe.FlushAsync(token).ConfigureAwait(false);

            byte[] buffer = new byte[ControlProtocol.MaxFrameBytes];
            int total = 0;
            do
            {
                int read = await pipe.ReadAsync(buffer.AsMemory(total), token).ConfigureAwait(false);
                if (read == 0)
                    break;
                total += read;
            }
            while (!pipe.IsMessageComplete && total < buffer.Length);

            if (total == 0)
                return ControlClientResult<T>.Transport;
            if (!pipe.IsMessageComplete)
                return ControlClientResult<T>.Malformed; // response larger than a valid v1 frame

            ResponseDecodeResult decoded = ControlCodec.DecodeResponse(
                buffer.AsSpan(0, total), requestId, operation);

            return decoded.Outcome switch
            {
                ResponseDecodeOutcome.Ok when select(decoded) is { } value =>
                    ControlClientResult<T>.Success(value),
                ResponseDecodeOutcome.Ok => ControlClientResult<T>.Malformed,
                ResponseDecodeOutcome.ProtocolError => ControlClientResult<T>.Protocol(decoded.Status),
                _ => ControlClientResult<T>.Malformed,
            };
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (OperationCanceledException)
        {
            return ControlClientResult<T>.Timeout;
        }
        catch (TimeoutException)
        {
            return ControlClientResult<T>.Timeout;
        }
        catch (Exception ex) when (ex is IOException or Win32Exception or UnauthorizedAccessException)
        {
            return ControlClientResult<T>.Transport;
        }
    }
}
