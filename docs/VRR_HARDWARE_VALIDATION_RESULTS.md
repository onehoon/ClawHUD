# VRR Hardware Validation Results

Last updated: 2026-08-29

This document is the cumulative hardware-validation record for ClawHUD VRR behavior on MSI Claw hardware. It is intentionally separate from `PHASE0-VRR-POC.md`, which describes the original PoC/runbook, and from `HUD_PRESENTATION_VRR_DECISION_HISTORY.md`, which records presentation-design decisions.

The purpose of this file is to preserve actual hardware observations, diagnostic limitations, and the reasoning that follows from them. New validation runs should be appended rather than replacing prior results.

## Scope and interpretation rules

The primary project requirement is that the production ClawHUD HUD must not break the existing VRR-safe presentation behavior.

The following signals are intentionally kept separate:

- **PresentMon / PresentMode**: verifies the game's presentation path and whether ClawHUD causes an Independent Flip regression.
- **D3DKMT VBlank Cadence**: diagnostic supporting cadence evidence from `D3DKMTWaitForVerticalBlankEvent`.
- **Special K Variable Rate / LFC display**: manual independent runtime observation used during hardware testing.
- **Intel IGCL Arc Sync data**: capability/profile evidence only. The tested `ctlGetVblankTimestamp` path is not currently usable and remains disabled.

Important interpretation boundary:

> `Hardware Composed: Independent Flip` is necessary presentation-path evidence, but it does not by itself prove that the physical panel is actively varying refresh rate.

Likewise, `AllowsTearing=1` does not prove active VRR.

## Current supported validation scope

The primary validation target is **ClawHUD by itself**.

MSI Center M OSD coexistence is not currently a supported ClawHUD validation requirement. Center M OSD runs are retained below because they produced useful diagnostic-method evidence, but they must not redefine the ClawHUD-only production acceptance criteria.

---

# 1. Historical diagnostic stress behavior — 100 ms Dynamic HUD

The original Main VRR Diagnostic used a synthetic DYNAMIC HUD that changed multiple mock telemetry values and rendered every **100 ms**.

Hardware testing showed that this was too aggressive to represent normal ClawHUD production behavior.

## 30 FPS / no Frame Generation / VSync OFF manual observation

Test condition:

- game fixed at approximately 30 FPS
- Frame Generation OFF
- in-game VSync OFF
- Special K visible

Observed behavior:

| State | Special K observation |
|---|---|
| Normal ClawHUD production HUD | Variable Rate below 120 Hz, LFC approximately x3/x4 |
| ClawHUD HUD OFF | Same variable behavior |
| Old diagnostic DYNAMIC @ 100 ms | Variable Rate became fixed at 120 Hz |

This was the key finding that invalidated the old 100 ms synthetic DYNAMIC phase as a production-equivalent VRR test.

### Conclusion

The 120 Hz lock seen in the old DYNAMIC phase was a **diagnostic-induced artifact**, not evidence that the normal production HUD breaks VRR.

The synthetic diagnostic cadence was changed from 100 ms to **500 ms**, matching the fastest recurring production HUD refresh cadence more closely.

---

# 2. Earlier 2026-08-29 captures under the old diagnostic model

These runs are retained as historical evidence but should not be used as the current production-equivalent acceptance baseline because they include the old STATIC/100 ms DYNAMIC diagnostic behavior.

## Diablo IV — approximately 35 FPS — no XeFG

PresentMon:

- same game swapchain across phases
- 100% `Hardware Composed: Independent Flip`
- approximately 35 FPS / 28.7 ms display-change cadence
- `AllowsTearing=1`

D3DKMT phase-wide cadence:

| Phase | Elapsed Hz | Median-derived Hz |
|---|---:|---:|
| HUD OFF | 80.82 | 77.12 |
| STATIC | 81.70 | 80.05 |
| DYNAMIC @ 100 ms | 119.54 | 120.01 |

The large jump to approximately 120 Hz in DYNAMIC was later consistent with the manual Special K observation that the old 100 ms diagnostic could force fixed-refresh behavior.

## Diablo IV — approximately 35 FPS — XeFG 2x

PresentMon remained 100% Independent Flip.

D3DKMT phase-wide cadence:

| Phase | Elapsed Hz | Median-derived Hz |
|---|---:|---:|
| HUD OFF | 94.00 | 113.97 |
| STATIC | 105.83 | 119.71 |
| DYNAMIC @ 100 ms | 119.58 | 120.00 |

A separate STATIC-phase pacing anomaly was also observed in this run (`MsBetweenDisplayChange` P95 approximately 57 ms with many >50 ms samples). It was not used as evidence against production ClawHUD because the legacy diagnostic model was subsequently replaced.

## XeFG 3x run

PresentMon remained 100% Independent Flip.

D3DKMT phase-wide cadence:

| Phase | Elapsed Hz | Median-derived Hz |
|---|---:|---:|
| HUD OFF | 110.01 | 119.82 |
| STATIC | 102.12 | 98.28 |
| DYNAMIC @ 100 ms | 106.11 | 112.76 |

PresentMon observations under XeFG are not treated as authoritative output/generated FPS because Intel UMD-generated frames may not all be visible to PresentMon.

---

# 3. Current diagnostic model — OFF -> DYNAMIC @ 500 ms

The current hardware-validation sequence is:

```text
PHASE A — HUD OFF
PHASE B — DYNAMIC HUD @ 500 ms
```

STATIC is intentionally removed.

The 500 ms DYNAMIC phase updates all synthetic telemetry together and performs one HUD render per tick. The production Presentation API / DirectComposition path and presentation contract remain unchanged.

## Death Stranding Director's Cut — 30 FPS — no FG — first 500 ms validation

Source folder:

```text
C:\GoogleDrive\ClawHUD\logs\0829\ds-30fps-nofg
```

Test condition:

- DX12
- 30 FPS cap
- Frame Generation OFF
- VSync OFF
- Special K visible

PresentMon result:

| Metric | HUD OFF | DYNAMIC @ 500 ms |
|---|---:|---:|
| Target rows | 838 | 838 |
| Independent Flip | 100% | 100% |
| Swapchain | same | same |
| AllowsTearing | 1 | 1 |
| Avg `MsBetweenDisplayChange` | ~33.33 ms | ~33.33 ms |

D3DKMT result:

| Metric | HUD OFF | DYNAMIC @ 500 ms |
|---|---:|---:|
| Sample count | 2880 | 3076 |
| Phase elapsed Hz | 101.93 | 108.93 |
| Median-derived Hz | 101.76 | 118.95 |
| Average delta | 9.810 ms | 9.180 ms |
| Median delta | 9.828 ms | 8.407 ms |

Manual Special K observation in both phases:

- Variable Rate remained below 120 Hz and continued moving.
- LFC remained approximately x3/x4.
- The previous 120 Hz lock did **not** occur.

### Interpretation

This run validated the 500 ms diagnostic change. Normal ClawHUD production behavior, HUD OFF, and DYNAMIC @ 500 ms all preserved visible variable-rate/LFC behavior.

The difference between DYNAMIC phase-wide elapsed Hz (~109 Hz) and median-derived Hz (~119 Hz) also showed that median individual wake-to-wake intervals can over-represent the nominal 120 Hz interval. Individual QPC deltas therefore must not be interpreted as literal physical scanout intervals.

For the D3DKMT PoC, event count over elapsed time is the more useful research metric.

---

# 4. D3DKMT 1-second window validation

PR #106 added non-overlapping approximately 1-second cadence windows calculated from successful D3DKMT wait events divided by elapsed time. The window calculation does not add another sampling thread or timer; it post-processes the already captured QPC timestamps.

PR #106 now promotes this signal from a hardware-validation POC to a mergeable diagnostic-only supporting signal. It remains non-authoritative for physical scanout timing.

## Death Stranding Director's Cut — 30 FPS — no FG — windowed run

Source folder:

```text
C:\GoogleDrive\ClawHUD\logs\0829\ds-30fps-nofg-1
```

PresentMon:

- HUD OFF: 838 rows, 100% Independent Flip
- DYNAMIC: 839 rows, 100% Independent Flip
- same game swapchain
- `AllowsTearing=1`
- overall ~33.33 ms / 30 FPS pacing retained

D3DKMT:

| Metric | HUD OFF | DYNAMIC @ 500 ms |
|---|---:|---:|
| Phase elapsed Hz | 101.22 | 107.84 |
| Median-derived Hz | 99.00 | 116.54 |
| Windowed Hz range | **97–108 Hz** | **101–116 Hz** |
| Windowed Hz average | **101.3 Hz** | **107.8 Hz** |

Representative OFF windows included values such as 97, 98, 99, 100, 102, 104, 106, and 108 Hz.

Representative DYNAMIC windows included values such as 101, 103, 105, 109, 111, 113, 115, and 116 Hz.

### Interpretation

The 1-second D3DKMT result was clearly **variable**, not fixed at nominal 120 Hz, in both ClawHUD-only phases.

This strengthens D3DKMT as an external cadence indicator under the intended ClawHUD-only validation condition. It is not connected to the automatic Main VRR Diagnostic verdict.

---

# 5. MSI Center M OSD coexistence experiment

Source folder:

```text
C:\GoogleDrive\ClawHUD\logs\0829\ds-30fps-nofg-centermosd
```

This folder contains two repeated diagnostic sessions with MSI Center M OSD active.

This is an **out-of-scope coexistence experiment**, not a ClawHUD production acceptance condition.

## Manual observation before/after diagnostic

Observed manually with Special K:

- Center M OSD + normal ClawHUD HUD, outside the diagnostic: Variable Rate continued moving below 120 Hz.
- Starting the ClawHUD diagnostic while Center M OSD remained active caused Special K Variable Rate to become fixed at 120 Hz.
- Both diagnostic HUD OFF and DYNAMIC phases remained fixed at 120 Hz.
- After the diagnostic ended, with Center M OSD and normal ClawHUD HUD still visible, Special K returned to variable-rate behavior.

This means the result must **not** be simplified to "Center M OSD always breaks VRR". The observed fixed-refresh state appeared specifically during the diagnostic coexistence condition.

## Session 161847

PresentMon:

- HUD OFF: 100% `Hardware Composed: Independent Flip`
- DYNAMIC: 100% `Hardware Composed: Independent Flip`
- same swapchain
- `AllowsTearing=1`

D3DKMT:

| Metric | HUD OFF | DYNAMIC @ 500 ms |
|---|---:|---:|
| Phase elapsed Hz | **119.896** | **119.860** |
| Median-derived Hz | **119.992** | **120.000** |
| Windowed Hz range | 117–121 | 116–121 |
| Windowed Hz average | **119.9** | **119.9** |

Almost every complete 1-second window was 119, 120, or 121 events/s.

The first OFF phase contained one unrelated approximately 1.25-second PresentMon timing gap. It did not reproduce in DYNAMIC or the second session and is treated as an isolated capture/runtime anomaly.

## Session 162017 repeat

PresentMon again retained 100% Independent Flip in both phases with the same swapchain and `AllowsTearing=1`.

D3DKMT:

| Metric | HUD OFF | DYNAMIC @ 500 ms |
|---|---:|---:|
| Phase elapsed Hz | **119.859** | **119.861** |
| Median-derived Hz | **120.021** | **120.005** |
| Windowed Hz range | 116–121 | 116–121 |
| Windowed Hz average | **119.9** | **119.9** |

This repeated the first session almost exactly.

## Important finding from the Center M experiment

The Center M coexistence runs provide two useful findings:

1. **Independent Flip does not prove active variable scanout.**
   - All four phases retained 100% `Hardware Composed: Independent Flip`.
   - At the same time, Special K was manually observed at fixed 120 Hz during diagnostic execution and D3DKMT measured approximately 119.9 Hz.

2. **D3DKMT did not simply report 120 Hz in every environment.**
   - ClawHUD-only tests produced clearly variable 1-second windows (~97–116 Hz depending on phase/run).
   - When Special K independently showed the diagnostic coexistence state at fixed 120 Hz, D3DKMT also changed to a very stable ~119.9 Hz result.

This is useful supporting evidence for the D3DKMT PoC, but it does not establish a causal mechanism for the Center M interaction.

No additional Center M isolation work is currently required because Center M OSD coexistence is outside the intended ClawHUD validation scope.

---

# 6. VRR OFF fixed-refresh control

Source folder:

```text
C:\GoogleDrive\ClawHUD\logs\0829\ds-30fps-nofg-VRRoff
```

This run intentionally disabled Intel device VRR while preserving the same Death Stranding 30 FPS / no-FG / VSync OFF test condition.

## Device state confirmation

The diagnostic's IGCL output confirmed the fixed-refresh control condition:

```text
Current Profile: OFF
Active Range: 120-120 Hz
Capability Range: 48-120 Hz
```

Special K was manually observed as:

- `Constant Rate 120 Hz`
- LFC display remained approximately x4 at the 30 FPS game rate

The LFC multiplier display is therefore not treated as equivalent to an "active VRR" flag.

## PresentMon

Both phases remained presentation-path stable:

| Metric | HUD OFF | DYNAMIC @ 500 ms |
|---|---:|---:|
| Target rows | 838 | 839 |
| Independent Flip | 100% | 100% |
| Dominant PresentMode | Hardware Composed: Independent Flip | Hardware Composed: Independent Flip |
| Swapchain | same | same |
| AllowsTearing | 1 | 1 |
| Avg `MsBetweenDisplayChange` | 33.330 ms | 33.331 ms |
| Not-displayed frames | 0 | 0 |

The game remained approximately 30 FPS even though the display was explicitly configured for fixed 120 Hz.

This is direct control evidence that **100% Independent Flip and `AllowsTearing=1` do not prove that VRR is active**.

## D3DKMT

The D3DKMT PoC closely matched the fixed-refresh device state:

| Metric | HUD OFF | DYNAMIC @ 500 ms |
|---|---:|---:|
| Sample count | 3387 | 3385 |
| Phase elapsed Hz | **119.826** | **119.860** |
| Median-derived Hz | **120.109** | **120.029** |
| Windowed Hz range | 116–120 | 117–120 |
| Windowed Hz average | **119.9** | **119.9** |

After the initial partial/startup window, essentially every complete 1-second window reported 120 events/s in both phases.

## Comparison with the VRR ON control

The difference from the otherwise similar VRR ON 30 FPS / no-FG run is clear:

| Device state | Special K | D3DKMT 1-second cadence |
|---|---|---|
| VRR ON | Variable Rate below 120 Hz, LFC x3/x4 | Variable, approximately 97–116 Hz across tested phases |
| VRR OFF | **Constant Rate 120 Hz**, LFC x4 | **Fixed, approximately 120 Hz** |

### Interpretation

This is the strongest D3DKMT control result collected so far.

Under ClawHUD-only conditions:

- when Special K reported variable refresh, D3DKMT produced variable sub-120 cadence;
- when Intel VRR was explicitly disabled and Special K reported Constant Rate 120 Hz, D3DKMT produced a stable ~119.9 Hz cadence;
- PresentMon retained 100% Independent Flip in both VRR ON and VRR OFF conditions, confirming that presentation mode alone cannot distinguish active VRR from fixed refresh.

This materially strengthens the evidence that `D3DKMTWaitForVerticalBlankEvent` can provide a useful external cadence reference on the tested MSI Claw / Intel display path.

It remains experimental and is not yet an authoritative physical scanout API or an automatic VRR verdict source.

---

# 7. Final promotion validation matrix

The following MSI Claw cases justify promoting D3DKMT cadence to a diagnostic supporting signal. Values are retained as observed hardware evidence; they are not universal thresholds or proof of physical panel timing.

## A. VRR OFF negative control

Death Stranding Director's Cut, 30 FPS, no Frame Generation, VSync OFF, device VRR OFF.

- IGCL: Current Profile `OFF`, Active Range `120-120 Hz`.
- Special K manual observation: Constant Rate 120 Hz; LFC display remained approximately x4.
- D3DKMT elapsed cadence: HUD OFF ~119.83 Hz; DYNAMIC ~119.86 Hz; 1-second windows effectively fixed near 120 Hz.
- PresentMon: 100% `Hardware Composed: Independent Flip`, same swapchain, `AllowsTearing=1`.

This control confirms that Independent Flip is not proof of active VRR.

## B. VRR ON, no FG, in-range, LFC 1x

Trails in the Sky 2nd Chapter Demo, approximately 75 FPS target, no Frame Generation, VRR ON.

- PresentMon cadence: approximately 73-74 FPS.
- Special K: Hardware Composed: Independent Flip, Variable Rate, LFC 1x.
- D3DKMT elapsed cadence: HUD OFF ~78.6 Hz; DYNAMIC ~80.9 Hz; 1-second windows approximately 75-86 Hz.

D3DKMT remained variable below 120 Hz without LFC multiplication.

## C. VRR ON, XeFG 2x, approximately 90 FPS output

Mafia: The Old Country, XeFG 2x, post-Frame-Generation output approximately 90 FPS, VRR ON.

- Special K manual observation: Variable Rate was observed above 90 Hz.
- D3DKMT: HUD OFF elapsed ~101.6 Hz; DYNAMIC elapsed ~103.7 Hz; HUD OFF 1-second range approximately 93-108 Hz.
- PresentMon: approximately 90 FPS output cadence with `Hardware Composed: Independent Flip` retained.

D3DKMT cadence is not equal to FPS. It appears useful as a VBlank/refresh-cadence indicator that correlates directionally with Special K Variable Rate, without claiming exact physical panel Hz.

## Promotion conclusion

Hardware validation on MSI Claw shows that D3DKMT VBlank event cadence is useful as an external supporting signal for distinguishing fixed-refresh and variable-refresh behavior. It is not treated as an authoritative physical scanout timestamp source and is not connected to the automatic VRR verdict.

Current production-representative ClawHUD HUD behavior has not shown a reproducible VRR regression in the tested no-FG, LFC, VRR-OFF control, and XeFG scenarios. The earlier fixed-120 behavior was reproducible only with the obsolete 100 ms synthetic diagnostic stress pattern.

# 8. Current conclusions as of 2026-08-29

## Production ClawHUD presentation

Current hardware evidence supports the following:

- Normal ClawHUD HUD operation has not shown a reproducible loss of Independent Flip.
- Current 500 ms production-representative diagnostic rendering preserves 100% Independent Flip in tested Death Stranding runs.
- In the tested 30 FPS / no-FG condition, Special K continued to show variable refresh/LFC behavior with normal ClawHUD and with the 500 ms DYNAMIC diagnostic when device VRR was enabled.
- The old 100 ms synthetic diagnostic was not production representative and could itself force a fixed 120 Hz state.
- VRR OFF control testing also retained 100% Independent Flip, demonstrating that Independent Flip is a presentation-path requirement rather than proof of active VRR.

## D3DKMT VBlank Cadence

Current status: **Validated diagnostic supporting signal**.

Evidence in favor:

- ClawHUD-only VRR ON runs showed variable D3DKMT cadence below 120 Hz.
- Explicit VRR OFF testing produced stable ~119.9 Hz D3DKMT cadence matching the `120-120 Hz` IGCL state and Special K `Constant Rate 120 Hz` observation.
- Fixed-120 diagnostic coexistence runs with Center M OSD also produced repeatable ~119.9 Hz D3DKMT cadence matching the manual Special K observation directionally.
- 1-second event-count/elapsed-time windows are more useful than individual-delta median values for this research.

Remaining limitations:

- `D3DKMTWaitForVerticalBlankEvent` has not yet been proven to be an authoritative physical scanout timestamp source under all Intel VRR conditions.
- D3DKMT remains supporting evidence only and is not an authoritative physical scanout source.
- It must not affect automatic VRR PASS/FAIL logic yet.
- XeFG/LFC inference remains separate and requires care because generated/output frames may not be fully observable through PresentMon.

## Main VRR Diagnostic verdict semantics

The current automatic PASS result primarily means:

> ClawHUD did not cause a persistent regression away from the expected Independent Flip presentation path in the captured comparison.

It must not be interpreted as a standalone proof that the physical panel was actively varying refresh rate for the entire phase.

---

# 9. Next validation work

Continue appending hardware results here.

Useful next runs include:

- VRR ON, approximately 73 FPS, no FG, VSync OFF, to validate a no-LFC variable-refresh case
- another low-FPS target such as approximately 40 FPS to observe a different LFC condition
- later XeFG 2x/3x validation after no-FG cadence behavior is considered sufficiently understood
- one additional game after the Death Stranding matrix is complete, to rule out a game-specific result

For D3DKMT correlation, record both:

- Special K manual Variable Rate / Constant Rate / LFC observation
- D3DKMT 1-second window range and phase-wide elapsed Hz

Do not add compensation factors or heuristics merely to force D3DKMT to match Special K.

---

# 10. Append template for future runs

```markdown
## YYYY-MM-DD — Game — FPS / FG condition

Source folder:

`C:\...`

### Test condition

- API:
- FPS cap:
- VSync:
- Frame Generation:
- Special K:
- Other overlays:
- ClawHUD diagnostic mode/version:

### Manual observation

- HUD OFF:
- DYNAMIC:
- LFC:

### PresentMon

| Metric | HUD OFF | DYNAMIC |
|---|---:|---:|
| Independent Flip | | |
| Dominant PresentMode | | |
| Swapchain continuity | | |
| Avg MsBetweenDisplayChange | | |

### D3DKMT

| Metric | HUD OFF | DYNAMIC |
|---|---:|---:|
| Phase elapsed Hz | | |
| Median-derived Hz | | |
| Windowed Hz range | | |
| Windowed Hz average | | |

### Conclusion

- Presentation-path result:
- VRR/manual observation:
- D3DKMT correlation:
- Caveats:
```
