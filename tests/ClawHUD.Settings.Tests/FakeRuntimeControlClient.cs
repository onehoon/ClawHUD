using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;

namespace ClawHUD.Settings.Tests;

/// <summary>
/// In-memory <see cref="IRuntimeControlClient"/> for ViewModel tests. Records the
/// operations it was asked to perform and returns a configurable result; an
/// optional gate holds a mutation "in flight" until the test releases it.
/// </summary>
internal sealed class FakeRuntimeControlClient : IRuntimeControlClient
{
    internal List<ControlOperation> Calls { get; } = [];
    internal SettingsSnapshot? NextSnapshot { get; set; }
    internal ControlClientOutcome NextOutcome { get; set; } = ControlClientOutcome.Success;
    internal ControlStatus NextStatus { get; set; } = ControlStatus.OperationFailed;
    internal TaskCompletionSource? Gate { get; set; }

    private async Task<ControlClientResult<SettingsSnapshot>> Mutate(ControlOperation operation)
    {
        Calls.Add(operation);
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

    public Task<ControlClientResult<RuntimeInfo>> GetRuntimeInfoAsync(CancellationToken cancellationToken = default) =>
        Task.FromResult(ControlClientResult<RuntimeInfo>.Success(
            new RuntimeInfo("test", 1, 1, WireLaunchMode.Standalone, WireRuntimeState.Ready)));

    public Task<ControlClientResult<SettingsSnapshot>> GetSettingsSnapshotAsync(CancellationToken cancellationToken = default) =>
        Task.FromResult(ControlClientResult<SettingsSnapshot>.Success(NextSnapshot!));

    public Task<ControlClientResult<SettingsSnapshot>> SetHudEnabledAsync(bool enabled, CancellationToken cancellationToken = default) =>
        Mutate(ControlOperation.SetHudEnabled);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudVisibilityModeAsync(WireVisibilityMode mode, CancellationToken cancellationToken = default) =>
        Mutate(ControlOperation.SetHudVisibilityMode);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudSizeOffsetAsync(int offset, CancellationToken cancellationToken = default) =>
        Mutate(ControlOperation.SetHudSizeOffset);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudFontAsync(WireFont font, CancellationToken cancellationToken = default) =>
        Mutate(ControlOperation.SetHudFont);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudAlignmentAsync(WireAlignment alignment, CancellationToken cancellationToken = default) =>
        Mutate(ControlOperation.SetHudAlignment);

    public Task<ControlClientResult<SettingsSnapshot>> SetHudBackgroundModeAsync(WireBackgroundMode mode, CancellationToken cancellationToken = default) =>
        Mutate(ControlOperation.SetHudBackgroundMode);
}
