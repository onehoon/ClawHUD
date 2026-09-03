using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;
using ClawHUD.Settings.ViewModels;
using Xunit;

namespace ClawHUD.Settings.Tests;

public class OpacityInteractionTests
{
    private static SettingsSnapshot Snap(ushort opacity, WireAlignment alignment = WireAlignment.Center) =>
        new(StartWithWindows: false, HudEnabled: true, HudSizeOffset: 0, HudFont: WireFont.Unispace,
            VisibilityMode: WireVisibilityMode.Always, Alignment: alignment,
            BackgroundMode: WireBackgroundMode.ContentWidth, BackgroundOpacityPercent: opacity,
            IntelVrrRangeFixEnabled: false, IntelVrrLastResult: null);

    private static (MainViewModel Vm, FakeRuntimeControlClient Fake) Loaded(ushort opacity = 70)
    {
        var fake = new FakeRuntimeControlClient { NextSnapshot = Snap(opacity) };
        var vm = new MainViewModel(fake);
        vm.ApplySnapshot(Snap(opacity));
        return (vm, fake);
    }

    // ---- §18.5 merge-critical: preview equality must not suppress commit ----

    [Fact]
    public async Task DirtyInteraction_CommitsOnce_EvenWhenPreviewSnapshotAlreadyEqualsFinalValue()
    {
        var (vm, fake) = Loaded(opacity: 70);
        fake.NextSnapshot = Snap(85); // every Preview/Commit response reports 85

        vm.BeginOpacityInteraction();
        vm.UpdateOpacityGesture(85);
        await vm.EndOpacityInteractionAsync();

        Assert.Equal(new ushort[] { 85 }, fake.PreviewValues);
        Assert.Equal(new ushort[] { 85 }, fake.CommitValues); // sent despite snapshot == 85
        Assert.False(vm.IsOpacityInteractionActive);
        Assert.Equal(85, vm.SliderOpacityValue);
    }

    // ---- §18.4 serialized latest-value coalescing ----

    [Fact]
    public async Task Drag_CoalescesIntermediateValues_AndCommitsFinalOnce()
    {
        var (vm, fake) = Loaded(opacity: 70);
        fake.NextSnapshot = Snap(90);
        var gate = new TaskCompletionSource();
        fake.Gate = gate;

        vm.BeginOpacityInteraction();
        vm.UpdateOpacityGesture(75); // Preview(75) held in flight
        vm.UpdateOpacityGesture(80);
        vm.UpdateOpacityGesture(85);
        vm.UpdateOpacityGesture(90);

        Assert.Equal(new ushort[] { 75 }, fake.PreviewValues); // 80/85/90 not individually dispatched

        Task end = vm.EndOpacityInteractionAsync();
        gate.SetResult();
        await end;

        Assert.Equal(new ushort[] { 75 }, fake.PreviewValues);
        Assert.Equal(new ushort[] { 90 }, fake.CommitValues);
        Assert.Equal(1, fake.MaxConcurrentCalls); // never two IPC calls at once
    }

    [Fact]
    public async Task Drag_ThumbFollowsGestureNotSnapshot_WhilePreviewInFlight()
    {
        var (vm, fake) = Loaded(opacity: 70);
        fake.NextSnapshot = Snap(70);
        fake.Gate = new TaskCompletionSource();

        vm.BeginOpacityInteraction();
        vm.UpdateOpacityGesture(85);

        Assert.Equal(85, vm.SliderOpacityValue);       // gesture value, not snapshot 70
        Assert.Equal("85%", vm.BackgroundOpacityText);
        Assert.True(vm.IsOpacitySliderEnabled);         // slider stays live during preview

        fake.Gate.SetResult();
        await vm.EndOpacityInteractionAsync();
    }

    // ---- §18.6 authoritative commit rollback ----

    [Fact]
    public async Task CommitRollback_ShowsRuntimeValueNotRequestedValue()
    {
        var (vm, fake) = Loaded(opacity: 70);

        vm.BeginOpacityInteraction();
        vm.UpdateOpacityGesture(85);
        fake.NextSnapshot = Snap(80); // runtime commits 80, not the requested 85
        await vm.EndOpacityInteractionAsync();

        Assert.Equal(80, vm.SliderOpacityValue);
        Assert.Equal("80%", vm.BackgroundOpacityText);
        Assert.False(vm.IsOpacityInteractionActive);
    }

    // ---- §18.7 commit failure ----

    [Fact]
    public async Task CommitFailure_RestoresLastAuthoritativeValueAndReenablesControls()
    {
        var (vm, fake) = Loaded(opacity: 70);
        fake.NextSnapshot = Snap(80); // last successful Preview reports 80

        vm.BeginOpacityInteraction();
        vm.UpdateOpacityGesture(80);
        fake.NextOutcome = ControlClientOutcome.TransportUnavailable; // final Commit(85) fails
        vm.UpdateOpacityGesture(85);
        await vm.EndOpacityInteractionAsync();

        Assert.Equal(80, vm.SliderOpacityValue); // last known runtime state
        Assert.False(vm.IsOpacityInteractionActive);
        Assert.True(vm.AreDiscreteSettingsControlsEnabled);
    }

    // ---- §18.2 non-dirty interaction ----

    [Fact]
    public async Task InteractionWithoutChange_SendsNothing()
    {
        var (vm, fake) = Loaded(opacity: 70);

        vm.BeginOpacityInteraction();
        await vm.EndOpacityInteractionAsync();

        Assert.Empty(fake.PreviewValues);
        Assert.Empty(fake.CommitValues);
    }

    // ---- discrete (keyboard / track-click) change ----

    [Fact]
    public async Task DiscreteChange_CommitsWithoutPreview()
    {
        var (vm, fake) = Loaded(opacity: 70);
        fake.NextSnapshot = Snap(80);

        await vm.ChangeOpacityAsync(80);

        Assert.Empty(fake.PreviewValues);
        Assert.Equal(new ushort[] { 80 }, fake.CommitValues);
        Assert.Equal(80, vm.SliderOpacityValue);
    }

    [Fact]
    public async Task DiscreteChange_ToCurrentValue_SendsNothing()
    {
        var (vm, fake) = Loaded(opacity: 70);
        await vm.ChangeOpacityAsync(70);
        Assert.Empty(fake.CommitValues);
    }

    // ---- §18.8 programmatic snapshot application ----

    [Fact]
    public void ApplySnapshot_ChangingOpacity_SendsNoMutation()
    {
        var (vm, fake) = Loaded(opacity: 70);
        vm.ApplySnapshot(Snap(95));

        Assert.Empty(fake.Calls);
        Assert.Equal(95, vm.SliderOpacityValue);
    }

    // ---- §18.9 mutual exclusion ----

    [Fact]
    public async Task DiscreteMutationInFlight_BlocksOpacityInteraction()
    {
        var (vm, fake) = Loaded(opacity: 70);
        fake.NextSnapshot = Snap(70, alignment: WireAlignment.Right);
        fake.Gate = new TaskCompletionSource();

        Task mutation = vm.SelectAlignmentAsync(WireAlignment.Right);

        vm.BeginOpacityInteraction();
        Assert.False(vm.IsOpacityInteractionActive);
        Assert.False(vm.IsOpacitySliderEnabled);

        fake.Gate.SetResult();
        await mutation;
    }

    [Fact]
    public async Task ActiveOpacityInteraction_BlocksDiscreteMutation()
    {
        var (vm, fake) = Loaded(opacity: 70);
        fake.NextSnapshot = Snap(70);
        fake.Gate = new TaskCompletionSource();

        vm.BeginOpacityInteraction();
        vm.UpdateOpacityGesture(80); // preview held in flight -> interaction busy

        await vm.SelectAlignmentAsync(WireAlignment.Right);
        Assert.DoesNotContain(ControlOperation.SetHudAlignment, fake.Calls);
        Assert.True(vm.IsOpacitySliderEnabled); // preview in flight does not disable the slider

        fake.Gate.SetResult();
        await vm.EndOpacityInteractionAsync();
    }

    // ---- §19.4 opacity terminal failure reaches the frontend-close path ----

    [Fact]
    public async Task CommitTerminalFailure_RaisesRuntimeLost()
    {
        var (vm, fake) = Loaded(opacity: 70);
        int raised = 0;
        vm.RuntimeLost += () => raised++;

        vm.BeginOpacityInteraction();
        vm.UpdateOpacityGesture(85);
        fake.NextOutcome = ControlClientOutcome.TransportUnavailable;
        await vm.EndOpacityInteractionAsync();

        Assert.Equal(1, raised);
        Assert.False(vm.IsOpacityInteractionActive);
    }

    [Fact]
    public async Task PreviewTerminalFailure_RaisesRuntimeLost()
    {
        var (vm, fake) = Loaded(opacity: 70);
        bool raised = false;
        vm.RuntimeLost += () => raised = true;

        fake.NextOutcome = ControlClientOutcome.ProtocolError;
        fake.NextStatus = ControlStatus.ShuttingDown;

        vm.BeginOpacityInteraction();
        vm.UpdateOpacityGesture(85); // preview fails terminally

        Assert.True(raised);

        fake.NextOutcome = ControlClientOutcome.ProtocolError; // commit also fails; already raised once
        await vm.EndOpacityInteractionAsync();
    }
}
