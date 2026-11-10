# Diagnostics

There is no Diagnostics tab and no diagnostic UI. Settings has only the General,
HUD, Tweaks, and About tabs.

## Debug logging (developer-only)

Verbose `RuntimeLogger` output — including the API2-backed `[PresentActivity]`
per-frame lines and the process/window lifecycle sources — is gated by a single
manual switch in `%LOCALAPPDATA%\ClawHUD\settings.ini`:

```ini
[Developer]
DebugLoggingEnabled=true
```

`DebugLoggingEnabled` defaults to `false`, accepts `true`/`false`
(case-insensitive) or `1`/`0`, is read once at startup, and is never written by
the app. Change it and restart ClawHUD to take effect. Logs are written to `%LOCALAPPDATA%\ClawHUD\logs`,
the shared directory for runtime, EC, and Intel VRR Range Fix logs.

## MSI Claw A8 baseline telemetry evidence

For the exact admitted MSI Claw A8 BZ2EM model, concise `[A8Telemetry]` INFO/WARN
events are written to the normal `clawhud.log` even when
`DebugLoggingEnabled=false`. This model-gated evidence reports the existing
PresentMon query bindings and the first valid raw samples from PresentMon and
Windows. It does not enable verbose debug observation, access EC sensors, add a
data reader, or change telemetry collection. Missing samples are reported once
per metric after active sampling attempts; a later first valid sample is still
logged.

To collect a baseline, leave the HUD set to **Always** and visible for at least
30 seconds, then launch a game and allow FPS/GPU-load samples to arrive. Send
`%LOCALAPPDATA%\ClawHUD\logs\clawhud.log` and, if present,
`clawhud.1.log` / `clawhud.2.log` because the normal logger rotates at 2 MiB.
The `[A8Telemetry]` events and existing default PresentMon status/query-plan
lines are sufficient; no debug switch or additional diagnostic tool is needed.
These software logs do not constitute physical A8 hardware certification.

There is no in-app developer diagnostic. Production PresentMon usage
(`PresentMonApi2Client`, `PresentMonTelemetryProvider`, FPS / system / frame /
debug-frame telemetry, `PresentMonRuntimeBootstrap`) is the only PresentMon
code in `ClawHUD.exe`.

## Retired diagnostics

Removed after their hardware/research validation completed; reference copies
live under `archive/diagnostics/` and are not part of the build:

- **MSI EC Read Test**, **IGCL Read-only Capability Test**, legacy
  **VRR / Presentation Test** (`PresentMon.exe` CSV phase comparison) —
  `archive/diagnostics/{ec,igcl,legacy-vrr-presentmon}/`.
- **PresentMon API2 Read-only Capability Test** and the `GameDetectionProbe`
  game-detection research — `archive/diagnostics/presentmon-api2/`. API2 itself
  is validated; developer diagnostics should not be coupled to `App`, Settings,
  the HUD, or production sampling.

A future standalone `ClawHUD.Diag.exe` console tool will be designed separately.
Production EC telemetry uses `ClawHUD.EcHelper` and the normal HUD sampling
loop; the MSI Claw A8 exact-model read scope and ordinary log evidence are
documented below. The Intel VRR Range Fix remains separate.

## MSI Claw A8 EC sampling

On exact model `msi.claw.bz2em` / board `MS-1T8K`, ClawHUD attempts the existing
read-only `Get_Temperature(0)` CPU-temperature getter and the first tach pair
from `Get_Fan(0)`. The A8 reader ignores any fan bytes after the first pair;
the existing Intel dual-fan decoder and sensor permissions are unchanged. A8
CPU temperature byte meaning and fan RPM accuracy remain unverified on physical
hardware. A8 `Get_Data(221)` power and EC battery-discharge telemetry are not
enabled.

The `[A8Telemetry]` startup summary reports configured EC sensors. The first
valid temperature and fan values each produce one INFO line; a getter with no
valid sample produces one WARN after 30 seconds of actual attempts. These lines
are written to the ordinary `clawhud.log` at INFO/WARN even when
`DebugLoggingEnabled=false`. To collect evidence, select **Always**, keep the
HUD visible for at least 30 seconds, then send `clawhud.log` and rotated logs.
The first EC read may show the existing elevation prompt; failed reads do not
relaunch the helper or repeat the prompt.
