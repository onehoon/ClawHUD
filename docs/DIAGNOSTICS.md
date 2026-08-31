# Diagnostics

There is no Diagnostics tab and no diagnostic UI. Settings has only the General,
HUD, Tweaks, and About tabs.

## Debug logging (developer-only)

Verbose `RuntimeLogger` output — including the API2-backed `[PresentActivity]`
per-frame lines and the process/window lifecycle sources — is gated by a single
manual switch in `%LOCALAPPDATA%\ClawHUD\settings.ini`:

```ini
[Developer]
DebugLog=true
```

`DebugLog` defaults to `false`, accepts `true`/`false` (case-insensitive) or
`1`/`0`, is read once at startup, and is never written by the app. Change it and
restart ClawHUD to take effect. Logs are written to `%LOCALAPPDATA%\ClawHUD\logs`,
the shared directory for runtime, EC, and Intel VRR Range Fix logs.

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
Production EC telemetry (`ClawHUD.EcHelper`, `MsiEcHudTelemetry`, EC HUD
sampling) and the Intel VRR Range Fix tweak are unrelated and unchanged.
