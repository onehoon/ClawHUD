using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using ClawHUD.Settings.Protocol;
using ClawHUD.Settings.Services;
using ClawHUD.Settings.ViewModels;

namespace ClawHUD.Settings;

/// <summary>
/// Settings page. On open it loads runtime info + the settings snapshot; every
/// discrete control forwards user actions to the ViewModel, which sends one
/// Control-IPC mutation at a time and re-projects the runtime's authoritative
/// snapshot. The opacity slider previews live during a drag and commits once on
/// release. Re-activating the window refreshes the snapshot. When an IPC point
/// proves the runtime is gone, the window closes cleanly (never restarting
/// ClawHUD.exe).
/// </summary>
public partial class MainWindow : Window
{
    private readonly IRuntimeControlClient _client;
    private readonly MainViewModel _viewModel;
    private bool _suppressOpacityValueChanged;
    private bool _initialLoadComplete;
    private bool _closing;

    public MainWindow() : this(new RuntimeControlClient())
    {
    }

    // Test seam: a caller-supplied client. The startup ordering below is the
    // hotfix contract — the opacity ValueChanged handler is attached only after
    // InitializeComponent + ViewModel + DataContext, so XAML construction (which
    // coerces the Slider value when Minimum moves 0 -> 50) can never be seen as a
    // user opacity mutation.
    internal MainWindow(IRuntimeControlClient client)
    {
        _client = client;
        InitializeComponent();

        _viewModel = new MainViewModel(_client);
        _viewModel.PropertyChanged += OnViewModelPropertyChanged;
        _viewModel.RuntimeLost += OnRuntimeLost;
        DataContext = _viewModel;

        OpacitySlider.ValueChanged += OnOpacityValueChanged;

        Loaded += OnLoadedAsync;
        Activated += OnActivated;
    }

    private async void OnLoadedAsync(object sender, RoutedEventArgs e)
    {
        var infoResult = await _client.GetRuntimeInfoAsync();
        if (!infoResult.IsSuccess)
        {
            CloseFromRuntimeLoss();
            return;
        }

        RuntimeInfo info = infoResult.Value!;
        if (info.MinimumProtocolVersion > ControlProtocol.ProtocolVersion ||
            info.MaximumProtocolVersion < ControlProtocol.ProtocolVersion)
        {
            CloseFromRuntimeLoss(); // incompatible protocol — cannot safely control the runtime
            return;
        }

        Title = $"ClawHUD {info.ApplicationVersion}";

        var snapshotResult = await _client.GetSettingsSnapshotAsync();
        if (!snapshotResult.IsSuccess)
        {
            CloseFromRuntimeLoss();
            return;
        }

        _viewModel.ApplySnapshot(snapshotResult.Value!);
        _initialLoadComplete = true;
    }

    private async void OnActivated(object? sender, EventArgs e)
    {
        if (_initialLoadComplete && !_closing)
            await _viewModel.RefreshOnActivationAsync();
    }

    private void OnRuntimeLost() => Dispatcher.BeginInvoke(CloseFromRuntimeLoss);

    private void CloseFromRuntimeLoss()
    {
        if (_closing)
            return;
        _closing = true;
        Close();
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

    private async void OnIntelVrrRangeFixClick(object sender, RoutedEventArgs e) =>
        await _viewModel.ToggleIntelVrrRangeFixAsync();

    private async void OnStartWithWindowsClick(object sender, RoutedEventArgs e) =>
        await _viewModel.ToggleStartWithWindowsAsync();

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
