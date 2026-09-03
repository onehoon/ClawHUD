using System.Windows;

namespace ClawHUD.Settings;

/// <summary>
/// PR1 shell entry point. No runtime/IPC wiring yet — the process exists only to
/// host the static Settings window for visual review and terminates with it.
/// </summary>
public partial class App : Application
{
}
