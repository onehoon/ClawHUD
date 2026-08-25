# HUD foreground visibility diagnostic

The current visibility path uses a temporary PID target so the event-driven lifecycle can be tested before PresentMon supplies a real game PID.

1. Launch `ClawHUD.exe` and start the mock HUD from the tray menu.
2. Switch to the intended game or test application, open the tray menu, and choose **Track Foreground as Mock Game**. The target HWND is captured before the tray menu changes foreground ownership.
3. With **HUD: In Game Only** selected, verify the HUD is visible while the tracked process is foreground, disappears after Alt+Tab, and reappears when returning to the same process.
4. Select **HUD: Always** and verify the HUD remains visible while switching to a browser or the desktop.
5. Select **HUD: In Game Only** again while another application is foreground; the HUD should hide immediately and return when the tracked process becomes foreground.

The tracker uses `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` and PID comparison. It does not classify applications as games, poll foreground state, or start telemetry providers. The mock HUD is rendered once; visibility changes only attach or detach the existing composition visual.
