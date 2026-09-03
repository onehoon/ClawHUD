using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;

namespace ClawHUD.Settings.Tests;

/// <summary>
/// In-memory <see cref="IRuntimeControlClient"/> for ViewModel / coordinator
/// tests. Records the operations (and opacity values) it was asked to perform and
/// returns a configurable result. An optional gate holds every call "in flight"
/// until the test releases it; <see cref="MaxConcurrentCalls"/> proves the
/// coordinator never runs two IPC calls at once.
/// </summary>
internal sealed class FakeRuntimeControlClient : IRuntimeControlClient
{
    private readonly Lock _sync = new();
    private int _inFlight;

    internal List<ControlOperation> Calls { get; } = [];
    internal List<ushort> PreviewValues { get; } = [];
    internal List<ushort> CommitValues { get; } = [];
    internal int MaxConcurrentCalls { get; private set; }
    internal SettingsSnapshot? NextSnapshot { get; set; }
    internal ControlClientOutcome NextOutcome { get; set; } = ControlClientOutcome.Success;
    internal ControlStatus NextStatus { get; set; } = ControlStatus.OperationFailed;
    internal TaskCompletionSource? Gate { get; set; }

    private async Task<ControlClientResult<SettingsSnapshot>> Respond(ControlOperation operation)
    {
        lock (_sync)
        {
            Calls.Add(operation);
            _inFlight++;
            MaxConcurrentCalls = Math.Max(MaxConcurrentCalls, _inFlight);
        }

        try
        {
            if (Gate is not null)
                await Gate.Task;

            return NextOutcome switch
            {
                ControlClientOutcome.Success => ControlClientResult<SettingsSnapshot>.Success(NextSnapshot!),
                ControlClientOutcome.ProtocolError => ControlClientResult<SettingsSnapshot>.Protocol(NextStatus),
                ControlClientOutcome.MalformedResponse => ControlClientResult<SettingsSnapshot>.Malformed,
                ControlClientOutcome.TimedOut => ControlClientResult<SettingsSnapshot>.Timeout,
                _ => ControlClientResult<SettingsSnapshot>.Transport,
            };
        }
        finally
        {
            lock (_sync)
                _inFlight--;
        }
    }

    public Task<ControlClientResult<RuntimeInfo>> GetRuntimeInfoAsync(CancellationToken cancellationToken = default) =>
        Task.FromResult(ControlClientResult<RuntimeInfo>.Success(
            new RuntimeInfo("test", 1, 1, WireLaunchMode.Standalone, WireRuntimeState.Ready)));

    public Task<ControlClientResult<SettingsSnapshot>> GetSettingsSnapshotAsync(CancellationToken cancellationToken = default) =>
        Respond(ControlOperation.GetSettingsSnapshot);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudEnabledAsync(bool enabled, CancellationToken cancellationToken = default) =>
        Respond(ControlOperation.SetHudEnabled);

    public Task<ControlClientResult<SettingsSnapshot>> SetStartWithWindowsAsync(bool enabled, CancellationToken cancellationToken = default) =>
        Respond(ControlOperation.SetStartWithWindows);

    public Task<ControlClientResult<SettingsSnapshot>> SetIntelVrrRangeFixEnabledAsync(bool enabled, CancellationToken cancellationToken = default) =>
        Respond(ControlOperation.SetIntelVrrRangeFixEnabled);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudVisibilityModeAsync(WireVisibilityMode mode, CancellationToken cancellationToken = default) =>
        Respond(ControlOperation.SetHudVisibilityMode);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudSizeOffsetAsync(int offset, CancellationToken cancellationToken = default) =>
        Respond(ControlOperation.SetHudSizeOffset);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudFontAsync(WireFont font, CancellationToken cancellationToken = default) =>
        Respond(ControlOperation.SetHudFont);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudAlignmentAsync(WireAlignment alignment, CancellationToken cancellationToken = default) =>
        Respond(ControlOperation.SetHudAlignment);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudBackgroundModeAsync(WireBackgroundMode mode, CancellationToken cancellationToken = default) =>
        Respond(ControlOperation.SetHudBackgroundMode);

    public Task<ControlClientResult<SettingsSnapshot>> PreviewHudOpacityAsync(ushort opacityPercent, CancellationToken cancellationToken = default)
    {
        lock (_sync)
            PreviewValues.Add(opacityPercent);
        return Respond(ControlOperation.PreviewHudOpacity);
    }

    public Task<ControlClientResult<SettingsSnapshot>> CommitHudOpacityAsync(ushort opacityPercent, CancellationToken cancellationToken = default)
    {
        lock (_sync)
            CommitValues.Add(opacityPercent);
        return Respond(ControlOperation.CommitHudOpacity);
    }
}
