using System.Windows;

namespace ClawHUD.Settings;

/// <summary>
/// Static PR1 Settings page. No runtime state, persistence, or IPC — closing this
/// window ends the process (<c>ShutdownMode="OnMainWindowClose"</c>).
/// </summary>
public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
    }
}
