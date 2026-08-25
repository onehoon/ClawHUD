# HUD display lifecycle diagnostic

`HudPresentation` treats `WM_DISPLAYCHANGE` and `WM_DPICHANGED` as event-driven stale-geometry notifications. The window callback only sets a pending flag; the next `Render()` or `Show()` reconstructs the presentation resources from the current primary-monitor geometry.

The rebuild preserves the configured physical bar height, re-queries the recreated HUD window DPI, recreates all three presentation buffers and D2D targets, and restores visibility only when the HUD was visible before the change. Hidden HUDs remain hidden until policy allows `Show()`.

Manual Windows validation:

1. Show the mock HUD and change the primary display resolution in both directions; verify the top bar uses the new width and remains 30 physical pixels high.
2. Change Windows scaling through 100%, 150%, and 175% where supported; verify the physical text/bar sizing remains stable.
3. Test Center + Full Width, Right + Content Width, and Left + Full Width with non-default opacity across a display change.
4. Repeat while InGameOnly is hidden; return to the tracked process and verify the refreshed geometry is used before the HUD appears.

No display polling, telemetry, multi-monitor policy, compact mode, or VRR/XeFG claim is added.
