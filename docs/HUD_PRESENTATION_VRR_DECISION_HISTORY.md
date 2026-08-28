# HUD Presentation / VRR / Click-Through / Opacity Decision History

> Status: historical design and hardware-validation record
> Last updated: 2026-08-28
> Scope: ClawHUD HUD presentation path, VRR/Independent Flip safety, click-through, and opacity decisions

## Purpose

This document records **why the current ClawHUD HUD presentation design evolved the way it did**.

It is intentionally different from:

- `PHASE0-VRR-POC.md`, which is a PoC/runbook document,
- `DIAGNOSTICS.md`, which describes the current diagnostic workflow,
- layout/style documents, which describe visual configuration.

The goal here is to preserve the **decision history, failed approaches, hardware observations, and regression evidence** so that future work does not accidentally repeat experiments that were already performed or undo presentation behavior that was introduced for a specific hardware reason.

This document is not a work order and does not, by itself, authorize a production contract change.

---

## 1. Project Goal From the Start

ClawHUD was started as a lightweight Windows performance HUD for MSI Claw devices with one presentation requirement above all others:

> The HUD must not break the game's VRR / Independent Flip path.

The project intentionally avoided DLL injection, Present hooks, DXGI hooks, swapchain interception, and game-specific rendering integration.

The preferred architecture became:

- top-level Win32 HUD window,
- D3D11 displayable BGRA textures,
- Direct2D rendering,
- DirectComposition,
- Windows Presentation API / `IPresentationManager`,
- premultiplied alpha,
- multiple displayable buffers,
- Independent Flip capability required,
- HUD rendered outside the game process.

The initial VRR work also established an important interpretation rule:

- `AllowsTearing=YES` alone does **not** prove VRR.
- capability support alone does **not** prove runtime VRR.
- `Hardware: Independent Flip` and `Hardware Composed: Independent Flip` are both Independent Flip presentation families.
- a transition to ordinary composed presentation, reproducible fixed-refresh quantization, or persistent pacing regression is a much stronger failure signal.

See also: `docs/PHASE0-VRR-POC.md`.

---

## 2. Early Presentation Contract

Before the reliable click-through change, the production HUD used a non-redirection HWND design equivalent to:

```cpp
WS_EX_NOACTIVATE |
WS_EX_TOOLWINDOW |
WS_EX_TRANSPARENT |
WS_EX_NOREDIRECTIONBITMAP |
WS_EX_TOPMOST
```

The rendering path already used:

```text
D3D11 displayable BGRA8 texture
→ Direct2D premultiplied-alpha rendering
→ DirectComposition
→ IPresentationSurface / IPresentationManager
→ Present()
```

This architecture was attractive because transparent pixels in the HUD composition behaved as intended and renderer-side background alpha produced the expected visual transparency.

The important later finding is that **background-only opacity worked correctly in this pre-Layered architecture**.

---

## 3. The Click-Through Problem

The HUD also needed to be fully non-interactive:

- no activation,
- no focus stealing,
- topmost,
- mouse input passes through to the game,
- gamepad behavior unaffected,
- no window hit-test interception.

The code already used:

```cpp
WS_EX_TRANSPARENT
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
```

However, real hardware testing showed that this was not sufficient to provide the reliable cross-process click-through behavior required for the project.

A low-risk attempt to preserve the existing `WS_EX_NOREDIRECTIONBITMAP` architecture while changing input behavior was also tested and did not provide the required real-device result.

The project therefore moved to a narrowly scoped Layered-window PoC.

---

## 4. Layered Click-Through PoC

### PR #66 — `PoC layered window click-through`

PR: https://github.com/onehoon/ClawHUD/pull/66

The PoC changed the HWND presentation style from:

```text
WS_EX_NOREDIRECTIONBITMAP
```

to:

```text
WS_EX_LAYERED
```

and initialized the window with:

```cpp
SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
```

while preserving:

- `WS_EX_NOACTIVATE`
- `WS_EX_TRANSPARENT`
- `WM_NCHITTEST -> HTTRANSPARENT`
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`
- DirectComposition
- Presentation API
- displayable BGRA textures
- premultiplied alpha
- Independent Flip requirement

The PoC was intentionally not merged.

Hardware validation showed that this Layered HWND model solved the required click-through behavior without requiring injection or a second rendering architecture.

---

## 5. Layered Click-Through Adopted in Production

### PR #82 — `Apply layered click-through to production HUD`

PR: https://github.com/onehoon/ClawHUD/pull/82

Merged commit:

```text
0f36b34b6183a6b270133b1303f1649380309c49
```

The production change was deliberately small.

The old contract:

```cpp
WS_EX_NOACTIVATE |
WS_EX_TOOLWINDOW |
WS_EX_TRANSPARENT |
WS_EX_NOREDIRECTIONBITMAP |
WS_EX_TOPMOST
```

became:

```cpp
WS_EX_NOACTIVATE |
WS_EX_TOOLWINDOW |
WS_EX_TRANSPARENT |
WS_EX_LAYERED |
WS_EX_TOPMOST
```

and the HWND was initialized using:

```cpp
SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
```

The Presentation API / DirectComposition / buffer path was otherwise preserved.

This became the production click-through model because it had the required hardware behavior.

---

## 6. The Opacity Regression Appeared After the Layered Transition

After the Layered click-through architecture became production, the existing **background-only opacity** control no longer behaved visually as expected.

The renderer still correctly drew a semi-transparent background using premultiplied alpha.

Conceptually:

```cpp
background alpha = requested Background Opacity
foreground text alpha = 1.0
```

The intended result was:

```text
game / desktop
    visible through
semi-transparent HUD background
    while HUD text remains fully opaque
```

Instead, reducing the renderer background alpha exposed a black-looking surface behind the Presentation content.

At this point it was not yet clear whether the problem was:

- wrong renderer alpha,
- incorrect premultiplication,
- buffer contents,
- Presentation API alpha mode,
- DComp composition,
- or the HWND itself.

---

## 7. Temporary Production Mitigation

### PR #92 — `Temporarily hide opacity control and force 50%`

PR: https://github.com/onehoon/ClawHUD/pull/92

Merged commit:

```text
5c9a6106b7757a887ccfa53d331cce08fcd5e48a
```

The UI control was hidden temporarily and the renderer-side background opacity was forced to 50%.

This PR intentionally did **not** alter:

- window styles,
- click-through,
- Presentation API,
- DirectComposition,
- VRR policy,
- lifecycle,
- alpha mode.

The purpose was to avoid expanding the production surface while the actual alpha problem was investigated.

---

## 8. Renderer / Presentation Alpha Diagnostic PoC

### PR #93 — `[POC] Diagnose HUD presentation alpha`

PR: https://github.com/onehoon/ClawHUD/pull/93

Status:

```text
Closed
Not merged
Diagnostic-only
```

The PoC read back a real pixel from the production D3D11 texture immediately before presentation.

It did not change the presentation contract.

### 8.1 50% background test

The texture showed the expected premultiplied alpha:

```text
requestedOpacity=0.500
expectedAlpha=128
B=1 G=1 R=1 A=128
textureFormat=DXGI_FORMAT_B8G8R8A8_UNORM
alphaMode=PREMULTIPLIED
bufferAlphaResult=PASS
SetBuffer=SUCCESS
Present=SUCCESS
```

The visible HUD background still appeared black rather than showing the game through the transparent background as intended.

### 8.2 0% background test

The renderer was then forced to fully transparent background:

```text
requestedOpacity=0.000
expectedAlpha=0
B=0 G=0 R=0 A=0
textureFormat=DXGI_FORMAT_B8G8R8A8_UNORM
alphaMode=PREMULTIPLIED
bufferAlphaResult=PASS
SetBuffer=SUCCESS
Present=SUCCESS
```

The visible black bar still remained.

This ruled out the renderer and the Presentation texture as the source of the black backing.

### 8.3 HWND backing proof

The diagnostic then painted the HWND client area magenta using ordinary Win32 painting.

When the HUD texture contained transparent pixels, the entire HUD background became magenta.

This proved the composition relationship:

```text
transparent Presentation content
    reveals
Layered HWND backing / redirection content
    instead of
game / desktop directly
```

### Diagnostic conclusion

The opacity failure was **not** caused by incorrect renderer alpha or premultiplied-alpha presentation.

It was caused by the HWND backing/redirection behavior introduced by the Layered-window architecture.

This also explained why background-only opacity had worked under the earlier `WS_EX_NOREDIRECTIONBITMAP` model.

---

## 9. Important Architectural Trade-Off

The hardware findings produced the following trade-off:

### `WS_EX_NOREDIRECTIONBITMAP`

Observed advantage:

- renderer background-only alpha behaved correctly.

Observed problem:

- reliable cross-process click-through was not achieved on the target hardware using the tested input approaches.

### `WS_EX_LAYERED + SetLayeredWindowAttributes(..., 255, LWA_ALPHA)`

Observed advantage:

- required click-through behavior worked on hardware.

Observed problem:

- transparent Presentation pixels reveal the HWND backing/redirection surface, which prevents the previous renderer-only background opacity design from producing the desired visual result.

This was the key presentation design conflict.

---

## 10. Alternatives Considered After the Root Cause Was Known

### Renderer-only background alpha

Rejected as a solution under the Layered production HWND because PR #93 proved that transparent Presentation pixels reveal the HWND backing.

### DirectComposition visual opacity

Technically possible, but it would fade the entire visual, including text and other foreground content.

It does not preserve background-only opacity semantics.

### Layered HWND global alpha

```cpp
SetLayeredWindowAttributes(window_, 0, alpha, LWA_ALPHA);
```

Simple and compatible with the existing Layered HWND.

It fades the entire HUD, including background, text, labels, values, units, and separators.

This became the next PoC direction.

### Color key

Rejected for true semi-transparent background behavior.

### `UpdateLayeredWindow`

Not selected because it would introduce a substantially different window/presentation model and would need new VRR/Independent Flip validation.

### Split background / foreground HWNDs or visual trees

Rejected as unnecessary complexity and presentation risk.

### Screen/game capture used as fake transparency

Rejected.

### Return to `WS_EX_NOREDIRECTIONBITMAP`

Not selected because it would re-open the already observed click-through problem and change the production presentation contract.

---

## 11. Whole-HUD Opacity PoC

### PR #98 — `[POC] Evaluate whole-HUD opacity`

PR: https://github.com/onehoon/ClawHUD/pull/98

Status at the time of this record:

```text
Draft
Open
PoC only
Must not be merged directly
```

The PoC deliberately tested a different product semantic:

```text
Old idea:
Background Opacity
→ only the background changes

PoC:
HUD Opacity
→ the complete HUD changes opacity uniformly
```

The renderer background was forced fully opaque:

```cpp
options.layout.backgroundOpacity = 1.0f;
```

and final HUD opacity was applied only through Layered HWND alpha:

```cpp
SetLayeredWindowAttributes(window_, 0, alpha, LWA_ALPHA);
```

This avoided accidental double multiplication such as:

```text
50% renderer background × 65% HWND alpha = 32.5% effective background
```

The PoC slider used:

- 50% to 100%,
- 5% steps,
- immediate runtime application,
- no D3D/D2D/Presentation resource recreation.

The initial PoC default was 65%.

---

## 12. Whole-HUD Opacity Visual Hardware Result

Real-device visual testing was performed across the slider range.

The preferred visual range was:

```text
65%
70%
75%
```

The important subjective result was that fading the text together with the background did **not** create a problematic visual mismatch.

Instead, the complete HUD appeared less visually heavy, which was judged positively.

The practical candidate production default became:

```text
70%
```

with 65% and 75% remaining nearby user-selectable values.

Click-through also remained functional during the whole-HUD opacity PoC.

---

## 13. VRR / Independent Flip Validation of Whole-HUD Opacity

A real Main Diagnostic run was then performed while the PoC HWND was using whole-HUD Layered alpha.

### Test session

```text
Timestamp: 2026-08-28 19:57:49
Game: Diablo IV
Target PID: 10292
Last selected HUD opacity before diagnostic: 75%
Layered alpha: 191 / 255
```

Runtime logging showed the final opacity selection before the diagnostic:

```text
HUD opacity POC: percent=75 alpha=191 SetLayeredWindowAttributes=SUCCESS
```

No later opacity change occurred before the F8 diagnostic.

The HUD presentation instance was not recreated between that selection and the OFF / STATIC / DYNAMIC test phases.

### Phase result

| Phase | Total Independent Flip | Hardware IF | Hardware Composed IF | Ordinary/non-IF Composed |
|---|---:|---:|---:|---:|
| HUD OFF | 100.0% | 99.9% | 0.1% | 0.0% |
| STATIC HUD | 100.0% | 99.7% | 0.3% | 0.0% |
| DYNAMIC HUD | 100.0% | 91.9% | 8.1% | 0.0% |

Additional observations:

- same game swapchain remained present through all phases,
- `AllowsTearing=YES` for all analyzed target rows,
- all three captures had approximately 96.4% coverage,
- no persistent frame-pacing regression was identified,
- no ordinary composed presentation appeared.

Result:

```text
Main VRR Diagnostic: PASS
VRR Presentation Result: PASS
Diagnostic Integrity: GOOD
```

The diagnostic does not prove physical panel scanout VRR by itself, but it provides strong project-relevant evidence that the whole-HUD alpha PoC did **not** push the game out of the observed Independent Flip presentation family.

---

## 14. Why `Hardware Composed: Independent Flip` Percentage Is Not a Standalone Failure Metric

During review of the opacity PoC, the Diablo IV DYNAMIC phase showed:

```text
Hardware Composed: Independent Flip = 8.1%
```

This initially appeared unusual.

Comparison against earlier hardware captures showed that the value is highly game/path dependent.

### Earlier Diablo IV session

Earlier production/Main Diagnostic:

| Phase | Hardware IF | Hardware Composed IF |
|---|---:|---:|
| OFF | 100.0% | 0.0% |
| STATIC | 99.3% | 0.7% |
| DYNAMIC | 87.1% | 12.9% |

The whole-HUD opacity PoC therefore did **not** introduce a new 8.1% behavior; an earlier Diablo IV run had already shown 12.9% in DYNAMIC.

### Control session

Control produced a very different distribution:

| Phase | Hardware IF | Hardware Composed IF |
|---|---:|---:|
| OFF | 0.3% | 99.7% |
| STATIC | 0.2% | 99.8% |
| DYNAMIC | 5.7% | 94.3% |

Yet Total Independent Flip remained 100% in all phases.

The project test configuration explains why these two games should not be expected to have identical presentation subtypes:

- Diablo IV used its native Intel XeFG path.
- Control used XeFG through OptiScaler because XeFG was being applied to a game without that native XeFG path in this test setup.

The exact reason for the subtype distribution difference is not proven by PresentMon alone, but the evidence strongly supports treating the subtype split as **path/game dependent informational data**, not as an independent VRR failure criterion.

### Current diagnostic interpretation

Primary indicators:

1. Total Independent Flip retention.
2. Appearance of ordinary/non-IF composed presentation.
3. Swapchain continuity.
4. `AllowsTearing` continuity as supporting evidence.
5. Frame-pacing distribution regression.
6. Capture integrity and sufficient phase coverage.

Informational only:

```text
Hardware: Independent Flip
vs
Hardware Composed: Independent Flip
percentage split
```

unless accompanied by a real presentation or pacing regression.

---

## 15. Current Project Conclusion

As of 2026-08-28, the accumulated evidence supports the following direction.

### Keep the current production click-through architecture

Do not re-open the click-through solution casually.

The current Layered HWND contract exists because the earlier non-redirection model did not provide the required real-device click-through behavior under the tested approaches.

### Do not continue pursuing renderer-only background opacity under the same Layered HWND

PR #93 proved that the renderer and Presentation texture already contain correct transparent pixels.

The visual failure comes from the HWND backing/redirection relationship.

Further renderer alpha changes would not address that root cause.

### Prefer whole-HUD opacity

The whole-HUD opacity PoC:

- looked acceptable at 65–75%,
- was visually preferred because the HUD appeared less heavy,
- retained click-through,
- retained the observed Independent Flip path during the tested Diablo IV diagnostic.

Candidate product design:

```text
Setting: HUD Opacity
Range: 50–100%
Step: 5%
Candidate default: 70%
Scope: entire HUD, including foreground text
```

This is a product-semantic change from the original background-only opacity idea.

---

## 16. Productionization Guidance

PR #98 is a PoC and should not be merged directly.

If the whole-HUD design is adopted, create a separate focused production change.

Recommended cleanup:

1. Rename old background-opacity concepts to whole-HUD terminology where appropriate.
2. Use a candidate default of 70%.
3. Keep 50–100% with a 5% step.
4. Keep renderer background opacity at 100% when global HWND opacity is the active final opacity mechanism.
5. Remove PoC-only logging and naming.
6. Avoid unnecessary HUD re-render when only Layered HWND alpha changes.
7. Handle `SetLayeredWindowAttributes` failure safely before persisting a new setting.
8. Preserve all existing presentation behavior unless separately hardware-validated.
9. Re-run Release build, CTest, diff check, click-through validation, and the Main VRR Diagnostic after the production cleanup.

---

## 17. Presentation Safety Boundary

The following behavior should remain treated as a hardware-sensitive presentation contract unless a new explicit design review and PoC authorizes a change:

```text
WS_EX_LAYERED
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
DirectComposition production path
Presentation API production path
displayable BGRA textures
premultiplied-alpha presentation
Independent Flip requirement
```

Do not use ordinary HUD styling work as a reason to change this contract.

Examples of styling work that should remain inside renderer/layout/state layers:

- font size,
- unit size,
- segment width,
- spacing,
- vertical padding,
- separator position,
- numeric alignment,
- HUD size,
- alignment,
- content width.

---

## 18. Known Non-Goals / Already-Rejected Directions

Unless a new requirement justifies reopening the design, do not spend time re-testing the following as the default opacity solution:

```text
renderer-only background alpha under the current Layered HWND
color-key transparency
fake screen/game capture transparency
split HUD windows only to preserve background-only opacity
UpdateLayeredWindow migration without a dedicated presentation PoC
returning to WS_EX_NOREDIRECTIONBITMAP without solving and re-validating click-through
```

These are not forbidden forever, but they should require a new explicit architecture decision rather than being introduced as a UI polish change.

---

## 19. Key PR / Commit Timeline

| Item | Purpose | Result |
|---|---|---|
| `docs/PHASE0-VRR-POC.md` | Establish external HUD / Presentation API VRR validation approach | Foundation |
| PR #66 | Layered HWND click-through PoC | Hardware experiment, not merged |
| PR #82 | Apply hardware-validated Layered click-through to production | Merged |
| commit `0f36b34...` | Production Layered click-through transition | Production |
| PR #92 | Hide broken opacity UI temporarily / force 50% renderer background | Merged |
| commit `5c9a610...` | Temporary opacity mitigation | Production |
| PR #93 | Diagnose real presentation texture alpha | Closed, not merged |
| PR #98 | Evaluate whole-HUD Layered HWND opacity | Draft PoC, not to merge directly |

PR links:

- https://github.com/onehoon/ClawHUD/pull/66
- https://github.com/onehoon/ClawHUD/pull/82
- https://github.com/onehoon/ClawHUD/pull/92
- https://github.com/onehoon/ClawHUD/pull/93
- https://github.com/onehoon/ClawHUD/pull/98

---

## 20. Hardware Evidence Worth Preserving

Current whole-HUD opacity PoC evidence was collected under:

```text
C:\GoogleDrive\ClawHUD\logs\poc
```

Relevant session:

```text
vrr-20260828-195749.txt
VRR_ANALYSIS.md
clawhud.log
vrr-20260828-195749-off.csv
vrr-20260828-195749-static.csv
vrr-20260828-195749-dynamic.csv
```

Earlier comparison sessions included:

```text
Diablo IV:
vrr-20260828-102203.txt

Control:
vrr-20260828-110107.txt
```

The generated CSV files are the raw evidence; analysis Markdown files are derived reports.

---

## 21. Final Decision Snapshot

Current state, in compact form:

```text
External HUD architecture:
    KEEP

Presentation API / DirectComposition:
    KEEP

Premultiplied BGRA displayable-buffer path:
    KEEP

Independent Flip requirement:
    KEEP

Layered click-through HWND:
    KEEP

Background-only opacity:
    ABANDON under the current Layered architecture unless a new design is justified

Whole-HUD opacity:
    POC SUCCESS CANDIDATE

Preferred visual range:
    65–75%

Candidate production default:
    70%

Click-through with whole-HUD alpha:
    PASS on hardware

Observed Independent Flip with 75% whole-HUD alpha:
    100% in OFF / STATIC / DYNAMIC

Ordinary composed mode:
    0% in tested opacity PoC session

Next work:
    productionize the accepted opacity semantics,
    then continue renderer/layout polish without reopening the presentation architecture
```

---

## Related Documentation

- `docs/PHASE0-VRR-POC.md`
- `docs/DIAGNOSTICS.md`
- `docs/HUD_DYNAMIC_PRESENTATION.md`
- `docs/HUD_DISPLAY_LIFECYCLE.md`
- `docs/HUD_LAYOUT_SETTINGS.md`

This document should be updated when a future hardware result changes one of the decisions above.
