# HUD layout settings diagnostic

The HUD tab edits the existing `App::HudLayoutOptions` authority and persists only layout values to `%LOCALAPPDATA%\ClawHUD\settings.ini`.

1. Open Settings → HUD and verify Center, Full Width, and 50% defaults on a clean profile.
2. With Mock HUD visible, change alignment, background width, and opacity; each change should apply immediately without recreating HUD resources.
3. Set Right, Content Width, and 35%, exit ClawHUD, restart it, and confirm the HUD tab and first mock render use those values.
4. Change settings while an InGameOnly HUD is hidden; it must remain hidden and use the new values when the tracked process returns foreground.
5. Open Settings and change values without starting Mock HUD; no HUD graphics resources should be created.

Invalid enum strings fall back to defaults and opacity is clamped to 0–100%.
