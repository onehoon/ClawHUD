using System.Diagnostics;
using System.Threading;

namespace ClawHUD.Settings.Services;

/// <summary>
/// Session-scoped single-instance gate for the WPF Settings frontend. One native
/// ClawHUD runtime exists per Windows session, so one Settings frontend controls
/// it: the first process to acquire the named mutex is the primary and owns the
/// window; any later process is a relay that signals the primary and exits
/// without building a window / ViewModel / IPC client.
///
/// Pure named-object synchronization (mutex for ownership, auto-reset event for
/// the activation signal) — no polling, no TCP, no second pipe. The event is
/// created early so a relay launched while the primary is still constructing its
/// window cannot lose its activation request (the auto-reset event stays
/// signalled until the primary registers its wait).
/// </summary>
internal sealed class SettingsInstanceCoordinator : IDisposable
{
    private readonly string _mutexName;
    private readonly EventWaitHandle _activationEvent;
    private Mutex? _ownershipMutex;
    private RegisteredWaitHandle? _registeredWait;
    private bool _disposed;

    internal SettingsInstanceCoordinator()
        : this(CurrentSessionSuffix())
    {
    }

    // Test seam: a unique suffix keeps CI runs from colliding with a developer's
    // real ClawHUD.Settings instance. Never used by production startup.
    internal SettingsInstanceCoordinator(string nameSuffix)
    {
        _mutexName = BuildName("Instance", nameSuffix);
        _activationEvent = new EventWaitHandle(false, EventResetMode.AutoReset,
            BuildName("Activate", nameSuffix));
    }

    /// <summary>Raised (on a thread-pool thread) when a relay asks the primary to come forward.</summary>
    internal event Action? ActivationRequested;

    internal static string BuildName(string kind, string suffix) =>
        $@"Local\ClawHUD.Settings.{kind}.{suffix}";

    private static string CurrentSessionSuffix() =>
        Process.GetCurrentProcess().SessionId.ToString(System.Globalization.CultureInfo.InvariantCulture);

    /// <summary>
    /// Attempts to become the primary Settings process. True → this process owns
    /// the window and an activation-signal wait is now registered. False → another
    /// process is primary; the caller should <see cref="SignalPrimary"/> and exit.
    /// </summary>
    internal bool TryAcquirePrimary()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);

        _ownershipMutex = new Mutex(initiallyOwned: true, _mutexName, out bool createdNew);
        if (!createdNew)
        {
            _ownershipMutex.Dispose();
            _ownershipMutex = null;
            return false;
        }

        _registeredWait = ThreadPool.RegisterWaitForSingleObject(
            _activationEvent,
            static (state, _) => ((SettingsInstanceCoordinator)state!).ActivationRequested?.Invoke(),
            this,
            Timeout.Infinite,
            executeOnlyOnce: false);
        return true;
    }

    /// <summary>Relay path: wake the primary so it brings its window forward.</summary>
    internal void SignalPrimary() => _activationEvent.Set();

    public void Dispose()
    {
        if (_disposed)
            return;
        _disposed = true;

        _registeredWait?.Unregister(null);
        _registeredWait = null;

        if (_ownershipMutex is not null)
        {
            try { _ownershipMutex.ReleaseMutex(); } catch (ApplicationException) { }
            _ownershipMutex.Dispose();
            _ownershipMutex = null;
        }

        _activationEvent.Dispose();
    }
}
