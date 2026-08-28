# EC Diagnostics

## IGCL read-only capability survey

The Diagnostics tab also provides **Start IGCL Test**. It is explicit-action only: selecting Settings or Diagnostics does not load IGCL. Starting the test closes Settings, waits five seconds, captures the current foreground HWND/PID/path, then dynamically loads the driver-installed System32 `ControlLib.dll` with `CTL_INIT_FLAG_USE_LEVEL_ZERO`.

The bounded survey enumerates every adapter and records read-only device, PCI, 3D Live State, frequency, engine, memory, temperature, power, fan, display/output, and Power Telemetry V2 information when the installed runtime exports the relevant symbols. It performs no IGCL setter/reset/control call. Dynamic Power Telemetry V2 is sampled at 250 ms for 20 samples; raw support flags, type/unit enums, values, counters, timestamps, symbolic/raw result codes, and missing/unsupported/error classifications are retained in `igcl-YYYYMMDD-HHMMSS.txt` under `%LOCALAPPDATA%\\ClawHUD\\logs`.

The survey is diagnostic-only and does not feed HUD telemetry or alter existing EC, Windows, PresentMon, VRR, or Intel VRR Fix behavior. Successful cleanup plays the existing Diagnostics completion sound; cancellation, initialization failure, and incomplete logging do not.

The current Diagnostics implementation is intentionally limited to an MSI Claw EC read probe. Opening Settings or selecting the Diagnostics tab does not initialize WMI. Press **Start EC Test** to start one bounded worker. `ClawHUD.exe` remains unelevated; the first EC test starts `ClawHUD.EcHelper.exe` with `runas` and uses a private per-process named pipe for the read-only transport. The helper is reused for the ten samples and exits when the pipe reaches EOF.

The test connects to `ROOT\WMI`, invokes only `Get_Temperature`, `Get_Fan`, and `Get_Data`, records ten samples at approximately one-second intervals, then releases WMI. Each sample keeps the raw payload and records decoded CPU/GPU temperature, fan RPM, and CPU package power where the documented payload is available. The battery current and voltage selectors are preserved as raw bytes; system-power decoding remains deferred until hardware validation closes the documented sign/scaling questions.

Logs are UTF-8 text files under `%LOCALAPPDATA%\\ClawHUD\\logs\\ec-YYYYMMDD-HHMMSS.txt`. **Open Log Folder** opens the shared ClawHUD log directory. Runtime, EC, VRR, and Intel VRR Range Fix logs all use `%LOCALAPPDATA%\\ClawHUD\\logs`; no `logs\\diagnostics` subdirectory is created. The log includes main/helper elevation state, helper PID, failure stage, HRESULT, and records `Unavailable` for environment fields that cannot be obtained. A read failure for one selector does not stop the remaining selectors. Helper missing, UAC cancellation, pipe disconnect, and WMI failures leave the app running and report EC as unavailable; a new explicit diagnostic is required before another elevation attempt.

No `Set_*`, TDP, fan-control, charge-limit, or ownership operation is implemented. The test is not production telemetry and has not been validated on physical MSI Claw hardware in this development environment.

## VRR orchestration

The Diagnostics tab also contains a user-started VRR / Presentation orchestration test. After **Start VRR Test**, the Settings window closes and the diagnostic waits indefinitely for the existing global **F8** hotkey. At the instant F8 is received, the UI thread validates and captures the current foreground process and fixes that PID for the complete test. The validation rejects ClawHUD, Explorer, browsers, Steam/BPM, and known Windows shell/FSE helper processes; it does not attempt to identify every possible game. If validation fails, the diagnostic remains armed and waits for another F8 press. The test uses the real main `ClawHUD.exe` presentation path for three phases:

1. **HUD OFF** — the main HUD is hidden and its update timer is stopped.
2. **STATIC HUD** — one deterministic HUD frame is rendered and shown; the 100 ms update timer is not started and no periodic redraw is issued.
3. **DYNAMIC HUD** — the same `HudPresentation` instance is kept visible and the existing 100 ms mock update path is resumed.

PresentMon samples 28 seconds in each phase and writes the VRR TXT report and phase CSVs to the shared `%LOCALAPPDATA%\\ClawHUD\\logs` directory. No `ClawHUD.VrrPoc.exe` child, test overlay, or second presentation path is created; the prior main-HUD visibility state is restored after completion, failure, or cancellation.

Return to the running game and press F8 after starting the test. A short Windows system sound confirms that the game target was validated and the diagnostic started. The phases then run automatically as **HUD OFF → STATIC HUD → DYNAMIC HUD**, approximately 28 seconds each. A second Windows system sound is played only after all phases complete successfully and cleanup/restoration has run. Invalid F8 targets, capture failures, cancellation, and application shutdown do not play either success sound. Sounds are asynchronous Windows system aliases; no audio files are bundled.

This PR records the same lifecycle evidence and adds raw PresentMon capture. The build pins the official `PresentMon-2.5.1-x64.exe` standalone asset and copies it to `tools/PresentMon.exe`; it does not download anything at runtime or request elevation. Each phase runs a 28-second PID-targeted capture with the default CSV schema, preserving the raw `-off.csv`, `-static.csv`, and `-dynamic.csv` files unchanged. The TXT report summarizes `PresentMode`, displayed versus non-displayed rows derived from default-schema `MsUntilDisplayed`, dominant swapchain, and `MsBetweenPresents` / `MsBetweenDisplayChange` statistics, plus OFF/STATIC/DYNAMIC comparisons.

The capture command does not use `--v1_metrics`, `--v2_metrics`, `--track_frame_type`, `--no_track_display`, `--exclude_dropped`, or PresentMon overlay options. The pinned v2.5.1 release executable does not expose `--write_display_metadata` in its actual console help, so display tracking is left enabled by default while the standard display-timing columns are retained. This compatibility detail is recorded in each diagnostic log.

The report always says `VRR Analysis: NEEDS MANUAL REVIEW`. `AllowsTearing`, Independent Flip, and PresentMon cadence are evidence for later human analysis, not an automatic PASS/FAIL engine. PresentMon may not observe all Intel UMD XeFG-generated frames; do not treat the capture as authoritative true XeFG displayed FPS or multiply render FPS by a XeFG multiplier. Capture can fail under an unelevated account when Windows ETW permissions are unavailable; that failure is retained as diagnostic evidence and does not change ClawHUD elevation policy.

### Intel IGCL supplementary evidence

During an explicitly started VRR test, ClawHUD also attempts to load the driver-installed `ControlLib.dll` dynamically for read-only evidence. This diagnostic path does not bundle the IGCL runtime or add a package download. The separate Intel VRR Range Fix may load the same driver-installed DLL during its independent bounded startup tweak; that path is mutation-capable only under its own conservative policy and is not part of Diagnostics.

PresentMon remains the application/presentation evidence path. IGCL is supplementary evidence for Intel Arc Sync capability/current profile and per-output/target VBlank timestamps during the same HUD-OFF, STATIC HUD, and DYNAMIC HUD capture windows. Duplicate timestamps are excluded and non-monotonic resets are recorded. IGCL result codes are retained with their symbolic name and raw hexadecimal value.

The IGCL path is read-only. No Arc Sync profile is changed. VBlank sampling is limited to outputs whose monitor capability query succeeds, and per-output call failures are retained in the diagnostic log. The diagnostic also records the same WMI monitor identity fields used by VRR Fix (`ManufacturerName`, `ProductCodeID`, `UserFriendlyName`, `Active`, and `InstanceName`). WMI identity is panel evidence; it is not assumed to be an IGCL output handle. IGCL initialization, missing symbols, unsupported outputs, and VBlank read failures do not fail the existing PresentMon diagnostic; they are recorded as unavailable evidence. Neither PresentMon nor IGCL alone proves that VRR is active. XeFG-generated output frames may not all be observable through PresentMon, so the measured VBlank rate is not authoritative true displayed FPS.

No LFC heuristic or automatic VRR PASS/FAIL decision is implemented. The final result remains `VRR Analysis: NEEDS MANUAL REVIEW`, pending MSI Claw hardware validation with HUD OFF/STATIC/DYNAMIC and OptiScaler/XeFG conditions.

### MPO and hardware-composition capability evidence

Each explicitly started VRR test also records read-only DXGI capability information for the primary output and the current BGRA8 HUD format: `IDXGIOutput3::CheckOverlaySupport` and `IDXGIOutput6::CheckHardwareCompositionSupport`, including symbolic flags and raw values. D3DKMT MPO plane caps remain deferred when collecting them would require additional plumbing. These values mean only that a path may be supported by the adapter/output; they do not prove that the running game and ClawHUD are using an MPO plane or hardware composition at runtime. The report therefore remains `VRR Analysis: NEEDS MANUAL REVIEW`.
