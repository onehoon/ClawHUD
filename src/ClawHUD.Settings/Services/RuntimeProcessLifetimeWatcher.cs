using System.Diagnostics;

namespace ClawHUD.Settings.Services;

internal enum RuntimePidArgumentState
{
    Absent,
    Malformed,
    Valid,
}

internal readonly record struct RuntimePidArgument(RuntimePidArgumentState State, int Pid)
{
    internal static RuntimePidArgument Parse(IReadOnlyList<string> arguments)
    {
        int? pid = null;
        for (int i = 0; i < arguments.Count; i++)
        {
            if (!string.Equals(arguments[i], "--runtime-pid", StringComparison.Ordinal))
                continue;
            if (pid.HasValue || ++i >= arguments.Count ||
                !int.TryParse(arguments[i], out int parsed) || parsed <= 0)
                return new(RuntimePidArgumentState.Malformed, 0);
            pid = parsed;
        }
        return pid.HasValue
            ? new(RuntimePidArgumentState.Valid, pid.Value)
            : new(RuntimePidArgumentState.Absent, 0);
    }
}

internal interface IRuntimeProcessObserver : IDisposable
{
    event EventHandler? Exited;
    bool HasExited { get; }
}

internal sealed class RuntimeProcessLifetimeWatcher : IDisposable
{
    private readonly IRuntimeProcessObserver _observer;
    private readonly Action _onExit;
    private int _notified;
    private int _disposed;

    private RuntimeProcessLifetimeWatcher(IRuntimeProcessObserver observer, Action onExit)
    {
        _observer = observer;
        _onExit = onExit;
        _observer.Exited += OnExited;
        if (_observer.HasExited)
            NotifyExit();
    }

    internal static bool TryStart(int pid, Action onExit,
        out RuntimeProcessLifetimeWatcher? watcher, out Exception? error,
        Func<int, IRuntimeProcessObserver>? observerFactory = null)
    {
        IRuntimeProcessObserver? observer = null;
        try
        {
            observer = (observerFactory ?? CreateObserver)(pid);
            watcher = new RuntimeProcessLifetimeWatcher(observer, onExit);
            error = null;
            return true;
        }
        catch (Exception exception)
        {
            observer?.Dispose();
            watcher = null;
            error = exception;
            return false;
        }
    }

    private static IRuntimeProcessObserver CreateObserver(int pid) =>
        new ProcessRuntimeProcessObserver(Process.GetProcessById(pid));

    private void OnExited(object? sender, EventArgs e) => NotifyExit();

    private void NotifyExit()
    {
        if (Volatile.Read(ref _disposed) != 0 ||
            Interlocked.Exchange(ref _notified, 1) != 0)
            return;
        try { _onExit(); }
        catch { /* lifetime cleanup must never escape the observer callback */ }
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0)
            return;
        _observer.Exited -= OnExited;
        _observer.Dispose();
    }

    private sealed class ProcessRuntimeProcessObserver : IRuntimeProcessObserver
    {
        private readonly Process _process;

        internal ProcessRuntimeProcessObserver(Process process)
        {
            _process = process;
            _process.EnableRaisingEvents = true;
        }

        public event EventHandler? Exited
        {
            add => _process.Exited += value;
            remove => _process.Exited -= value;
        }

        public bool HasExited => _process.HasExited;

        public void Dispose() => _process.Dispose();
    }
}
