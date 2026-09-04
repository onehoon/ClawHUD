using ClawHUD.Settings.Services;
using Xunit;

namespace ClawHUD.Settings.Tests;

public sealed class RuntimeProcessLifetimeWatcherTests
{
    [Fact]
    public void Parser_DistinguishesAbsentAndMalformed()
    {
        Assert.Equal(RuntimePidArgumentState.Absent,
            RuntimePidArgument.Parse(Array.Empty<string>()).State);
        Assert.Equal(RuntimePidArgumentState.Malformed,
            RuntimePidArgument.Parse(new[] { "--runtime-pid", "0" }).State);
        Assert.Equal(RuntimePidArgumentState.Malformed,
            RuntimePidArgument.Parse(new[] { "--runtime-pid", "2147483648" }).State);
        var valid = RuntimePidArgument.Parse(new[] { "--runtime-pid", "1234" });
        Assert.Equal(RuntimePidArgumentState.Valid, valid.State);
        Assert.Equal(1234, valid.Pid);
    }

    [Fact]
    public void AlreadyExitedProcess_NotifiesOnce()
    {
        var observer = new FakeObserver { HasExited = true };
        int notifications = 0;
        Assert.True(RuntimeProcessLifetimeWatcher.TryStart(1, () => notifications++,
            out var watcher, out _, _ => observer));
        using (watcher!)
        {
            observer.RaiseExit();
            Assert.Equal(1, notifications);
        }
    }

    [Fact]
    public void LaterExit_NotifiesOnce()
    {
        var observer = new FakeObserver();
        int notifications = 0;
        Assert.True(RuntimeProcessLifetimeWatcher.TryStart(1, () => notifications++,
            out var watcher, out _, _ => observer));
        using (watcher!)
        {
            observer.RaiseExit();
            observer.RaiseExit();
            Assert.Equal(1, notifications);
        }
    }

    [Fact]
    public void InitializationFailure_IsReportedAndDoesNotCreateWatcher()
    {
        Assert.False(RuntimeProcessLifetimeWatcher.TryStart(1, () => { }, out var watcher,
            out var error, _ => throw new InvalidOperationException("denied")));
        Assert.Null(watcher);
        Assert.IsType<InvalidOperationException>(error);
    }

    [Fact]
    public void Disposal_PreventsLateExitCallback()
    {
        var observer = new FakeObserver();
        int notifications = 0;
        Assert.True(RuntimeProcessLifetimeWatcher.TryStart(1, () => notifications++,
            out var watcher, out _, _ => observer));
        watcher!.Dispose();
        observer.RaiseExit();
        Assert.Equal(0, notifications);
    }

    private sealed class FakeObserver : IRuntimeProcessObserver
    {
        public bool HasExited { get; set; }
        public event EventHandler? Exited;
        public void RaiseExit() => Exited?.Invoke(this, EventArgs.Empty);
        public void Dispose() { }
    }
}
