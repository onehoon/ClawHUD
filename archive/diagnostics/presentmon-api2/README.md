Archived diagnostic reference only.
Not part of the production build.
Removed after PresentMon API2 was validated and the game-detection research
served its purpose.

## Purpose

This was the former in-app, developer-only diagnostic wired into `App`, the
Settings > Diagnostics tab, and the message loop. It validated the PresentMon
API2 runtime and was a scratch space for game-detection evidence ideas. It was
never production authority.

## Historical functionality

### PresentMonApi2Diagnostic

- API2 runtime / session validation and version query
- introspection root dump
- static + dynamic metric capability survey (per device / array index)
- foreground PID capture with a 5-second settle delay
- dynamic telemetry queries
- frame queries via `pmRegisterFrameQuery` / `pmConsumeFrames`
- frame CSV + text log output under `%LOCALAPPDATA%\ClawHUD\logs`
- metric classification (`Api2MetricResult` / `ClassifyApi2Metric`)

### GameDetectionProbe research

- foreground PID / HWND / executable tracking
- window title / visibility / owner inspection
- window and monitor geometry
- fullscreen-like classification
- PDH GPU Engine activity deltas per PID
- PID candidate ranking
- PresentMon auto-target parity research (`assets/PresentMonAutoTargetBlockList.txt`)
- API2 telemetry summaries for foreground processes

## Why archived

- PresentMon API2 itself is validated; the production telemetry layer
  (`PresentMonApi2Client`, `PresentMonTelemetryProvider`,
  `PresentMon{Process,System,Frame,DebugFrame}Telemetry`,
  `PresentMonRuntimeBootstrap`) is what the app actually uses now.
- The game-detection research here was never promoted to production authority;
  production game detection uses its own coordinator / trigger / verifier stack.
- Developer diagnostics should not be coupled to `App`, Settings, HUD lifecycle,
  production sampling, or suspend/resume.
- Future diagnostics will be rebuilt as a standalone `ClawHUD.Diag.exe` console
  tool, designed from a clean slate.

## Status

REFERENCE ONLY
NOT PART OF THE PRODUCTION BUILD

`assets/PresentMonAutoTargetBlockList.txt` was consumed only by
`GameDetectionProbe` and is archived with it. Git history is the authoritative
record for deeper reconstruction.
