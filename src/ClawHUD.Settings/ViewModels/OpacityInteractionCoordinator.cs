using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;

namespace ClawHUD.Settings.ViewModels;

/// <summary>
/// Drives the background-opacity slider gesture. A drag issues live
/// <c>PreviewHudOpacity</c> requests (serialized, latest-value coalesced, at most
/// one in flight); releasing the drag sends exactly one <c>CommitHudOpacity</c>
/// when the gesture changed the value — even if the newest preview snapshot
/// already reports that value (a preview is applied but not persisted). Keyboard
/// / track-click changes go straight to a single commit.
///
/// State is guarded by a lock so the async pump is correct without depending on a
/// captured synchronization context; awaits never run under the lock.
/// </summary>
internal sealed class OpacityInteractionCoordinator(
    IRuntimeControlClient client,
    Func<ushort> currentRuntimeOpacity,
    Action<SettingsSnapshot> applySnapshot,
    Action stateChanged)
{
    private readonly object _gate = new();

    private bool _active;
    private bool _finalizing;
    private bool _discreteCommitInFlight;
    private bool _dirty;
    private bool _pumpRunning;

    private ushort _gestureValue;
    private ushort? _pendingPreview;
    private ushort? _lastSuccessfulPreview;
    private Task _previewPump = Task.CompletedTask;

    internal bool IsActive
    {
        get { lock (_gate) return _active; }
    }

    internal bool IsBusy
    {
        get { lock (_gate) return _active || _finalizing || _discreteCommitInFlight; }
    }

    internal ushort GestureValue
    {
        get { lock (_gate) return _gestureValue; }
    }

    /// <summary>Begin a drag gesture, anchored at the current authoritative opacity.</summary>
    internal void Begin()
    {
        lock (_gate)
        {
            if (_active || _finalizing || _discreteCommitInFlight)
                return;
            _active = true;
            _finalizing = false;
            _dirty = false;
            _gestureValue = currentRuntimeOpacity();
            _pendingPreview = null;
            _lastSuccessfulPreview = null;
        }
        stateChanged();
    }

    /// <summary>Report a new snapped gesture value during the drag.</summary>
    internal void Update(ushort snappedPercent)
    {
        bool startPump = false;
        lock (_gate)
        {
            if (!_active || _finalizing || snappedPercent == _gestureValue)
                return;
            _gestureValue = snappedPercent;
            _dirty = true;
            _pendingPreview = snappedPercent;
            if (!_pumpRunning)
            {
                _pumpRunning = true;
                startPump = true;
            }
        }
        if (startPump)
            _previewPump = RunPreviewPumpAsync();
        stateChanged();
    }

    /// <summary>Finish the drag: drain any in-flight preview, then commit once if dirty.</summary>
    internal async Task EndAsync()
    {
        Task pump;
        lock (_gate)
        {
            if (!_active || _finalizing)
                return;
            _finalizing = true;
            _pendingPreview = null; // discard stale intermediate previews
            pump = _previewPump;
        }
        stateChanged();

        await pump; // let an already-issued preview complete; the pump exits on _finalizing

        ushort finalValue;
        bool dirty;
        lock (_gate)
        {
            finalValue = _gestureValue;
            dirty = _dirty;
        }

        if (dirty)
        {
            ControlClientResult<SettingsSnapshot> result = await client.CommitHudOpacityAsync(finalValue);
            if (result.IsSuccess)
                applySnapshot(result.Value!);
            // On failure the last authoritative snapshot is retained; the slider
            // snaps back to it once the interaction clears below.
        }

        lock (_gate)
        {
            _active = false;
            _finalizing = false;
            _dirty = false;
        }
        stateChanged();
    }

    /// <summary>A discrete (keyboard / track-click) opacity change: one commit, no preview.</summary>
    internal async Task ChangeAndCommitAsync(ushort snappedPercent)
    {
        lock (_gate)
        {
            if (_active || _finalizing || _discreteCommitInFlight ||
                snappedPercent == currentRuntimeOpacity())
                return;
            _discreteCommitInFlight = true;
        }
        stateChanged();

        ControlClientResult<SettingsSnapshot> result = await client.CommitHudOpacityAsync(snappedPercent);
        if (result.IsSuccess)
            applySnapshot(result.Value!);

        lock (_gate)
            _discreteCommitInFlight = false;
        stateChanged();
    }

    private async Task RunPreviewPumpAsync()
    {
        while (true)
        {
            ushort next;
            lock (_gate)
            {
                if (_finalizing || _pendingPreview is null || _pendingPreview == _lastSuccessfulPreview)
                {
                    _pendingPreview = null;
                    _pumpRunning = false;
                    return;
                }
                next = _pendingPreview.Value;
                _pendingPreview = null;
            }

            ControlClientResult<SettingsSnapshot> result = await client.PreviewHudOpacityAsync(next);
            if (result.IsSuccess)
            {
                lock (_gate)
                    _lastSuccessfulPreview = next;
                applySnapshot(result.Value!);
            }
            // A failed preview is not retried; the loop simply picks up the next
            // pending value (or exits), keeping the gesture responsive.
            stateChanged();
        }
    }
}
