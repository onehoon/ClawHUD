# Main App Shell / Tray / Velopack

`ClawHUD.exe` is the production application shell. `ClawHUD.VrrPoc.exe` remains a separate executable and is not loaded by the production app.

## Startup lifecycle

The native entry point runs the official Velopack C/C++ startup hook first, then acquires a per-user named mutex. A second instance exits without opening or activating the first instance. The normal path performs a silent Velopack GitHub update check, download, and apply/restart when available; errors are logged through `OutputDebugString` and continue with the current executable. The Velopack DLL is delay-loaded so it is not imported before the entry point.

On a direct CMake/Visual Studio build, Velopack startup/update exceptions are caught and the app continues silently. A packaged Velopack layout uses the same entry point and official hook handling without a custom `Update.exe` path probe.

After update handling, startup creates only a message-only tray window and a tray icon. No Settings window, taskbar window, D3D device, DirectComposition object, PresentMon session, WMI/EC polling, IGCL object, or diagnostics service is created.

The tray menu contains:

- Settings
- Exit

Settings is created only when selected. It contains the placeholder General, HUD, and Diagnostics tabs. Closing it calls `DestroyWindow`; the owning `unique_ptr` is released after the destroy notification. Selecting Settings again creates a fresh window.

## Build

Configure and build from a Windows 11 x64 Visual Studio environment:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

CMake downloads the pinned Velopack C/C++ 1.2.0 library and verifies its SHA-256 before extracting it into the build directory. The update feed is the ClawHUD GitHub repository; until a Velopack release feed exists, the check fails or reports no update and startup continues without a notification or popup.

This PR intentionally does not add telemetry, game detection, settings persistence, startup-with-Windows, VRR measurement, Composition Swapchain production HUD rendering, or hardware diagnostics.
