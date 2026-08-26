# HUD layout settings diagnostic

The HUD tab edits the existing `App::HudLayoutOptions` authority. `Enable HUD` is a runtime master switch that leaves the ClawHUD process and tray running while stopping HUD presentation and production sampling. `Always` and `In Game Only` are independent visibility modes; the selected mode is persisted to `%LOCALAPPDATA%\ClawHUD\settings.ini`.

1. Open Settings → HUD and verify Enable HUD, In Game Only, Center, Full Width, and 50% defaults on a clean profile.
2. With Mock HUD enabled, switch between Always and In Game Only and verify the selected visibility policy applies immediately.
3. Disable Enable HUD and confirm only the HUD and production sampling stop; ClawHUD and Settings remain available. Re-enable it and confirm the selected visibility mode is restored.
4. With Mock HUD visible, change alignment, background width, and opacity; each change should apply immediately without recreating HUD resources.
5. Set Always, Right, Content Width, and 35%, exit ClawHUD, restart it, and confirm the HUD tab and first mock render use those values.
6. Change settings while an InGameOnly HUD is hidden; it must remain hidden and use the new values when the tracked process returns foreground.
7. Open Settings and change values without enabling HUD; no HUD graphics resources should be created until Enable HUD is selected.

Invalid enum strings fall back to defaults and opacity is clamped to 0–100%.
