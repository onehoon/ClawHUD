# Work Order — Addon-Integration Branch PresentMon GPU Power HUD Metric

> **Date:** 2026-10-06  
> **Target repository:** `onehoon/ClawHUD`  
> **Target branch:** `integration/steamaddon` **ONLY**  
> **Do not modify:** `main` / standalone ClawHUD  
> **Scope:** Add one read-only Intel GPU power metric to the existing production HUD by reusing the existing PresentMon API2 system-telemetry path.  
> **Presentation guardrail:** `docs/HUD_PRESENTATION_REFACTOR_GUARDRAIL.md` remains fully authoritative.

## 1. Goal

Expose the actual GPU power value reported by PresentMon API2 on the Addon-integrated ClawHUD branch.

The desired GPU HUD segment is:

```text
GPU 98% 2300MHz 13W
```

Requirements:

- use the existing `PM_METRIC_GPU_POWER` metric;
- keep the raw telemetry value as a floating-point value internally;
- render the HUD value as a rounded whole number;
- reserve a stable two-digit GPU-power slot using `99W` as the width exemplar;
- keep `W` rendered through the existing small/unit typography path, matching `MHz`, `GB`, `RPM`, `%`, and the existing TDP `W`;
- if the metric is unsupported or remains unavailable, render `--W`, never a fake numeric sentinel such as `99W`;
- preserve existing HUD width stability and VRR-critical presentation behavior.

This PR is also the hardware-support probe for B390 / supported MSI Claw hardware. If PresentMon does not expose usable iGPU power, the HUD must make that visible without breaking any other metric.

## 2. Branch boundary

Implement this work on:

```text
integration/steamaddon
```

Do **not** port this work to:

```text
main
```

The user does not currently use the standalone branch.

Do not modify `SteamAddonforClaw` as part of this work order.

## 3. Architecture decision

Use the existing production telemetry path only:

```text
PresentMon API2
  -> PM_METRIC_GPU_POWER
  -> PresentMonSystemTelemetry
  -> PresentMonSystemSnapshot
  -> HudSystemTelemetryInput
  -> HudTelemetryAggregator
  -> HudTelemetrySnapshot
  -> HudModel
  -> HudRenderer
```

Do not add a second telemetry provider in this PR.

Specifically, do **not** add:

- PawnIO;
- LibreHardwareMonitor;
- direct Intel RAPL/MSR access;
- IGCL power telemetry;
- a helper/service/driver installer;
- a new telemetry manager or abstraction.

If PresentMon GPU power proves unavailable on the target hardware, record that result and leave the HUD showing `--W`. A PawnIO/RAPL PP1 fallback is a separate follow-up decision.

## 4. Current code facts to preserve

At the current `integration/steamaddon` branch:

- `PresentMonApi2Api.h` already declares `PM_METRIC_GPU_POWER`;
- PresentMon units already include `PM_UNIT_MILLIWATTS`, `PM_UNIT_WATTS`, and `PM_UNIT_KILOWATTS`;
- `PresentMonSystemTelemetry` already builds a capability-driven dynamic query for CPU/GPU system metrics;
- the Intel GPU is selected through the existing graphics-adapter/vendor capability path;
- system telemetry is sampled on the existing production cadence;
- `HudTelemetryAggregator` retains a valid system metric until `kSystemTelemetryMissingThreshold == 3` consecutive missing polls;
- `HudRenderer` already uses tabular figures;
- `HudRenderer` already reserves metric-specific exemplar widths;
- `FindHudUnitRangesImpl()` already recognizes `W` as a unit and renders it with the existing small unit font/offset;
- `HudMetricKind::TdpPower` already demonstrates the existing power-unit presentation behavior.

Do not replace or generalize these mechanisms.

## 5. PresentMon system telemetry changes

### 5.1 Add one system metric slot

Extend:

```cpp
enum class SystemMetricSlot
```

with:

```cpp
GpuPower
```

Keep the existing slots unchanged.

### 5.2 Add GPU power to the existing query plan

In `BuildPresentMonSystemQueryPlan()`, request:

```cpp
PM_METRIC_GPU_POWER
```

for the same existing Intel `PM_DEVICE_TYPE_GRAPHICS_ADAPTER` selection used for GPU usage/frequency.

The metric must remain capability-driven.

Required behavior:

```text
GPU_POWER advertised AVAILABLE
    -> add it to the system dynamic query

GPU_POWER unavailable / not exported / unsupported / not implemented
    -> do not add it
    -> keep all other existing system metrics working
    -> gpuPowerW remains unavailable
```

Do not make GPU power mandatory for successful system-query initialization.

### 5.3 Decode power using introspected units

Add a focused decoder such as:

```cpp
std::optional<double> DecodePresentMonPowerWatts(...)
```

Use the metric's introspected `PM_UNIT`.

Support at minimum:

```text
PM_UNIT_MILLIWATTS -> value / 1000.0
PM_UNIT_WATTS      -> value
PM_UNIT_KILOWATTS  -> value * 1000.0
```

Reject and return `std::nullopt` for:

- unsupported units;
- non-finite values;
- negative values;
- malformed/undersized query data.

Do not round in the telemetry layer.

### 5.4 Extend the system snapshot

Add:

```cpp
std::optional<double> gpuPowerW;
```

to `PresentMonSystemSnapshot`.

Populate it from the new `GpuPower` binding in `DecodePresentMonSystemSnapshot()`.

## 6. Logging / hardware-verification contract

The implementation must make B390 support status obvious without noisy per-poll logging.

Extend the existing query-plan log with:

```text
gpuPower=0|1
```

Example:

```text
[PresentMonSystem] query-plan elements=... cpu=1 cpuClock=1 gpuUsage=1 gpuClock=1 gpuPower=1 vram=1
```

Interpretation:

- `gpuPower=1`: PresentMon introspection says the Intel adapter exposes the metric and it was admitted into the query;
- `gpuPower=0`: PresentMon does not expose a usable GPU power metric for that adapter.

Extend the existing first-sample log with:

```text
gpuPowerW=<value|n/a>
```

The existing poll-status logging already reports meaningful query status/result-count changes. Reuse it.

Do not emit a new warning every one-second sample.

Do not add a new state machine merely for logging.

## 7. HUD telemetry propagation

Add `gpuPowerW` through:

- `HudSystemTelemetryInput`;
- `MakeHudSystemTelemetryInput()`;
- `HudTelemetryAggregator`;
- `HudTelemetrySnapshot`.

Use the existing system-telemetry retention rule:

```text
first valid value
    -> retain

1 or 2 consecutive missing polls
    -> retain last valid GPU power

3 consecutive missing polls
    -> clear gpuPowerW
    -> HUD renders --W
```

If GPU power has never produced a valid value, it is unavailable immediately and the HUD renders `--W`.

This keeps transient sampling gaps from causing visible value flicker while still making persistent failure obvious.

Do not create a GPU-power-specific retry loop, timer, epoch, or recovery state.

## 8. HUD formatting contract

### 8.1 GPU metric order

GPU metrics must render in this order:

```text
GPU <usage> <clock> <power>
```

Example:

```text
GPU 98% 2300MHz 13W
```

If power is unavailable:

```text
GPU 98% 2300MHz --W
```

The power cell must remain present on this Addon-integration branch so hardware failure is visually testable.

### 8.2 Whole-number rounding

Do not display decimals.

Use the existing whole-number formatting behavior based on `std::lround`.

Examples:

```text
1.4 W  -> 1W
1.5 W  -> 2W
12.4 W -> 12W
12.6 W -> 13W
```

Keep the unrounded value in telemetry/snapshot state; round only for HUD text.

### 8.3 Never use 99W as a failure sentinel

`99W` is reserved only as a layout exemplar.

Do not put `99.0` into `gpuPowerW` when reading fails.

Failure/unavailable representation is:

```text
--W
```

This prevents a fake value from contaminating later logs, aggregation, testing, or future calculations.

## 9. Stable width / renderer requirements

The HUD must not move horizontally as GPU power changes.

Add a dedicated metric kind:

```cpp
HudMetricKind::GpuPower
```

Use this width exemplar:

```cpp
case HudMetricKind::GpuPower:
    return L"99W";
```

Update GPU token classification so:

```text
...%   -> UsagePercent
...MHz -> FrequencyMHz
...W   -> GpuPower
```

Do not let the new `W` token fall through to `UsagePercent`.

The existing measurement rule:

```cpp
max(reserved exemplar width, actual metric width)
```

must remain unchanged.

Therefore these normal values occupy the same reserved metric slot:

```text
1W
9W
13W
99W
--W
```

An unexpected value wider than the exemplar may continue to expand naturally rather than being clipped. Do not add clipping solely because normal Claw GPU power is expected to stay within two digits.

## 10. Unit typography

Do not invent a new `W` drawing path.

The existing renderer already includes:

```cpp
addToken(L"W", true);
```

and renders recognized units with:

- the existing `unitFontPixelSize`;
- the existing `UnitAdvanceGap()`;
- the existing `UnitTextYOffset()`;
- the existing outline/color behavior.

GPU power `W` must therefore visually match the existing TDP `W` unit and other small units.

The main numeric glyphs remain tabular figures.

Do not change global font sizes, offsets, gaps, separator geometry, or other metric exemplars for this task.

## 11. HUD presentation hard stop

This feature is a renderer/telemetry extension only.

Do not modify or weaken any protected behavior in:

```text
docs/HUD_PRESENTATION_REFACTOR_GUARDRAIL.md
```

In particular, do not change:

- `HudPresentation` production presentation path;
- DirectComposition / Presentation API ownership;
- independent-flip requirement;
- premultiplied-alpha contract;
- window styles;
- click-through behavior;
- activation behavior;
- topmost behavior;
- hit testing;
- background-opacity semantics.

No presentation workaround is needed for this feature.

## 12. Tests

Update/add focused tests using synthetic PresentMon capability/query data only. CI must not require real GPU telemetry.

### 12.1 PresentMon telemetry tests

In `tests/PresentMonTelemetryProviderTests.cpp` or the existing focused system-telemetry test location, verify:

1. `PM_METRIC_GPU_POWER` is added only when AVAILABLE for the selected Intel graphics adapter.
2. GPU power being unavailable does not remove/break CPU usage, CPU frequency, GPU usage, GPU frequency, or VRAM bindings.
3. mW -> W conversion.
4. W passthrough.
5. kW -> W conversion.
6. unsupported unit -> `nullopt`.
7. negative / NaN / infinity -> `nullopt`.
8. decoded GPU power reaches `PresentMonSystemSnapshot::gpuPowerW`.

### 12.2 Aggregator tests

In `tests/HudTelemetryAggregatorTests.cpp`, verify:

- valid GPU power propagates into the HUD snapshot;
- one/two missing polls retain the prior value;
- the third consecutive missing poll clears it;
- reset clears the retained value and missing counter;
- existing telemetry fields are unaffected.

### 12.3 HUD model tests

In `tests/HudModelTests.cpp`, verify:

```text
12.4 -> 12W
12.6 -> 13W
missing -> --W
```

and verify GPU metric order remains:

```text
usage -> MHz -> W
```

### 12.4 Renderer tests

In `tests/HudRendererTests.cpp`, verify:

- `W` remains detected as a unit for a GPU power token;
- GPU power is classified as `HudMetricKind::GpuPower`;
- the reserved exemplar is `99W`;
- normal power changes do not change the reserved GPU-power slot width;
- `--W` uses the same reserved slot;
- existing TDP `W`, MHz, GB, RPM, percent, temperature, FPS, and battery layout behavior is unchanged.

Preserve all existing HUD presentation/geometry regression tests.

## 13. Hardware validation

After CI passes, validate on the target Addon-integrated MSI Claw build.

### Pass path

Expected log:

```text
gpuPower=1
gpuPowerW=<finite value>
```

Expected HUD under GPU load:

```text
GPU 9x% xxxxMHz xxW
```

Confirm:

- value changes plausibly with GPU load;
- displayed value is integer-rounded;
- `W` uses the existing small unit typography;
- transitions such as `9W -> 10W` and `19W -> 20W` do not move later HUD segments or the centered HUD line;
- no new per-second warning spam.

### Unsupported path

Expected log:

```text
gpuPower=0
```

Expected HUD:

```text
GPU ... --W
```

This is a valid diagnostic outcome for this PR. Do not add PawnIO or another fallback inside the same implementation merely to turn the result into a number.

### Runtime read-loss path

If the metric was initially valid and then remains missing:

- retain the last valid value through the existing first two missing system polls;
- after the third consecutive missing poll, render `--W`;
- recovery to a valid PresentMon value should resume normal numeric display through the same existing ingestion path.

## 14. Non-goals

This work order does not include:

- standalone/main branch changes;
- PawnIO installation;
- LibreHardwareMonitor integration;
- direct MSR/RAPL PP1 reads;
- IGCL GPU-energy telemetry;
- GPU power control;
- GPU PL1 configuration;
- SteamAddon TDP/GPU-clock policy changes;
- CPU/GPU package-power allocation policy;
- historical graphing;
- additional polling threads/timers;
- telemetry provider abstraction/refactor;
- AMD/NVIDIA generalization;
- HUD presentation architecture changes.

## 15. Review criteria

The PR is ready only if all of the following are true:

- changes are limited to `integration/steamaddon`;
- PresentMon remains the only GPU-power source in this PR;
- unsupported GPU power does not break existing system telemetry;
- raw GPU power remains optional and non-sentinel;
- HUD displays rounded integer watts;
- unavailable displays `--W`;
- GPU power slot reserves `99W` width;
- `W` uses the existing small/unit typography;
- HUD width does not oscillate as GPU power changes;
- existing system telemetry retention semantics are reused;
- no repetitive failure logging was added;
- no new helper/service/driver/dependency was introduced;
- all existing presentation/VRR guardrails remain unchanged;
- focused tests and existing regression tests pass.
