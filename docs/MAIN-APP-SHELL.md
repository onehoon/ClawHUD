# Main App Shell / Tray / Velopack

`ClawHUD.exe` is the production application shell. `ClawHUD.VrrPoc.exe` remains a separate executable and is not loaded by the production app.

## Startup lifecycle

The native entry point runs the official Velopack C/C++ startup hook first, then acquires a per-user named mutex. A second instance exits without opening or activating the first instance. The normal path performs a silent Velopack GitHub update check, download, and apply/restart when available; errors are logged through `OutputDebugString` and continue with the current executable. The Velopack DLL is delay-loaded so it is not imported before the entry point.

On a direct CMake/Visual Studio build, Velopack startup/update exceptions are caught and the app continues silently. A packaged Velopack layout uses the same entry point and official hook handling without a custom `Update.exe` path probe.

After update handling, startup creates only a tray window and a tray icon. No Settings window, taskbar window, D3D device, DirectComposition object, PresentMon session, WMI/EC polling, or diagnostics worker is created. After the core tray/runtime is ready, the independent Intel VRR Range Fix may perform one bounded WMI/IGCL startup sequence; its failure never blocks the tray or other runtime paths.

The tray menu contains:

- Settings
- Exit

Settings is created only when selected. It contains the General, HUD, Tweaks, and About tabs. There is no Diagnostics tab and no in-app developer diagnostic; verbose debug logging is a developer-only switch (`[Developer] DebugLog` in `settings.ini`, read once at startup — see [DIAGNOSTICS.md](DIAGNOSTICS.md)). The Tweaks tab only persists the Intel VRR Range Fix startup toggle and displays its last result; it never applies a profile from the UI. The Settings minimize and close buttons hide the window to the tray while keeping ClawHUD running; selecting Settings again restores the same window.

The General tab contains **Start ClawHUD with Windows**, enabled by default when the setting is absent. The setting is stored in `%LOCALAPPDATA%\\ClawHUD\\settings.ini`. When enabled, ClawHUD maintains its own `ClawHUD.lnk` in the current user's Startup folder; disabling it removes only that shortcut. The packaged Velopack release includes the Startup shortcut location as well.

## Build

Configure and build from a Windows 11 x64 Visual Studio environment:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

CMake downloads the pinned Velopack C/C++ 1.2.0 library and verifies its SHA-256 before extracting it into the build directory. The update feed is the ClawHUD GitHub repository; until a Velopack release feed exists, the check fails or reports no update and startup continues without a notification or popup.

Game detection, settings persistence, startup-with-Windows, and Composition Swapchain production HUD rendering remain out of scope of this document.
