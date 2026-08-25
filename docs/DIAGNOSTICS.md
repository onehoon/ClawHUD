# EC Diagnostics

The current Diagnostics implementation is intentionally limited to an MSI Claw EC read probe. Opening Settings or selecting the Diagnostics tab does not initialize WMI. Press **Start EC Test** to start one bounded worker.

The test connects to `ROOT\WMI`, invokes only `Get_Temperature`, `Get_Fan`, and `Get_Data`, records ten samples at approximately one-second intervals, then releases WMI. Each sample keeps the raw payload and records decoded CPU/GPU temperature, fan RPM, and CPU package power where the documented payload is available. The battery current and voltage selectors are preserved as raw bytes; system-power decoding remains deferred until hardware validation closes the documented sign/scaling questions.

Logs are UTF-8 text files under `logs/diagnostics/ec-YYYYMMDD-HHMMSS.txt`. **Open Log Folder** opens that directory. The log includes elevation state and records `Unavailable` for environment fields that cannot be obtained. A read failure for one selector does not stop the remaining selectors; WMI connection failure ends the test and is recorded.

No `Set_*`, TDP, fan-control, charge-limit, or ownership operation is implemented. The test is not production telemetry and has not been validated on physical MSI Claw hardware in this development environment.

## VRR orchestration

The Diagnostics tab also contains a user-started VRR / Presentation orchestration test. It waits a bounded period for a non-ClawHUD foreground process, records a 30-second HUD-OFF phase, then launches the separate `ClawHUD.VrrPoc.exe --diagnostic` child for a 35-second HUD-ON grace period. PresentMon samples 28 seconds inside that verified HUD lifetime; the child is owned by the test and is terminated during Stop or Exit if necessary.

This PR records the same lifecycle evidence and adds raw PresentMon capture. The build pins the official `PresentMon-2.5.1-x64.exe` standalone asset and copies it to `tools/PresentMon.exe`; it does not download anything at runtime or request elevation. Each phase runs a 28-second PID-targeted capture with the default CSV schema, preserving the raw `-off.csv` and `-on.csv` files unchanged. The TXT report summarizes `PresentMode`, displayed versus non-displayed rows, dominant swapchain, and `MsBetweenPresents` / `MsBetweenDisplayChange` statistics.

The capture command does not use `--v1_metrics`, `--v2_metrics`, `--track_frame_type`, `--no_track_display`, `--exclude_dropped`, or PresentMon overlay options. The pinned v2.5.1 release executable does not expose `--write_display_metadata` in its actual console help, so display tracking is left enabled by default while the standard display-timing columns are retained. This compatibility detail is recorded in each diagnostic log.

The report always says `VRR Analysis: NEEDS MANUAL REVIEW`. `AllowsTearing`, Independent Flip, and PresentMon cadence are evidence for later human analysis, not an automatic PASS/FAIL engine. PresentMon may not observe all Intel UMD XeFG-generated frames; do not treat the capture as authoritative true XeFG displayed FPS or multiply render FPS by a XeFG multiplier. Capture can fail under an unelevated account when Windows ETW permissions are unavailable; that failure is retained as diagnostic evidence and does not change ClawHUD elevation policy.
