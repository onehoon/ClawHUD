# Mock Composition HUD manual check

This PR adds a temporary tray action for validating the production-oriented
Composition presentation path without real telemetry.

1. Build and launch `ClawHUD.exe` on Windows 11.
2. Open the tray menu and select **Show Mock HUD**.
3. Confirm the centered one-line DC sample appears at the top of the primary display:
   `DX11 60 FPS | CPU 36% 67°C | GPU 98% 72°C | TDP 18 W | SYS 24 W | FAN 3540 RPM | BAT 72% 2.5h`
4. Confirm the dark FullWidth background, readable category colors, and that mouse/keyboard focus remains with the prior application.
5. Select **Hide Mock HUD** and confirm the visual is removed, not merely made transparent.
6. Exit ClawHUD and confirm the temporary HUD window and resources are gone.

This is a visual presentation check only. It does not validate VRR, XeFG,
telemetry, game detection, or production visibility policy.
