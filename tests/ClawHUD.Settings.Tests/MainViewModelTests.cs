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
        ushort opacity = 70) =>
        new(StartWithWindows: true, HudEnabled: hudEnabled, HudSizeOffset: size, HudFont: font,
            VisibilityMode: visibility, Alignment: alignment, BackgroundMode: background,
            BackgroundOpacityPercent: opacity, IntelVrrRangeFixEnabled: false, IntelVrrLastResult: null);

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

        Assert.False(vm.AreDiscreteHudControlsEnabled);
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
        Assert.True(vm.AreDiscreteHudControlsEnabled);
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
}
