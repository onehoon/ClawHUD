using System.Windows;
using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;
using ClawHUD.Settings.ViewModels;

namespace ClawHUD.Settings;

/// <summary>
/// Read-only PR2 Settings page. On open it performs one bounded async load from
/// the ClawHUD Control IPC (runtime info, then settings snapshot) and projects
/// the result. No mutation is ever sent; controls stay <c>IsHitTestVisible=false</c>.
/// </summary>
public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Loaded += OnLoadedAsync;
    }

    private async void OnLoadedAsync(object sender, RoutedEventArgs e)
    {
        var client = new RuntimeControlClient();

        var infoResult = await client.GetRuntimeInfoAsync();
        if (!infoResult.IsSuccess)
            return;

        RuntimeInfo info = infoResult.Value!;
        if (info.MinimumProtocolVersion > ControlProtocol.ProtocolVersion ||
            info.MaximumProtocolVersion < ControlProtocol.ProtocolVersion)
            return; // client/runtime protocol incompatibility — do not project a snapshot

        Title = $"ClawHUD {info.ApplicationVersion}";

        var snapshotResult = await client.GetSettingsSnapshotAsync();
        if (!snapshotResult.IsSuccess)
            return;

        DataContext = MainViewModel.FromSnapshot(snapshotResult.Value!);
    }
}
