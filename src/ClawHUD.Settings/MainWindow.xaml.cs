using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;
using ClawHUD.Settings.ViewModels;

namespace ClawHUD.Settings;

/// <summary>
/// Settings page. On open it loads runtime info + the settings snapshot (PR2
/// flow). Discrete card 1-3 controls forward user actions to the ViewModel, which
/// sends one Control-IPC mutation at a time and re-projects the runtime's
/// authoritative snapshot. The opacity slider (PR4) previews live during a drag
/// and commits once on release. Cards 4-5 stay read-only.
/// </summary>
public partial class MainWindow : Window
{
    private readonly RuntimeControlClient _client = new();
    private readonly MainViewModel _viewModel;
    private bool _suppressOpacityValueChanged;

    public MainWindow()
    {
        InitializeComponent();
        _viewModel = new MainViewModel(_client);
        _viewModel.PropertyChanged += OnViewModelPropertyChanged;
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

    // ---- Background opacity slider --------------------------------------

    // Push the ViewModel's opacity value onto the slider without it counting as
    // user input (§15.1). WPF raises ValueChanged synchronously from the setter,
    // so the flag reliably brackets exactly the programmatic change.
    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        double target = _viewModel.SliderOpacityValue;
        if (OpacitySlider.Value == target)
            return;
        _suppressOpacityValueChanged = true;
        try { OpacitySlider.Value = target; }
        finally { _suppressOpacityValueChanged = false; }
    }

    private void OnOpacityDragStarted(object sender, DragStartedEventArgs e) =>
        _viewModel.BeginOpacityInteraction();

    private async void OnOpacityValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (_suppressOpacityValueChanged)
            return;

        ushort snapped = SnapOpacity(e.NewValue);
        if (_viewModel.IsOpacityInteractionActive)
            _viewModel.UpdateOpacityGesture(snapped);
        else
            await _viewModel.ChangeOpacityAsync(snapped); // keyboard / track click
    }

    private async void OnOpacityDragCompleted(object sender, DragCompletedEventArgs e) =>
        await _viewModel.EndOpacityInteractionAsync();

    private static ushort SnapOpacity(double raw)
    {
        int stepped = (int)System.Math.Round(raw / ControlProtocol.OpacityStepPercent) *
            ControlProtocol.OpacityStepPercent;
        return (ushort)System.Math.Clamp(stepped,
            ControlProtocol.MinOpacityPercent, ControlProtocol.MaxOpacityPercent);
    }

    private static T TagValue<T>(object sender) where T : struct, System.Enum =>
        System.Enum.Parse<T>((string)((FrameworkElement)sender).Tag);
}
