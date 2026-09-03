using System.Runtime.InteropServices;

namespace ClawHUD.Settings;

/// <summary>
/// The few Win32 calls needed to bring the existing Settings window forward when
/// a relay process asks for activation. A relay calls <see cref="AllowSetForegroundWindow"/>
/// with <see cref="ASFW_ANY"/> just before signalling so the primary's
/// <see cref="SetForegroundWindow"/> is not blocked by the foreground lock —
/// this avoids the always-on-top toggle hack.
/// </summary>
internal static class NativeMethods
{
    internal const int ASFW_ANY = -1;

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool AllowSetForegroundWindow(int dwProcessId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool SetForegroundWindow(IntPtr hWnd);
}
