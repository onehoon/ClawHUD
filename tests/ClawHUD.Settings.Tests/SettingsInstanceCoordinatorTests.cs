using ClawHUD.Settings.Services;
using Xunit;

namespace ClawHUD.Settings.Tests;

public class SettingsInstanceCoordinatorTests
{
    // Unique per test so a CI run never collides with a developer's real
    // ClawHUD.Settings instance or with a sibling test.
    private static string Suffix() => $"test-{Guid.NewGuid():N}";

    [Fact] // §17.1
    public void FirstInstance_AcquiresPrimaryOwnership()
    {
        using var coordinator = new SettingsInstanceCoordinator(Suffix());
        Assert.True(coordinator.TryAcquirePrimary());
    }

    [Fact] // §17.2 — second instance is relay-only; its signal reaches the primary
    public void SecondInstance_IsNotPrimary_AndItsSignalReachesPrimary()
    {
        string suffix = Suffix();

        using var primary = new SettingsInstanceCoordinator(suffix);
        Assert.True(primary.TryAcquirePrimary());

        using var activated = new ManualResetEventSlim(false);
        int activationThreadId = 0;
        primary.ActivationRequested += () =>
        {
            activationThreadId = Environment.CurrentManagedThreadId;
            activated.Set();
        };

        using (var secondary = new SettingsInstanceCoordinator(suffix))
        {
            Assert.False(secondary.TryAcquirePrimary()); // relay: never becomes primary
            secondary.SignalPrimary();
        }

        Assert.True(activated.Wait(TimeSpan.FromSeconds(5)));
        // §17.5: the callback arrives on a thread-pool thread, so real activation
        // must be marshalled onto the WPF Dispatcher (done in App.OnActivationRequested).
        Assert.NotEqual(Environment.CurrentManagedThreadId, activationThreadId);
    }

    [Fact] // §17.3 — ownership is released on dispose
    public void PrimaryOwnership_IsReleasedOnDispose()
    {
        string suffix = Suffix();

        var first = new SettingsInstanceCoordinator(suffix);
        Assert.True(first.TryAcquirePrimary());
        first.Dispose();

        using var second = new SettingsInstanceCoordinator(suffix);
        Assert.True(second.TryAcquirePrimary());
    }

    [Fact] // §17.4 — production names are Local + session-scoped
    public void BuildName_IsLocalScopedAndIncludesSuffix()
    {
        string name = SettingsInstanceCoordinator.BuildName("Instance", "42");

        Assert.StartsWith(@"Local\ClawHUD.Settings.", name);
        Assert.Contains("Instance", name);
        Assert.EndsWith(".42", name);
    }

    [Fact]
    public void TryAcquirePrimary_AfterDispose_Throws()
    {
        var coordinator = new SettingsInstanceCoordinator(Suffix());
        coordinator.Dispose();
        Assert.Throws<ObjectDisposedException>(() => coordinator.TryAcquirePrimary());
    }
}
