using System.Windows;
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

        // Compact fixed window (700 x 600 DIP), non-resizable.
        Assert.Equal(700, width);
        Assert.Equal(600, height);
        Assert.Equal(ResizeMode.NoResize, resizeMode);
    }
}
