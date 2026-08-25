# HUD numeric stability diagnostic

The dynamic mock HUD now crosses normal digit boundaries while keeping renderer-side value slots stable.

1. Launch `ClawHUD.exe`, select **HUD: Always**, and choose **Show Mock HUD**.
2. With the default centered layout, observe the repeating FPS, CPU/GPU usage, TDP, FAN, and BAT values.
3. Confirm transitions such as `99 → 100 FPS`, `9.8 → 10.1 W`, and `999 → 1000 RPM` do not move separators or the centered line.
4. Repeat with **HUD: In Game Only** and verify Alt+Tab hide/show and redraw continue to work.

Values remain semantic strings in HudModel. HudRenderer measures representative DirectWrite strings as minimum value extents; unexpected larger values expand naturally instead of being clipped.
