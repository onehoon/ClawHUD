using System.IO;

namespace ClawHUD.Settings.Services;

/// <summary>
/// Best-effort fatal-exception log for the WPF Settings process. Native
/// <c>clawhud.log</c> can only observe WER / window-lifecycle side effects, not
/// the managed exception, so this writes <c>Exception.ToString()</c> (type,
/// message, inner exceptions, managed stack) to a separate frontend log.
///
/// Every path is no-throw: a logging failure must never replace the original
/// application failure.
/// </summary>
internal static class SettingsCrashLogger
{
    private const long MaxBytes = 1L * 1024 * 1024;

    private static readonly string LogPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "ClawHUD", "logs", "clawhud-settings.log");

    internal static void LogFatal(string source, Exception exception)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);

            try
            {
                if (File.Exists(LogPath) && new FileInfo(LogPath).Length > MaxBytes)
                    File.Delete(LogPath);
            }
            catch
            {
                // Rotation is optional; keep going.
            }

            string entry =
                $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} [FATAL] pid={Environment.ProcessId} " +
                $"source={source}{Environment.NewLine}{exception}{Environment.NewLine}{Environment.NewLine}";
            File.AppendAllText(LogPath, entry);
        }
        catch
        {
            // Never throw over the original failure.
        }
    }
}
