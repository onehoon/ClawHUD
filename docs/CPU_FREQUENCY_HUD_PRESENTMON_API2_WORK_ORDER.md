# Work Order — Display CPU Frequency (MHz) in ClawHUD

Status: Ready for implementation
Target: `main`
Scope: One small production PR; no changes to HUD presentation contracts.
Date: 2026-10-01

## Objective

Add CPU frequency to the existing CPU HUD segment, between usage and temperature. Retain separate TDP segment and existing GPU frequency behavior.

Example (illustrative values):

- Before: `CPU 42% 67°C`
- After:  `CPU 42% 4200MHz 67°C`
- Existing GPU: `GPU 98% 1850MHz` (unchanged)
- Existing TDP: `TDP 18W` (unchanged)

Frequency is rounded to an integer MHz, not shown in GHz. No separate CPU frequency row, detailed per-core view, new user-facing setting, new polling thread, CTW integration, or fallback hardware provider in this iteration.

## Data meaning and provenance

Use existing PresentMon API2 `PM_METRIC_CPU_FREQUENCY` on the system device, subject to introspection-reported support.

PresentMon upstream `IntelPresentMon/ControlLib/wmi/WmiTelemetryProvider.cpp` computes the system-representative CPU frequency as:

```cpp
frequencyMhz = processorFrequencyCounter *
    (processorPerformancePercent / 100.0);
```

The counters are:

```text
\Processor Information(_Total)\Processor Frequency
\Processor Information(_Total)\% Processor Performance
```

Therefore describe it as a Windows/PresentMon system-representative CPU frequency, **not** the instantaneous clock of the fastest P-core, per-core arithmetic mean, effective clock, or a guaranteed exact Task Manager reading. Do not implement our own per-core averaging or change the upstream formula. `PM_STAT_AVG` averages samples in the dynamic query time window; it does not establish per-core weighting.

Before production readiness, verify this API2 metric is advertised as available by the actual PresentMon service on a target Claw. Do not introduce a fabricated frequency if unavailable; the remainder of the CPU segment must continue working.

Upstream source:
https://github.com/GameTechDev/PresentMon/blob/main/IntelPresentMon/ControlLib/wmi/WmiTelemetryProvider.cpp

## Current project integration points (main as inspected)

1. `src/ClawHUD/PresentMonApi2Api.h` already declares `PM_METRIC_CPU_FREQUENCY` — do **not** modify or renumber the API enum.
2. `src/ClawHUD/PresentMonSystemTelemetry.h/.cpp` has a capability-driven dynamic query plan for CPU usage, Intel GPU usage, GPU frequency and VRAM. It already has `DecodePresentMonFrequencyMHz` with introspected unit handling, and uses a single system query.
3. `src/ClawHUD/PresentMonTelemetryTypes.h` defines `PresentMonSystemSnapshot`.
4. `src/ClawHUD/ProductionTelemetryController.cpp` maps a PresentMon snapshot into `HudSystemTelemetryInput`.
5. `src/ClawHUD/HudTelemetryAggregator.h/.cpp` retains optional telemetry fields across a bounded number of missing polls.
6. `src/ClawHUD/HudModel.h/.cpp` owns `HudTelemetrySnapshot` and `CpuValue()`, currently usage then temperature.
7. `src/ClawHUD/HudRenderer.cpp` tokenizes CPU values into metric cells, classifies usage/temperature, and reserves total width with `CPU 100% 100°C`. Merely adding the value in `CpuValue()` is insufficient: the renderer would currently treat an `MHz` token as temperature when it is not the first CPU token.
8. Test entry points include `tests/PresentMonTelemetryProviderTests.cpp`, `tests/HudTelemetryAggregatorTests.cpp`, and `tests/HudModelTests.cpp`; inspect any existing HUD renderer/layout regression tests as well.

## Implementation steps

### A. API2 telemetry (reuse existing query)

- Add `SystemMetricSlot::CpuFrequency` and `std::optional<double> cpuClockMHz` in the API2 system snapshot.
- In `BuildPresentMonSystemQueryPlan`, add `PM_METRIC_CPU_FREQUENCY` through the existing **system-device** capability-driven `add()` path; do not hardcode an Intel CPU/vendor or device ID. Preserve the existing four metric requests and their behavior.
- In `DecodePresentMonSystemSnapshot`, use the existing `DecodePresentMonFrequencyMHz(blob, element, type, unit)` for CPU frequency, as is done for GPU frequency. Honor introspected units and statistics/type conversion. Use `std::nullopt` when the value is missing, non-finite, negative, or otherwise unusable.
- Include CPU frequency availability and first-sample status in existing concise diagnostics where helpful; no high-frequency per-poll spam. Leave telemetry polling period, dynamic query window/offset and session lifecycle unchanged.
- Never fail the entire system telemetry query solely because CPU frequency is unsupported; follow existing capability-gated metric registration.

### B. HUD snapshot aggregation

Propagate the optional value:

```text
PresentMonSystemSnapshot.cpuClockMHz
  -> ProductionTelemetryController's HudSystemTelemetryInput.cpuClockMHz
  -> HudTelemetryAggregator retained cpuClockMHz_
  -> HudTelemetrySnapshot.cpuClockMHz
```

- Follow exactly the existing system-metric missing-sample retention policy (three successive missing polls), and clear on `ResetSystem()`.
- A missing CPU clock must not clear CPU usage, CPU temperature or the entire CPU segment.
- Refresh comments documenting owned field counts if they become inaccurate.

### C. Model formatting

In `CpuValue()`, format optional fields in this specific order:

```text
CPU [usage%] [clockMHz] [temperature°C]
```

For example:

```text
CPU 42% 4200MHz 67°C
CPU 42% 67°C         // frequency unavailable
CPU 4200MHz 67°C     // usage unavailable
CPU 4200MHz          // clock is the only available CPU field
```

- Integer rounded MHz (use existing `Integer()` formatting conventions).
- Append a single space only between present tokens.
- Leave the standalone `TDP` run, GPU run and segment ordering unchanged.
- Avoid showing invalid/NaN/infinite, negative or bogus values as clock text; do not substitute max/base CPU speed.

### D. Renderer classification and stable sizing

- Add a frequency metric kind (or share existing frequency kind after renaming it generically) with exemplar `9999MHz`.
- Update `MetricKindForToken(HudSegmentKind::Cpu, ...)` to explicitly classify tokens ending with `MHz` **by suffix rather than by position**. The CPU token classifier must correctly recognize frequency when usage or temperature is absent. Retain the current usage `%` and temperature `°C` classification.
- Keep GPU frequency token recognition functional; do not accidentally alter GPU placement/measurement.
- Update `MeasureReservedHudWidth()` exemplar from `CPU 100% 100°C` to `CPU 100% 9999MHz 100°C`. Follow existing metric-cell reserved widths so 999→1000 or 3999→4200 MHz does not cause HUD jitter.
- Do not change other HUD display options or introduce new layout modes. If a larger active segment exposes a clipping/measurement issue, fix only the renderer/measurement layer.

### E. Focused tests

Update or add targeted regression checks:

1. `BuildPresentMonSystemQueryPlan` includes CPU frequency on the available SYSTEM device, with appropriate stat and unit, alongside the four existing metrics.
2. Capability absence or unavailability of `PM_METRIC_CPU_FREQUENCY` omits **only** that binding, leaving other system fields usable.
3. CPU MHz decoding with Hz/MHz/GHz introspected units (and invalid/non-finite inputs) exercises existing frequency conversion.
4. CPU clock passes through production snapshot mapping and aggregator, survives short missing periods and clears after threshold/reset without changing other telemetry.
5. HUD formatting cases with all CPU fields; missing clock; missing usage; clock only. The new order is usage → MHz → temperature.
6. Renderer token classification for `4200MHz` and the reserved-width exemplar must be covered by existing renderer tests or a small directly testable helper, if feasible without a substantial refactor.
7. Existing GPU clock, TDP, CPU temperature, absence handling, and HUD visibility tests remain passing.
8. Run the supported Windows build and relevant CTest suite; report results in the implementation PR. If hardware availability cannot be validated in CI, explicitly call it out for device smoke testing.

## Non-negotiable VRR/presentation safety

**DO NOT modify or bypass any of the following:** HUD `windowExStyle`, `WS_EX_TRANSPARENT`, `WS_EX_NOACTIVATE`, `WS_EX_TOPMOST`, existing `WS_EX_LAYERED` behavior, `WM_NCHITTEST -> HTTRANSPARENT`, `WM_MOUSEACTIVATE -> MA_NOACTIVATE`, `ProductionHudPresentationContract()`, independent-flip requirement, existing Presentation API / DirectComposition production path, or premultiplied-alpha presentation contract.

In particular, do not change presentation/composition contracts to accommodate width, opacity, alignment or rendering. Background opacity remains background-only; foreground text opacity must remain unchanged. Preserve existing presentation-contract and VRR regression tests.

## Out of scope

- Per-core P/E clocks, max-core clock and effective-clock calculations.
- Task Manager-specific reverse engineering or copying its UI.
- WMI, PDH or LibreHardwareMonitor access **from ClawHUD** as a second provider.
- Changes to FPS/PresentMon process target selection, GPU/VRAM, fan, battery, TDP, or EC sampling.
- A settings toggle for CPU clock in this first pass.
- Packaging/deployment changes unless a real version/compatibility issue is found.

## Acceptance criteria

- CPU display becomes `CPU 42% 4200MHz 67°C` when all fields are present.
- CPU display remains correct and otherwise unchanged when CPU frequency is missing/unsupported.
- `GPU 98% 1850MHz` and separate `TDP 18W` continue to work.
- Stable reserved width and proper token classification for MHz.
- No additional provider or thread, no PresentMon timing changes, no production HUD presentation-contract changes.
- Relevant automated tests pass; a Claw device smoke test confirms a plausible changing CPU MHz value and no gameplay HUD regression.

Implementation PR: target `main`, keep the diff small (preferably under 500 LOC). This work order is documentation only; no production implementation is included here.
