using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using ClawHUD.Settings;
using ClawHUD.Settings.Protocol;
using Xunit;

namespace ClawHUD.Settings.Tests;

/// <summary>
/// Regression coverage for the 2026-09-04 device crash: the opacity Slider's
/// <c>Minimum</c> moving 0 → 50 during <c>InitializeComponent</c> coerced the
/// value and fired a <c>ValueChanged</c> handler that dereferenced the not-yet-
/// assigned ViewModel. The handler is now wired only after construction, so real
/// <c>MainWindow</c> construction must neither throw nor issue an opacity
/// mutation. Also pins the compact fixed geometry. This is the only test that
/// builds the process-wide WPF Application.
/// </summary>
public class MainWindowStartupTests
{
    [Fact]
    public void MainWindow_Construction_DoesNotThrowOrIssueAnOpacityMutation()
    {
        Exception? failure = null;
        var fake = new FakeRuntimeControlClient();
        double width = 0, height = 0;
        ResizeMode resizeMode = ResizeMode.CanResize;

        var thread = new Thread(() =>
        {
            try
            {
                if (Application.Current is null)
                {
                    var app = new App();
                    app.InitializeComponent(); // loads the merged Styles resource dictionary
                }

                var window = new MainWindow(fake); // internal ctor; the default ctor delegates here
                width = window.Width;
                height = window.Height;
                resizeMode = window.ResizeMode;
                GC.KeepAlive(window);
            }
            catch (Exception ex)
            {
                failure = ex;
            }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.IsBackground = true;
        thread.Start();

        Assert.True(thread.Join(TimeSpan.FromSeconds(30)), "MainWindow construction hung.");
        Assert.Null(failure);

        // XAML construction / value coercion must never reach the opacity path.
        Assert.Empty(fake.PreviewValues);
        Assert.Empty(fake.CommitValues);
        Assert.DoesNotContain(ControlOperation.PreviewHudOpacity, fake.Calls);
        Assert.DoesNotContain(ControlOperation.CommitHudOpacity, fake.Calls);

        // Compact fixed window (600 x 600 DIP), non-resizable.
        Assert.Equal(600, width);
        Assert.Equal(600, height);
        Assert.Equal(ResizeMode.NoResize, resizeMode);
    }

    [Fact]
    public void MainWindow_Load_LeavesDeviceInfoOptionalAndOnlyShowsTrustedCapability()
    {
        Exception? failure = null;
        bool closedForOldRuntime = false;
        bool oldRuntimeWindowStayedOpen = false;
        Visibility beforeLoad = Visibility.Visible;
        Visibility afterOldRuntime = Visibility.Visible;
        bool unrelatedSettingsLoaded = false;
        bool supportedCapabilityApplied = false;
        bool malformedCapabilityStayedHidden = false;
        bool malformedSnapshotStillLoaded = false;
        int supportedDeviceInfoRequestCount = 0;
        bool terminalDeviceInfoClosed = false;
        string bindingState = string.Empty;
        Visibility afterSupportedIntel = Visibility.Collapsed;

        var thread = new Thread(() =>
        {
            try
            {
                if (Application.Current is null)
                {
                    var app = new App();
                    app.InitializeComponent();
                }

                var oldRuntime = new FakeRuntimeControlClient
                {
                    NextSnapshot = new SettingsSnapshot(true, true, 0, WireFont.Unispace,
                        WireVisibilityMode.Always, WireAlignment.Center, WireBackgroundMode.ContentWidth,
                        70, true, null),
                    NextDeviceInfoOutcome = ClawHUD.Settings.Services.ControlClientOutcome.ProtocolError,
                    NextDeviceInfoStatus = ControlStatus.UnknownOperation,
                };
                var oldWindow = new MainWindow(oldRuntime);
                oldWindow.Closed += (_, _) => closedForOldRuntime = true;
                var oldCard = (Border)oldWindow.FindName("IntelVrrFixCard")!;
                beforeLoad = oldCard.Visibility;
                oldWindow.ShowInTaskbar = false;
                oldWindow.ShowActivated = false;
                oldWindow.Left = -10000;
                oldWindow.Top = -10000;
                oldWindow.Show();
                oldWindow.UpdateLayout();
                afterOldRuntime = oldCard.Visibility;
                oldRuntimeWindowStayedOpen = oldWindow.IsVisible && !closedForOldRuntime;
                var oldViewModel = (ClawHUD.Settings.ViewModels.MainViewModel)oldWindow.DataContext;
                unrelatedSettingsLoaded = oldViewModel.HudEnabled && oldViewModel.AreDiscreteSettingsControlsEnabled;

                var supportedRuntime = new FakeRuntimeControlClient
                {
                    NextSnapshot = new SettingsSnapshot(true, true, 0, WireFont.Unispace,
                        WireVisibilityMode.Always, WireAlignment.Center, WireBackgroundMode.ContentWidth,
                        70, true, null),
                    NextDeviceInfoOutcome = ClawHUD.Settings.Services.ControlClientOutcome.Success,
                    NextDeviceInfo = new DeviceInfo("msi.claw.a2vm.7", "msi.claw", "MSI Claw A2VM 7",
                        WireGpuVendor.Intel, true),
                };
                var supportedWindow = new MainWindow(supportedRuntime);
                supportedWindow.ShowInTaskbar = false;
                supportedWindow.ShowActivated = false;
                supportedWindow.Left = -10000;
                supportedWindow.Top = -10000;
                supportedWindow.Show();
                supportedWindow.UpdateLayout();
                supportedCapabilityApplied =
                    ((ClawHUD.Settings.ViewModels.MainViewModel)supportedWindow.DataContext).IsIntelVrrFixSupported;
                supportedDeviceInfoRequestCount = supportedRuntime.Calls.Count(
                    operation => operation == ControlOperation.GetDeviceInfo);
                var supportedCard = (Border)supportedWindow.FindName("IntelVrrFixCard")!;
                afterSupportedIntel = supportedCard.Visibility;
                var visibilityBinding = BindingOperations.GetBindingExpression(
                    supportedCard, UIElement.VisibilityProperty);
                bindingState = $"{visibilityBinding?.Status}; source={visibilityBinding?.DataItem}; vm={supportedWindow.DataContext}; value={afterSupportedIntel}";

                var malformedRuntime = new FakeRuntimeControlClient
                {
                    NextSnapshot = new SettingsSnapshot(true, true, 0, WireFont.Unispace,
                        WireVisibilityMode.Always, WireAlignment.Center, WireBackgroundMode.ContentWidth,
                        70, true, null),
                    NextDeviceInfoOutcome = ClawHUD.Settings.Services.ControlClientOutcome.MalformedResponse,
                };
                var malformedWindow = new MainWindow(malformedRuntime);
                malformedWindow.ShowInTaskbar = false;
                malformedWindow.ShowActivated = false;
                malformedWindow.Left = -10000;
                malformedWindow.Top = -10000;
                malformedWindow.Show();
                malformedWindow.UpdateLayout();
                var malformedViewModel = (ClawHUD.Settings.ViewModels.MainViewModel)malformedWindow.DataContext;
                malformedCapabilityStayedHidden = !malformedViewModel.IsIntelVrrFixSupported &&
                    ((Border)malformedWindow.FindName("IntelVrrFixCard")!).Visibility == Visibility.Collapsed;
                malformedSnapshotStillLoaded = malformedViewModel.HudEnabled &&
                    malformedViewModel.AreDiscreteSettingsControlsEnabled;

                var terminalRuntime = new FakeRuntimeControlClient
                {
                    NextSnapshot = new SettingsSnapshot(true, true, 0, WireFont.Unispace,
                        WireVisibilityMode.Always, WireAlignment.Center, WireBackgroundMode.ContentWidth,
                        70, false, null),
                    NextDeviceInfoOutcome = ClawHUD.Settings.Services.ControlClientOutcome.TransportUnavailable,
                };
                var terminalWindow = new MainWindow(terminalRuntime);
                terminalWindow.Closed += (_, _) => terminalDeviceInfoClosed = true;
                terminalWindow.ShowInTaskbar = false;
                terminalWindow.ShowActivated = false;
                terminalWindow.Left = -10000;
                terminalWindow.Top = -10000;
                terminalWindow.Show();
                terminalWindow.UpdateLayout();
                oldWindow.Close();
                supportedWindow.Close();
                malformedWindow.Close();
                GC.KeepAlive(oldWindow);
                GC.KeepAlive(supportedWindow);
            }
            catch (Exception ex)
            {
                failure = ex;
            }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.IsBackground = true;
        thread.Start();

        Assert.True(thread.Join(TimeSpan.FromSeconds(30)), "MainWindow startup test hung.");
        Assert.Null(failure);
        Assert.Equal(Visibility.Collapsed, beforeLoad);
        Assert.Equal(Visibility.Collapsed, afterOldRuntime);
        Assert.True(oldRuntimeWindowStayedOpen,
            "UnknownOperation from an older runtime must not close Settings");
        Assert.True(unrelatedSettingsLoaded,
            "UnknownOperation from an older runtime must not prevent other settings from loading");
        Assert.Equal(1, supportedDeviceInfoRequestCount);
        Assert.True(supportedCapabilityApplied, "successful DeviceInfo enables the static capability projection");
        Assert.True(malformedCapabilityStayedHidden,
            "a malformed optional DeviceInfo response must not reveal VRR or create support");
        Assert.True(malformedSnapshotStillLoaded,
            "a recoverable DeviceInfo failure must leave the compatible settings snapshot usable");
        Assert.True(terminalDeviceInfoClosed,
            "terminal pipe loss during DeviceInfo load must retain existing runtime-loss close behavior");
        Assert.True(afterSupportedIntel == Visibility.Visible, bindingState);
    }
}
