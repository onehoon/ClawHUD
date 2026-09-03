using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.ViewModels;
using Xunit;

namespace ClawHUD.Settings.Tests;

public class MainViewModelTests
{
    private static SettingsSnapshot Snapshot(
        int size = 0,
        WireFont font = WireFont.Unispace,
        WireVisibilityMode visibility = WireVisibilityMode.Always,
        WireAlignment alignment = WireAlignment.Center,
        WireBackgroundMode background = WireBackgroundMode.ContentWidth,
        ushort opacity = 70) =>
        new(StartWithWindows: true, HudEnabled: true, HudSizeOffset: size, HudFont: font,
            VisibilityMode: visibility, Alignment: alignment, BackgroundMode: background,
            BackgroundOpacityPercent: opacity, IntelVrrRangeFixEnabled: false, IntelVrrLastResult: null);

    [Theory]
    [InlineData(-2, "-2")]
    [InlineData(-1, "-1")]
    [InlineData(0, "Default")]
    [InlineData(1, "+1")]
    [InlineData(2, "+2")]
    public void HudSizeLabel_MapsOffset(int offset, string expected) =>
        Assert.Equal(expected, MainViewModel.FromSnapshot(Snapshot(size: offset)).HudSizeLabel);

    [Fact]
    public void Projection_MapsMutuallyExclusiveChoices()
    {
        MainViewModel vm = MainViewModel.FromSnapshot(Snapshot(
            font: WireFont.SegoeUiVariable, visibility: WireVisibilityMode.InGameOnly,
            alignment: WireAlignment.Right, background: WireBackgroundMode.FullWidth, opacity: 55));

        Assert.True(vm.IsFontSegoeUiVariable);
        Assert.False(vm.IsFontUnispace);
        Assert.True(vm.IsVisibilityInGameOnly);
        Assert.False(vm.IsVisibilityAlways);
        Assert.True(vm.IsAlignmentRight);
        Assert.True(vm.IsBackgroundFullWidth);
        Assert.Equal(55, vm.BackgroundOpacityPercent);
        Assert.Equal("55%", vm.BackgroundOpacityText);
        Assert.True(vm.StartWithWindows);
    }
}
