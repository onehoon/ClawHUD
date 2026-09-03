using System.ComponentModel;
using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;

namespace ClawHUD.Settings.ViewModels;

/// <summary>
/// Projection of the latest authoritative runtime <see cref="SettingsSnapshot"/>.
/// It never owns persisted settings and never writes files. A user action sends
/// one mutation through <see cref="IRuntimeControlClient"/>; only the snapshot the
/// runtime returns replaces local state. At most one mutation runs at a time, and
/// discrete controls, the opacity interaction, and activation refresh are
/// mutually exclusive. Terminal runtime loss at any IPC point raises
/// <see cref="RuntimeLost"/> so the window can close cleanly.
/// </summary>
public sealed class MainViewModel : INotifyPropertyChanged
{
    private readonly IRuntimeControlClient _client;
    private readonly OpacityInteractionCoordinator _opacity;
    private SettingsSnapshot? _snapshot;
    private bool _mutationInFlight;
    private bool _refreshInFlight;
    private bool _runtimeLostRaised;

    internal MainViewModel(IRuntimeControlClient client)
    {
        _client = client;
        _opacity = new OpacityInteractionCoordinator(
            client,
            () => _snapshot?.BackgroundOpacityPercent ?? (ushort)ControlProtocol.MinOpacityPercent,
            ApplySnapshot,
            RaiseAllChanged,
            HandleFailedResult);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>Raised once when an IPC result proves the runtime can no longer be controlled.</summary>
    public event Action? RuntimeLost;

    internal void ApplySnapshot(SettingsSnapshot snapshot)
    {
        _snapshot = snapshot;
        RaiseAllChanged();
    }

    public bool IsMutationInFlight
    {
        get => _mutationInFlight;
        private set
        {
            _mutationInFlight = value;
            RaiseAllChanged();
        }
    }

    // Discrete controls across all five cards are interactive only with an
    // authoritative snapshot and no discrete mutation, activation refresh, or
    // opacity interaction in flight. The availability state mirrors the guard in
    // CanStartInteraction() so a click during a busy window is never accepted by
    // WPF only to be silently dropped by the ViewModel.
    public bool AreDiscreteSettingsControlsEnabled =>
        _snapshot is not null && !_mutationInFlight && !_refreshInFlight && !_opacity.IsBusy;

    // The opacity slider stays enabled through its own preview IPC so a drag is
    // never interrupted; it yields to a discrete mutation or an activation refresh
    // (neither of which can begin while an opacity gesture is active).
    public bool IsOpacitySliderEnabled =>
        _snapshot is not null && !_mutationInFlight && !_refreshInFlight;

    public bool IsOpacityInteractionActive => _opacity.IsActive;

    public bool HudEnabled => _snapshot?.HudEnabled ?? false;

    public bool IsVisibilityAlways => _snapshot?.VisibilityMode == WireVisibilityMode.Always;
    public bool IsVisibilityInGameOnly => _snapshot?.VisibilityMode == WireVisibilityMode.InGameOnly;

    public int HudSizeOffset => _snapshot?.HudSizeOffset ?? 0;
    public string HudSizeLabel => FormatSizeOffset(HudSizeOffset);
    public bool CanDecreaseHudSize => AreDiscreteSettingsControlsEnabled && HudSizeOffset > ControlProtocol.MinHudSizeOffset;
    public bool CanIncreaseHudSize => AreDiscreteSettingsControlsEnabled && HudSizeOffset < ControlProtocol.MaxHudSizeOffset;

    public bool IsFontUnispace => _snapshot?.HudFont == WireFont.Unispace;
    public bool IsFontSegoeUiVariable => _snapshot?.HudFont == WireFont.SegoeUiVariable;

    public bool IsAlignmentLeft => _snapshot?.Alignment == WireAlignment.Left;
    public bool IsAlignmentCenter => _snapshot?.Alignment == WireAlignment.Center;
    public bool IsAlignmentRight => _snapshot?.Alignment == WireAlignment.Right;

    public bool IsBackgroundFullWidth => _snapshot?.BackgroundMode == WireBackgroundMode.FullWidth;
    public bool IsBackgroundContentWidth => _snapshot?.BackgroundMode == WireBackgroundMode.ContentWidth;

    // While dragging, the slider thumb and % label follow the ephemeral gesture
    // value; otherwise both come straight from the authoritative snapshot.
    public double SliderOpacityValue => _opacity.IsActive
        ? _opacity.GestureValue
        : _snapshot?.BackgroundOpacityPercent ?? ControlProtocol.MinOpacityPercent;

    public string BackgroundOpacityText => _opacity.IsActive
        ? $"{_opacity.GestureValue}%"
        : _snapshot is null ? string.Empty : $"{_snapshot.BackgroundOpacityPercent}%";

    public bool IntelVrrRangeFixEnabled => _snapshot?.IntelVrrRangeFixEnabled ?? false;
    public bool StartWithWindows => _snapshot?.StartWithWindows ?? false;

    // ---- Discrete mutation intents -----------------------------------

    internal Task ToggleHudEnabledAsync() =>
        Mutate(c => c.SetHudEnabledAsync(!HudEnabled));

    internal Task SelectVisibilityModeAsync(WireVisibilityMode mode) =>
        _snapshot?.VisibilityMode == mode
            ? ReassertProjection()
            : Mutate(c => c.SetHudVisibilityModeAsync(mode));

    internal Task SelectFontAsync(WireFont font) =>
        _snapshot?.HudFont == font
            ? ReassertProjection()
            : Mutate(c => c.SetHudFontAsync(font));

    internal Task SelectAlignmentAsync(WireAlignment alignment) =>
        _snapshot?.Alignment == alignment
            ? ReassertProjection()
            : Mutate(c => c.SetHudAlignmentAsync(alignment));

    internal Task SelectBackgroundModeAsync(WireBackgroundMode mode) =>
        _snapshot?.BackgroundMode == mode
            ? ReassertProjection()
            : Mutate(c => c.SetHudBackgroundModeAsync(mode));

    internal Task StepHudSizeAsync(int delta)
    {
        if (_snapshot is null)
            return ReassertProjection();
        int target = _snapshot.HudSizeOffset + delta;
        return WireValue.IsHudSizeOffset(target)
            ? Mutate(c => c.SetHudSizeOffsetAsync(target))
            : ReassertProjection();
    }

    internal Task ToggleIntelVrrRangeFixAsync() =>
        Mutate(c => c.SetIntelVrrRangeFixEnabledAsync(!IntelVrrRangeFixEnabled));

    internal Task ToggleStartWithWindowsAsync() =>
        Mutate(c => c.SetStartWithWindowsAsync(!StartWithWindows));

    // ---- Background opacity interaction (card 3) --------------------

    internal void BeginOpacityInteraction()
    {
        if (!CanStartInteraction())
            return;
        _opacity.Begin();
    }

    internal void UpdateOpacityGesture(ushort snappedPercent) => _opacity.Update(snappedPercent);

    internal Task EndOpacityInteractionAsync() => _opacity.EndAsync();

    internal Task ChangeOpacityAsync(ushort snappedPercent) =>
        CanStartInteraction() ? _opacity.ChangeAndCommitAsync(snappedPercent) : Task.CompletedTask;

    // ---- Activation-time authoritative refresh (§12) --------------

    /// <summary>
    /// Re-read the authoritative snapshot when the window is re-activated. Skipped
    /// (not queued) while any interaction is active; never fetches GetRuntimeInfo.
    /// </summary>
    internal async Task RefreshOnActivationAsync()
    {
        if (_snapshot is null || _mutationInFlight || _refreshInFlight || _opacity.IsBusy)
            return;

        _refreshInFlight = true;
        RaiseAllChanged(); // disable the mutation controls for the refresh window
        try
        {
            ControlClientResult<SettingsSnapshot> result = await _client.GetSettingsSnapshotAsync();
            if (result.IsSuccess)
                _snapshot = result.Value;
            else
                HandleFailedResult(result);
        }
        finally
        {
            _refreshInFlight = false;
            RaiseAllChanged();
        }
    }

    // ---- Shared mutation plumbing ---------------------------------

    private bool CanStartInteraction() =>
        _snapshot is not null && !_mutationInFlight && !_refreshInFlight && !_opacity.IsBusy;

    private async Task Mutate(Func<IRuntimeControlClient, Task<ControlClientResult<SettingsSnapshot>>> operation)
    {
        if (!CanStartInteraction())
        {
            RaiseAllChanged();
            return;
        }

        IsMutationInFlight = true;
        try
        {
            ControlClientResult<SettingsSnapshot> result = await operation(_client);
            if (result.IsSuccess)
                _snapshot = result.Value; // the returned snapshot is authoritative — never the requested value
            else
                HandleFailedResult(result);
        }
        finally
        {
            IsMutationInFlight = false;
        }
    }

    private void HandleFailedResult(ControlClientResult<SettingsSnapshot> result)
    {
        // The previous authoritative snapshot is kept. A terminal loss additionally
        // closes the frontend (a plain timeout is left recoverable).
        if (!_runtimeLostRaised && RuntimeLoss.IsTerminal(result))
        {
            _runtimeLostRaised = true;
            RuntimeLost?.Invoke();
        }
    }

    // A no-op user action (redundant selection, size boundary, before load) must
    // still push authoritative state back onto any control the click toggled.
    private Task ReassertProjection()
    {
        RaiseAllChanged();
        return Task.CompletedTask;
    }

    private void RaiseAllChanged() =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(string.Empty));

    private static string FormatSizeOffset(int offset) => offset switch
    {
        0 => "Default",
        > 0 => $"+{offset}",
        _ => offset.ToString(),
    };
}
