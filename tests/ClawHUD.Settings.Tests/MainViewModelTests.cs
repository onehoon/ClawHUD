using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;
using ClawHUD.Settings.ViewModels;
using Xunit;

namespace ClawHUD.Settings.Tests;

public class MainViewModelTests
{
    private static SettingsSnapshot Snapshot(
        bool hudEnabled = true,
        int size = 0,
        WireFont font = WireFont.Unispace,
        WireVisibilityMode visibility = WireVisibilityMode.Always,
        WireAlignment alignment = WireAlignment.Center,
        WireBackgroundMode background = WireBackgroundMode.ContentWidth,
        ushort opacity = 70,
        bool startWithWindows = true,
        bool intelVrr = false) =>
        new(StartWithWindows: startWithWindows, HudEnabled: hudEnabled, HudSizeOffset: size, HudFont: font,
            VisibilityMode: visibility, Alignment: alignment, BackgroundMode: background,
            BackgroundOpacityPercent: opacity, IntelVrrRangeFixEnabled: intelVrr, IntelVrrLastResult: null);

    private static (MainViewModel Vm, FakeRuntimeControlClient Fake) Loaded(SettingsSnapshot? initial = null)
    {
        var fake = new FakeRuntimeControlClient();
        var vm = new MainViewModel(fake);
        vm.ApplySnapshot(initial ?? Snapshot());
        return (vm, fake);
    }

    // ---- Projection --------------------------------------------------

    [Theory]
    [InlineData(-2, "-2")]
    [InlineData(-1, "-1")]
    [InlineData(0, "Default")]
    [InlineData(1, "+1")]
    [InlineData(2, "+2")]
    public void HudSizeLabel_MapsOffset(int offset, string expected) =>
        Assert.Equal(expected, Loaded(Snapshot(size: offset)).Vm.HudSizeLabel);

    [Fact]
    public void Projection_MapsMutuallyExclusiveChoices()
    {
        var (vm, _) = Loaded(Snapshot(font: WireFont.SegoeUiVariable, visibility: WireVisibilityMode.InGameOnly,
            alignment: WireAlignment.Right, background: WireBackgroundMode.FullWidth, opacity: 55));

        Assert.True(vm.IsFontSegoeUiVariable);
        Assert.False(vm.IsFontUnispace);
        Assert.True(vm.IsVisibilityInGameOnly);
        Assert.True(vm.IsAlignmentRight);
        Assert.True(vm.IsBackgroundFullWidth);
        Assert.Equal(55, vm.SliderOpacityValue);
        Assert.Equal("55%", vm.BackgroundOpacityText);
    }

    [Fact]
    public void BeforeSnapshot_ControlsAreNotInteractive()
    {
        var vm = new MainViewModel(new FakeRuntimeControlClient());

        Assert.False(vm.AreDiscreteSettingsControlsEnabled);
        Assert.False(vm.CanIncreaseHudSize);
        Assert.False(vm.CanDecreaseHudSize);
        Assert.Equal(string.Empty, vm.BackgroundOpacityText);
    }

    // ---- Size boundaries (§16.7) -----------------------------------

    [Fact]
    public void HudSize_MinBoundary_DisablesDecrease()
    {
        var (vm, _) = Loaded(Snapshot(size: -2));
        Assert.False(vm.CanDecreaseHudSize);
        Assert.True(vm.CanIncreaseHudSize);
    }

    [Fact]
    public void HudSize_MaxBoundary_DisablesIncrease()
    {
        var (vm, _) = Loaded(Snapshot(size: 2));
        Assert.True(vm.CanDecreaseHudSize);
        Assert.False(vm.CanIncreaseHudSize);
    }

    [Fact]
    public async Task StepHudSize_AtBoundary_SendsNothing()
    {
        var (vm, fake) = Loaded(Snapshot(size: 2));
        await vm.StepHudSizeAsync(+1);
        Assert.Empty(fake.Calls);
    }

    [Fact]
    public async Task StepHudSize_ComputesFromAuthoritativeOffsetNotLabel()
    {
        var (vm, fake) = Loaded(Snapshot(size: 1));
        fake.NextSnapshot = Snapshot(size: 2);

        await vm.StepHudSizeAsync(+1);

        Assert.Equal(new[] { ControlOperation.SetHudSizeOffset }, fake.Calls);
        Assert.Equal(2, vm.HudSizeOffset);
        Assert.Equal("+2", vm.HudSizeLabel);
    }

    // ---- Authoritative reconciliation (§16.6) ----------------------

    [Fact]
    public async Task SuccessfulMutation_AppliesWholeReturnedSnapshot()
    {
        var (vm, fake) = Loaded(Snapshot(alignment: WireAlignment.Center, font: WireFont.Unispace));
        // Runtime accepts the alignment change and also reverts the font.
        fake.NextSnapshot = Snapshot(alignment: WireAlignment.Right, font: WireFont.SegoeUiVariable);

        await vm.SelectAlignmentAsync(WireAlignment.Right);

        Assert.True(vm.IsAlignmentRight);
        Assert.True(vm.IsFontSegoeUiVariable);
    }

    [Fact]
    public async Task RuntimeRollback_KeepsRuntimeValueNotRequestedValue()
    {
        var (vm, fake) = Loaded(Snapshot(alignment: WireAlignment.Center));
        // User asks for Right; runtime returns Ok but with alignment still Center.
        fake.NextSnapshot = Snapshot(alignment: WireAlignment.Center);

        await vm.SelectAlignmentAsync(WireAlignment.Right);

        Assert.True(vm.IsAlignmentCenter);
        Assert.False(vm.IsAlignmentRight);
        Assert.False(vm.IsMutationInFlight);
    }

    [Fact]
    public async Task RedundantSelection_SendsNoMutation()
    {
        var (vm, fake) = Loaded(Snapshot(visibility: WireVisibilityMode.Always));
        await vm.SelectVisibilityModeAsync(WireVisibilityMode.Always);
        Assert.Empty(fake.Calls);
    }

    // ---- Failure preserves state (§12, §16.4) ---------------------

    [Theory]
    [InlineData(ControlClientOutcome.ProtocolError)]
    [InlineData(ControlClientOutcome.TransportUnavailable)]
    [InlineData(ControlClientOutcome.TimedOut)]
    [InlineData(ControlClientOutcome.MalformedResponse)]
    public async Task FailedMutation_LeavesLastAuthoritativeStateAndClearsBusy(ControlClientOutcome outcome)
    {
        var (vm, fake) = Loaded(Snapshot(hudEnabled: true, alignment: WireAlignment.Left));
        fake.NextOutcome = outcome;

        await vm.SelectAlignmentAsync(WireAlignment.Right);

        Assert.True(vm.IsAlignmentLeft);
        Assert.True(vm.HudEnabled);
        Assert.False(vm.IsMutationInFlight);
        Assert.True(vm.AreDiscreteSettingsControlsEnabled);
    }

    // ---- One mutation in flight (§11, §16.8) ---------------------

    [Fact]
    public async Task SecondMutation_IsNotDispatchedWhileFirstIsInFlight()
    {
        var (vm, fake) = Loaded(Snapshot(alignment: WireAlignment.Center));
        fake.NextSnapshot = Snapshot(alignment: WireAlignment.Right);
        var gate = new TaskCompletionSource();
        fake.Gate = gate;

        Task first = vm.SelectAlignmentAsync(WireAlignment.Right);
        Assert.True(vm.IsMutationInFlight);

        Task second = vm.SelectAlignmentAsync(WireAlignment.Left);
        Assert.True(second.IsCompleted);          // rejected synchronously by the in-flight guard
        Assert.Single(fake.Calls);

        gate.SetResult();
        await first;
        Assert.False(vm.IsMutationInFlight);
        Assert.Single(fake.Calls);
    }

    // ---- Cards 4/5: Start with Windows + Intel VRR Range Fix (§17) ----

    [Fact] // §17.1 merge-critical: native App::SetStartWithWindows rolls back internally on shortcut failure
    public async Task StartWithWindows_AuthoritativeRollback_StaysOffWhenSnapshotUnchanged()
    {
        var (vm, fake) = Loaded(Snapshot(startWithWindows: false));
        fake.NextSnapshot = Snapshot(startWithWindows: false); // Ok, but runtime rolled back

        await vm.ToggleStartWithWindowsAsync();

        Assert.Equal(new[] { ControlOperation.SetStartWithWindows }, fake.Calls);
        Assert.False(vm.StartWithWindows);
    }

    [Fact] // §17.2
    public async Task StartWithWindows_Success_ProjectsReturnedValue()
    {
        var (vm, fake) = Loaded(Snapshot(startWithWindows: false));
        fake.NextSnapshot = Snapshot(startWithWindows: true);

        await vm.ToggleStartWithWindowsAsync();

        Assert.True(vm.StartWithWindows);
    }

    [Theory] // §17.3 both directions; toggling does not run the VRR algorithm here
    [InlineData(false, true)]
    [InlineData(true, false)]
    public async Task IntelVrrRangeFix_TogglesToReturnedSnapshotValue(bool from, bool to)
    {
        var (vm, fake) = Loaded(Snapshot(intelVrr: from));
        fake.NextSnapshot = Snapshot(intelVrr: to);

        await vm.ToggleIntelVrrRangeFixAsync();

        Assert.Equal(new[] { ControlOperation.SetIntelVrrRangeFixEnabled }, fake.Calls);
        Assert.Equal(to, vm.IntelVrrRangeFixEnabled);
    }

    [Fact] // §17.4 whole-snapshot reconciliation
    public async Task Card45Mutation_AppliesWholeReturnedSnapshot()
    {
        var (vm, fake) = Loaded(Snapshot(startWithWindows: false, alignment: WireAlignment.Left, hudEnabled: true));
        fake.NextSnapshot = Snapshot(startWithWindows: true, alignment: WireAlignment.Right, hudEnabled: false);

        await vm.ToggleStartWithWindowsAsync();

        Assert.True(vm.StartWithWindows);
        Assert.True(vm.IsAlignmentRight);
        Assert.False(vm.HudEnabled);
    }

    [Fact] // §17.5
    public async Task Card45Mutation_BlockedWhileOpacityInteractionActive()
    {
        var (vm, fake) = Loaded(Snapshot(intelVrr: false));
        fake.NextSnapshot = Snapshot(intelVrr: false);
        fake.Gate = new TaskCompletionSource();

        vm.BeginOpacityInteraction();
        vm.UpdateOpacityGesture(80); // preview held -> interaction busy

        await vm.ToggleIntelVrrRangeFixAsync();
        Assert.DoesNotContain(ControlOperation.SetIntelVrrRangeFixEnabled, fake.Calls);

        fake.Gate.SetResult();
        await vm.EndOpacityInteractionAsync();
    }

    [Fact] // §17.5
    public async Task Card45Mutation_BlockedWhileAnotherDiscreteMutationInFlight()
    {
        var (vm, fake) = Loaded(Snapshot(startWithWindows: false));
        fake.NextSnapshot = Snapshot(startWithWindows: false);
        fake.Gate = new TaskCompletionSource();

        Task first = vm.ToggleStartWithWindowsAsync();
        Assert.True(vm.IsMutationInFlight);

        Task second = vm.ToggleIntelVrrRangeFixAsync();
        Assert.True(second.IsCompleted);
        Assert.Single(fake.Calls);

        fake.Gate.SetResult();
        await first;
    }

    // ---- Activation-time refresh (§18) ----

    [Fact] // §18.1
    public async Task ActivationRefresh_AppliesWholeSnapshot()
    {
        var (vm, fake) = Loaded(Snapshot(alignment: WireAlignment.Left, opacity: 70));
        fake.NextSnapshot = Snapshot(alignment: WireAlignment.Right, opacity: 90);

        await vm.RefreshOnActivationAsync();

        Assert.Equal(new[] { ControlOperation.GetSettingsSnapshot }, fake.Calls);
        Assert.True(vm.IsAlignmentRight);
        Assert.Equal(90, vm.SliderOpacityValue);
    }

    [Fact] // availability state must match the interaction guard during the refresh window
    public async Task ActivationRefresh_DisablesAllControlsWhileInFlight()
    {
        var (vm, fake) = Loaded(Snapshot());
        fake.NextSnapshot = Snapshot();
        fake.Gate = new TaskCompletionSource();

        Task refresh = vm.RefreshOnActivationAsync();

        Assert.False(vm.AreDiscreteSettingsControlsEnabled);
        Assert.False(vm.IsOpacitySliderEnabled);

        fake.Gate.SetResult();
        await refresh;

        Assert.True(vm.AreDiscreteSettingsControlsEnabled);
        Assert.True(vm.IsOpacitySliderEnabled);
    }

    [Fact] // a mutation attempted during the refresh window is rejected (controls are disabled anyway)
    public async Task Mutation_DuringActivationRefresh_IsNotDispatched()
    {
        var (vm, fake) = Loaded(Snapshot(alignment: WireAlignment.Center));
        fake.NextSnapshot = Snapshot(alignment: WireAlignment.Center);
        fake.Gate = new TaskCompletionSource();

        Task refresh = vm.RefreshOnActivationAsync();
        await vm.SelectAlignmentAsync(WireAlignment.Right);

        Assert.DoesNotContain(ControlOperation.SetHudAlignment, fake.Calls);

        fake.Gate.SetResult();
        await refresh;
    }

    [Fact] // §18.2
    public void ActivationRefresh_SkippedBeforeInitialSnapshot()
    {
        var fake = new FakeRuntimeControlClient();
        var vm = new MainViewModel(fake);

        _ = vm.RefreshOnActivationAsync();

        Assert.Empty(fake.Calls);
    }

    [Fact] // §18.2
    public async Task ActivationRefresh_SkippedWhileMutationInFlight()
    {
        var (vm, fake) = Loaded(Snapshot(alignment: WireAlignment.Center));
        fake.NextSnapshot = Snapshot(alignment: WireAlignment.Right);
        fake.Gate = new TaskCompletionSource();

        Task mutation = vm.SelectAlignmentAsync(WireAlignment.Right);
        await vm.RefreshOnActivationAsync();

        Assert.DoesNotContain(ControlOperation.GetSettingsSnapshot, fake.Calls);

        fake.Gate.SetResult();
        await mutation;
    }

    [Fact] // §18.2
    public async Task ActivationRefresh_SkippedWhileOpacityInteractionActive()
    {
        var (vm, fake) = Loaded(Snapshot(opacity: 70));
        fake.NextSnapshot = Snapshot(opacity: 70);
        fake.Gate = new TaskCompletionSource();

        vm.BeginOpacityInteraction();
        vm.UpdateOpacityGesture(80);

        await vm.RefreshOnActivationAsync();
        Assert.DoesNotContain(ControlOperation.GetSettingsSnapshot, fake.Calls);

        fake.Gate.SetResult();
        await vm.EndOpacityInteractionAsync();
    }

    // ---- Runtime-loss / clean close (§19) ----

    [Theory] // §19.2 / §19.3
    [InlineData(ControlClientOutcome.TransportUnavailable, ControlStatus.Ok)]
    [InlineData(ControlClientOutcome.ProtocolError, ControlStatus.ShuttingDown)]
    [InlineData(ControlClientOutcome.ProtocolError, ControlStatus.RuntimeUnavailable)]
    public async Task TerminalRuntimeLossDuringMutation_RaisesRuntimeLostOnceAndKeepsSnapshot(
        ControlClientOutcome outcome, ControlStatus status)
    {
        var (vm, fake) = Loaded(Snapshot(alignment: WireAlignment.Left));
        fake.NextOutcome = outcome;
        fake.NextStatus = status;
        int raised = 0;
        vm.RuntimeLost += () => raised++;

        await vm.SelectAlignmentAsync(WireAlignment.Right);
        await vm.SelectFontAsync(WireFont.SegoeUiVariable); // second terminal failure

        Assert.Equal(1, raised); // raised once, not per failure
        Assert.True(vm.IsAlignmentLeft); // previous authoritative snapshot retained
        Assert.False(vm.IsMutationInFlight);
    }

    [Fact] // §19.2
    public async Task TerminalRuntimeLossDuringActivationRefresh_RaisesRuntimeLost()
    {
        var (vm, fake) = Loaded(Snapshot());
        fake.NextOutcome = ControlClientOutcome.TransportUnavailable;
        bool raised = false;
        vm.RuntimeLost += () => raised = true;

        await vm.RefreshOnActivationAsync();

        Assert.True(raised);
    }

    [Theory] // §19.5 a plain timeout is not terminal
    [InlineData(ControlClientOutcome.TimedOut)]
    [InlineData(ControlClientOutcome.MalformedResponse)]
    public async Task NonTerminalFailure_DoesNotRaiseRuntimeLost(ControlClientOutcome outcome)
    {
        var (vm, fake) = Loaded(Snapshot(alignment: WireAlignment.Left));
        fake.NextOutcome = outcome;
        bool raised = false;
        vm.RuntimeLost += () => raised = true;

        await vm.SelectAlignmentAsync(WireAlignment.Right);

        Assert.False(raised);
        Assert.True(vm.IsAlignmentLeft);
        Assert.True(vm.AreDiscreteSettingsControlsEnabled); // recovers
    }
}
