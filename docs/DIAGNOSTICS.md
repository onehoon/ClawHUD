# EC Diagnostics

The current Diagnostics implementation is intentionally limited to an MSI Claw EC read probe. Opening Settings or selecting the Diagnostics tab does not initialize WMI. Press **Start EC Test** to start one bounded worker. `ClawHUD.exe` remains unelevated; the first EC test starts `ClawHUD.EcHelper.exe` with `runas` and uses a private per-process named pipe for the read-only transport. The helper is reused for the ten samples and exits when the pipe reaches EOF.

The test connects to `ROOT\WMI`, invokes only `Get_Temperature`, `Get_Fan`, and `Get_Data`, records ten samples at approximately one-second intervals, then releases WMI. Each sample keeps the raw payload and records decoded CPU/GPU temperature, fan RPM, and CPU package power where the documented payload is available. The battery current and voltage selectors are preserved as raw bytes; system-power decoding remains deferred until hardware validation closes the documented sign/scaling questions.

Logs are UTF-8 text files under `logs/diagnostics/ec-YYYYMMDD-HHMMSS.txt`. **Open Log Folder** opens that directory. The log includes main/helper elevation state, helper PID, failure stage, HRESULT, and records `Unavailable` for environment fields that cannot be obtained. A read failure for one selector does not stop the remaining selectors. Helper missing, UAC cancellation, pipe disconnect, and WMI failures leave the app running and report EC as unavailable; a new explicit diagnostic is required before another elevation attempt.

No `Set_*`, TDP, fan-control, charge-limit, or ownership operation is implemented. The test is not production telemetry and has not been validated on physical MSI Claw hardware in this development environment.

## VRR orchestration

The Diagnostics tab also contains a user-started VRR / Presentation orchestration test. It waits a bounded period for a non-ClawHUD foreground process, fixes that PID for the complete test, and uses the real main `ClawHUD.exe` presentation path for three phases:

1. **HUD OFF** — the main HUD is hidden and its update timer is stopped.
2. **STATIC HUD** — one deterministic HUD frame is rendered and shown; the 100 ms update timer is not started and no periodic redraw is issued.
3. **DYNAMIC HUD** — the same `HudPresentation` instance is kept visible and the existing 100 ms mock update path is resumed.

PresentMon samples 28 seconds in each phase. No `ClawHUD.VrrPoc.exe` child, test overlay, or second presentation path is created; the prior main-HUD visibility state is restored after completion, failure, or cancellation.

This PR records the same lifecycle evidence and adds raw PresentMon capture. The build pins the official `PresentMon-2.5.1-x64.exe` standalone asset and copies it to `tools/PresentMon.exe`; it does not download anything at runtime or request elevation. Each phase runs a 28-second PID-targeted capture with the default CSV schema, preserving the raw `-off.csv`, `-static.csv`, and `-dynamic.csv` files unchanged. The TXT report summarizes `PresentMode`, displayed versus non-displayed rows derived from default-schema `MsUntilDisplayed`, dominant swapchain, and `MsBetweenPresents` / `MsBetweenDisplayChange` statistics, plus OFF/STATIC/DYNAMIC comparisons.

The capture command does not use `--v1_metrics`, `--v2_metrics`, `--track_frame_type`, `--no_track_display`, `--exclude_dropped`, or PresentMon overlay options. The pinned v2.5.1 release executable does not expose `--write_display_metadata` in its actual console help, so display tracking is left enabled by default while the standard display-timing columns are retained. This compatibility detail is recorded in each diagnostic log.

The report always says `VRR Analysis: NEEDS MANUAL REVIEW`. `AllowsTearing`, Independent Flip, and PresentMon cadence are evidence for later human analysis, not an automatic PASS/FAIL engine. PresentMon may not observe all Intel UMD XeFG-generated frames; do not treat the capture as authoritative true XeFG displayed FPS or multiply render FPS by a XeFG multiplier. Capture can fail under an unelevated account when Windows ETW permissions are unavailable; that failure is retained as diagnostic evidence and does not change ClawHUD elevation policy.

### Intel IGCL supplementary evidence

During an explicitly started VRR test, ClawHUD also attempts to load the driver-installed `ControlLib.dll` dynamically. It does not bundle the IGCL runtime, add a package download, or initialize IGCL at startup, tray idle, Settings creation, or HUD startup.

PresentMon remains the application/presentation evidence path. IGCL is supplementary evidence for Intel Arc Sync capability/current profile and per-output/target VBlank timestamps during the same HUD-OFF, STATIC HUD, and DYNAMIC HUD capture windows. Duplicate timestamps are excluded and non-monotonic resets are recorded. IGCL result codes are retained with their symbolic name and raw hexadecimal value.

The IGCL path is read-only. No Arc Sync profile is changed. IGCL initialization, missing symbols, unsupported outputs, and VBlank read failures do not fail the existing PresentMon diagnostic; they are recorded as unavailable evidence. Neither PresentMon nor IGCL alone proves that VRR is active. XeFG-generated output frames may not all be observable through PresentMon, so the measured VBlank rate is not authoritative true displayed FPS.

No LFC heuristic or automatic VRR PASS/FAIL decision is implemented. The final result remains `VRR Analysis: NEEDS MANUAL REVIEW`, pending MSI Claw hardware validation with HUD OFF/STATIC/DYNAMIC and OptiScaler/XeFG conditions.

### MPO and hardware-composition capability evidence

Each explicitly started VRR test also records read-only DXGI capability information for the primary output and the current BGRA8 HUD format: `IDXGIOutput3::CheckOverlaySupport` and `IDXGIOutput6::CheckHardwareCompositionSupport`, including symbolic flags and raw values. D3DKMT MPO plane caps remain deferred when collecting them would require additional plumbing. These values mean only that a path may be supported by the adapter/output; they do not prove that the running game and ClawHUD are using an MPO plane or hardware composition at runtime. The report therefore remains `VRR Analysis: NEEDS MANUAL REVIEW`.
