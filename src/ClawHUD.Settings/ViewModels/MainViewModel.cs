using System.ComponentModel;
using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;

namespace ClawHUD.Settings.ViewModels;

/// <summary>
/// Projection of the latest authoritative runtime <see cref="SettingsSnapshot"/>.
/// It never owns persisted settings and never writes files. A user action sends
/// one mutation through <see cref="IRuntimeControlClient"/>; only the snapshot
/// the runtime returns replaces local state. At most one mutation runs at a time.
/// </summary>
public sealed class MainViewModel : INotifyPropertyChanged
{
    private readonly IRuntimeControlClient _client;
    private readonly OpacityInteractionCoordinator _opacity;
    private SettingsSnapshot? _snapshot;
    private bool _mutationInFlight;

    internal MainViewModel(IRuntimeControlClient client)
    {
        _client = client;
        _opacity = new OpacityInteractionCoordinator(
            client,
            () => _snapshot?.BackgroundOpacityPercent ?? (ushort)ControlProtocol.MinOpacityPercent,
            ApplySnapshot,
            RaiseAllChanged);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

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

    // Discrete card 1-3 controls (everything except the opacity slider) are
    // interactive only with an authoritative snapshot, no discrete mutation in
    // flight, and no opacity interaction active.
    public bool AreDiscreteHudControlsEnabled =>
        _snapshot is not null && !_mutationInFlight && !_opacity.IsBusy;

    // The opacity slider stays enabled through its own preview IPC so a drag is
    // never interrupted; it only yields to a discrete mutation.
    public bool IsOpacitySliderEnabled => _snapshot is not null && !_mutationInFlight;

    public bool IsOpacityInteractionActive => _opacity.IsActive;

    public bool HudEnabled => _snapshot?.HudEnabled ?? false;

    public bool IsVisibilityAlways => _snapshot?.VisibilityMode == WireVisibilityMode.Always;
    public bool IsVisibilityInGameOnly => _snapshot?.VisibilityMode == WireVisibilityMode.InGameOnly;

    public int HudSizeOffset => _snapshot?.HudSizeOffset ?? 0;
    public string HudSizeLabel => FormatSizeOffset(HudSizeOffset);
    public bool CanDecreaseHudSize => AreDiscreteHudControlsEnabled && HudSizeOffset > ControlProtocol.MinHudSizeOffset;
    public bool CanIncreaseHudSize => AreDiscreteHudControlsEnabled && HudSizeOffset < ControlProtocol.MaxHudSizeOffset;

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

    // ---- Mutation intents (cards 1-3, opacity excluded) ------------------

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

    // ---- Background opacity interaction (card 3) ------------------------

    internal void BeginOpacityInteraction()
    {
        if (_snapshot is null || _mutationInFlight || _opacity.IsBusy)
            return;
        _opacity.Begin();
    }

    internal void UpdateOpacityGesture(ushort snappedPercent) => _opacity.Update(snappedPercent);

    internal Task EndOpacityInteractionAsync() => _opacity.EndAsync();

    internal Task ChangeOpacityAsync(ushort snappedPercent)
    {
        if (_snapshot is null || _mutationInFlight || _opacity.IsBusy)
            return Task.CompletedTask;
        return _opacity.ChangeAndCommitAsync(snappedPercent);
    }

    private async Task Mutate(Func<IRuntimeControlClient, Task<ControlClientResult<SettingsSnapshot>>> operation)
    {
        if (_snapshot is null || _mutationInFlight || _opacity.IsBusy)
        {
            RaiseAllChanged();
            return;
        }

        IsMutationInFlight = true;
        try
        {
            ControlClientResult<SettingsSnapshot> result = await operation(_client);
            if (result.IsSuccess)
                _snapshot = result.Value;
            // Any failure keeps the previous authoritative snapshot; the finally
            // block re-projects it so a transiently toggled control snaps back.
        }
        finally
        {
            IsMutationInFlight = false;
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
