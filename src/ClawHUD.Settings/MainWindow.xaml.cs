using System.Windows;
using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;
using ClawHUD.Settings.ViewModels;

namespace ClawHUD.Settings;

/// <summary>
/// PR3 Settings page. On open it loads runtime info + the settings snapshot
/// (PR2 flow); cards 1-3 (opacity excluded) then forward user actions to the
/// ViewModel, which sends one Control-IPC mutation at a time and re-projects
/// whatever authoritative snapshot the runtime returns. Cards 4-5 and the
/// opacity slider stay read-only.
/// </summary>
public partial class MainWindow : Window
{
    private readonly RuntimeControlClient _client = new();
    private readonly MainViewModel _viewModel;

    public MainWindow()
    {
        InitializeComponent();
        _viewModel = new MainViewModel(_client);
        DataContext = _viewModel;
        Loaded += OnLoadedAsync;
    }

    private async void OnLoadedAsync(object sender, RoutedEventArgs e)
    {
        var infoResult = await _client.GetRuntimeInfoAsync();
        if (!infoResult.IsSuccess)
            return;

        RuntimeInfo info = infoResult.Value!;
        if (info.MinimumProtocolVersion > ControlProtocol.ProtocolVersion ||
            info.MaximumProtocolVersion < ControlProtocol.ProtocolVersion)
            return; // client/runtime protocol incompatibility — stay read-only

        Title = $"ClawHUD {info.ApplicationVersion}";

        var snapshotResult = await _client.GetSettingsSnapshotAsync();
        if (!snapshotResult.IsSuccess)
            return;

        _viewModel.ApplySnapshot(snapshotResult.Value!);
    }

    private async void OnEnableHudClick(object sender, RoutedEventArgs e) =>
        await _viewModel.ToggleHudEnabledAsync();

    private async void OnVisibilityModeClick(object sender, RoutedEventArgs e) =>
        await _viewModel.SelectVisibilityModeAsync(TagValue<WireVisibilityMode>(sender));

    private async void OnFontClick(object sender, RoutedEventArgs e) =>
        await _viewModel.SelectFontAsync(TagValue<WireFont>(sender));

    private async void OnAlignmentClick(object sender, RoutedEventArgs e) =>
        await _viewModel.SelectAlignmentAsync(TagValue<WireAlignment>(sender));

    private async void OnBackgroundModeClick(object sender, RoutedEventArgs e) =>
        await _viewModel.SelectBackgroundModeAsync(TagValue<WireBackgroundMode>(sender));

    private async void OnHudSizeDecreaseClick(object sender, RoutedEventArgs e) =>
        await _viewModel.StepHudSizeAsync(-1);

    private async void OnHudSizeIncreaseClick(object sender, RoutedEventArgs e) =>
        await _viewModel.StepHudSizeAsync(+1);

    private static T TagValue<T>(object sender) where T : struct, Enum =>
        Enum.Parse<T>((string)((FrameworkElement)sender).Tag);
}
