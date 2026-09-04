using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using ClawHUD.Settings.Services;

namespace ClawHUD.Settings;

/// <summary>
/// Settings frontend entry point. The window is created explicitly only after
/// this process wins session-scoped single-instance ownership; a later process
/// is a relay that signals the primary to come forward and exits without
/// building any window / ViewModel / IPC client.
/// </summary>
public partial class App : Application
{
    private SettingsInstanceCoordinator? _coordinator;
    private RuntimeProcessLifetimeWatcher? _runtimeWatcher;
    private bool _pendingActivation;
    private bool _runtimeExitPending;

    public App()
    {
        // Best-effort managed-crash diagnostics. Native clawhud.log can only see
        // WER / ghost-window side effects; this captures the actual exception.
        DispatcherUnhandledException += (_, args) =>
            SettingsCrashLogger.LogFatal("DispatcherUnhandledException", args.Exception);
        // Deliberately not args.Handled = true: an unknown fatal exception must not
        // be swallowed and the UI pretended healthy.
        AppDomain.CurrentDomain.UnhandledException += (_, args) =>
        {
            if (args.ExceptionObject is Exception exception)
                SettingsCrashLogger.LogFatal("AppDomain.UnhandledException", exception);
        };
    }

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        _coordinator = new SettingsInstanceCoordinator();
        if (!_coordinator.TryAcquirePrimary())
        {
            // Relay: let the primary take foreground, wake it, and exit. This is
            // reliable only because the tray left-click path takes foreground
            // before launching us (see TrayIcon::WindowProc).
            if (!NativeMethods.AllowSetForegroundWindow(NativeMethods.ASFW_ANY))
                Trace.WriteLine("ClawHUD.Settings relay: AllowSetForegroundWindow denied " +
                    $"(error {Marshal.GetLastWin32Error()})");
            _coordinator.SignalPrimary();
            _coordinator.Dispose();
            _coordinator = null;
            Shutdown();
            return;
        }

        _coordinator.ActivationRequested += OnActivationRequested;

        RuntimePidArgument runtimePid = RuntimePidArgument.Parse(e.Args);
        if (runtimePid.State == RuntimePidArgumentState.Valid)
        {
            if (!RuntimeProcessLifetimeWatcher.TryStart(runtimePid.Pid, OnRuntimeExit,
                    out _runtimeWatcher, out Exception? error))
            {
                Trace.WriteLine("ClawHUD.Settings: runtime lifetime watcher initialization failed " +
                    error?.GetType().Name + ": " + error?.Message);
                Shutdown();
                return;
            }
        }

        var window = new MainWindow();
        if (_runtimeExitPending)
        {
            Shutdown();
            return;
        }
        MainWindow = window;
        window.Show();

        if (_pendingActivation)
        {
            _pendingActivation = false;
            BringToFront(window);
        }
    }

    private void OnActivationRequested()
    {
        // The registered-wait callback runs on a thread-pool thread.
        Dispatcher.BeginInvoke(() =>
        {
            if (MainWindow is { } window)
                BringToFront(window);
            else
                _pendingActivation = true; // window still being constructed in OnStartup
        });
    }

    private void OnRuntimeExit()
    {
        _runtimeExitPending = true;
        Dispatcher.BeginInvoke(() =>
        {
            if (MainWindow is { } window)
                window.Close();
            else
                Shutdown();
        });
    }

    private static void BringToFront(Window window)
    {
        if (window.WindowState == WindowState.Minimized)
            window.WindowState = WindowState.Normal;
        window.Show();
        window.Activate();
        NativeMethods.SetForegroundWindow(new WindowInteropHelper(window).EnsureHandle());
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _runtimeWatcher?.Dispose();
        _runtimeWatcher = null;
        _coordinator?.Dispose();
        _coordinator = null;
        base.OnExit(e);
    }
}
