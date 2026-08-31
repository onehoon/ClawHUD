# Diagnostics

The Settings > Diagnostics tab contains:

- **PresentMon API2 Read-only Capability Test** (Start / Stop)
- **Enable debug logging**
- **Open Log Folder**

## PresentMon API2 Read-only Capability Test

Requires the installed PresentMon API2 SDK/runtime. **Start API2 Test** closes
Settings, waits five seconds, selects the current foreground PID, then records a
fixed-target metric survey for approximately 15 seconds
(`api2-YYYYMMDD-HHMMSS.txt` / `-frames.csv` under `%LOCALAPPDATA%\ClawHUD\logs`).
A game-detection research probe continues until **Stop**. The survey is
read-only: no PresentMon control calls, no HUD telemetry effect. Starting it
pauses production EC / PresentMon / graphics-API sampling for its duration, as
before.

## Debug logging / Open Log Folder

**Enable debug logging** toggles verbose `RuntimeLogger` output.
**Open Log Folder** opens `%LOCALAPPDATA%\ClawHUD\logs`, the shared directory for
runtime, EC, and Intel VRR Range Fix logs.

## Retired diagnostics

The **MSI EC Read Test**, **IGCL Read-only Capability Test**, and the legacy
**VRR / Presentation Test** (which launched `PresentMon.exe` and compared
HUD-OFF vs HUD-DYNAMIC CSV phases) were removed after their hardware/research
validation completed. Reference copies live under
`archive/diagnostics/` and are not part of the build. Production EC telemetry
(`ClawHUD.EcHelper`, `MsiEcHudTelemetry`, EC HUD sampling) and the Intel VRR
Range Fix tweak are unrelated and unchanged.
