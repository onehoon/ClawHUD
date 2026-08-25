# HUD dynamic presentation diagnostic

The mock HUD now redraws deterministic changing values through three fixed Composition presentation buffers.

1. Launch `ClawHUD.exe`, select **HUD: Always**, and choose **Show Mock HUD**.
2. Confirm FPS, CPU, and GPU values change approximately every 100 ms without freezing.
3. Select **Track Foreground as Mock Game** and **HUD: In Game Only**. Alt+Tab away and back repeatedly; the visual should detach and reattach while updates resume.
4. Use **Hide Mock HUD**, then **Show Mock HUD** again. Rendering should resume without stale or broken content.
5. Exit while updating and confirm the process and HUD window close cleanly.

This is deterministic mock data only. Rendering skips an update when no presentation buffer reports `IsAvailable`; it never waits indefinitely or rewrites an in-use buffer. No PresentMon, EC, battery, or game-discovery provider is connected.
