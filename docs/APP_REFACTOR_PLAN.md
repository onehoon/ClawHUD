# App.cpp Refactor Plan

`src/ClawHUD/App.cpp` is ~2,800 lines with a single `App` class holding ~80 members
and ~120 methods. Every subsystem talks to every other subsystem only through `App`,
which acts as a mediator. This document is the running plan for breaking `App` into
focused controllers. It is committed so work can resume across sessions.

## Goal and non-goals

**Goal.** `App` becomes an assembler plus a thin facade. Each subsystem owns its own
state and is unit tested. `App.cpp` drops to roughly 500-600 lines (bootstrap, `Run()`,
the message pump, and facade forwarding).

**Non-goals.**

- The mediator is *not* fully removed. `App` keeps routing calls between controllers.
  Eliminating that routing (event bus / direct wiring) is a possible later effort.
- `SettingsWindow`, `TrayIcon`, and `VrrDiagnostic` call sites are **not changed**.
  They keep calling `App`; `App` forwards to the owning controller. A residual facade
  on `App` is an accepted end state.
- No behavior changes. Any behavior change is a separate, clearly labelled PR.

## Target structure

```
App  (bootstrap, Run(), message pump, facade forwarding)
 |-- HudSettingsStore                 settings.ini load/save
 |-- DiagnosticsController            EC / IGCL / VRR / PMApi2 lifecycle + mutual exclusion
 |-- HudController                    owns HudPresentation, render, options, mock/production HUD
 |-- HudVisibilityStateMachine        manual override / diagnostic HUD mode / cross-thread requests
 |-- HudTelemetryAggregator           latest* / missingCount* / battery estimate / snapshot build
 |-- ProductionSamplingScheduler      EC / PresentMon / FPS timers, graphics API probe
 `-- ProductionGameDetectionController GameDetection/* wiring + transition handling
```

## Safety net

The app cannot be run on the current development machine (unsupported hardware), and
PRs are auto-merged on green CI without human diff review. The safety net is:

1. **Local full build** including `ClawHUD.exe` (`cmake --build`), run before every push.
2. **Local `ctest`** (full suite), run before every push.
3. **New unit tests** for every piece of pure logic that moves.
4. **CI** (`Build-Test.yml`, VS 2026 / v145) as the auto-merge gate.

Residual risk: imperative Win32 glue that has no test and still compiles can change
behavior silently (a dropped guard, a reordered call, a flipped condition). Mitigations:

- Glue is relocated **verbatim** (identical lines); the only new code is the class
  shell and forwarding stubs. Self-check with `git diff --color-moved=zebra`.
- Every PR ships a **behavior inventory** (see `APP_REFACTOR_BEHAVIOR_INVENTORY_TEMPLATE.md`)
  listing every guard, ordering constraint, and side effect in the moved code, so a
  later reviewer or the next session can audit against it.
- Pure-logic extractions come first; they are provably safe and shrink `App.cpp` by
  roughly a third before any risky relocation.

## Working agreement

- One PR per row in the table below. Branch from `main`, squash-merge, auto-merge on green.
- Build order: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON`
  then `cmake --build build --config Release` then `ctest --test-dir build -C Release`.
  (Local dev may use the Ninja + VS 2022 BuildTools toolchain; CI is the authority.)
- No artificial diff-size cap. Follow natural cluster boundaries. Split a large PR into
  staged commits instead (add file, move methods, wire forwarding).
- If a seam forces a behavior decision, stop and report rather than guess.
- Update the Status column and check the boxes in this file as part of each PR.

## PR sequence

| PR | Title | Cluster | Risk | Status |
|----|-------|---------|------|--------|
| 0 | Refactor plan + behavior inventory template | docs | none | done (#167) |
| 1 | Extract stateless helpers (`Win32Format`, `ProcessLiveness`) | A | very low | done |
| 2 | Extract `HudSettingsStore` (settings.ini load/save) | B | low | todo |
| 3 | Extract `DiagnosticsController` | C | medium | todo |
| 4 | Extract `HudTelemetryAggregator` (retained fields + snapshot) | D | medium | todo |
| 5 | Extract `ProductionSamplingScheduler` | E | medium-high | todo |
| 6 | Extract `HudController` | F | medium | todo |
| 7 | Extract `HudVisibilityStateMachine` | G | high | todo |
| 8 | Extract `ProductionGameDetectionController` | H | high | todo |
| 9 | Message-pump dispatch table + final `App` slimming | I | medium | todo |

### Dependencies

```
PR1, PR2, PR3  independent (do first; PR3 watches the VrrDiagnostic seam)
PR4 -> PR5 -> PR8
PR6 -> PR7 -> PR8
                 `-> PR9 (last)
```

Recommended order for unattended execution: 1, 2, 3, 4, 5, 6, 7, 8, 9.

## Cluster reference (App.cpp regions at plan time, commit 266624e)

| Cluster | Region (approx lines) | What it is |
|---------|-----------------------|------------|
| A | 30-140 | stateless helpers: INI read, `ProcessAlive`, `HexHresult`, `HwndText` |
| B | 2447-2526 | `LoadHudSettings` / `SaveHudSettings` / `SaveHudEnabledSetting` |
| C | 344-570 | EC / IGCL / VRR / PMApi2 `Start*` / `Stop*` / `*Running`, `StopDiagnostic` |
| D | 1035-1240, 1126-1165 | retained telemetry fields, battery estimate wiring, snapshot build |
| E | 1242-1644 | EC / PresentMon / FPS sampling timers, graphics API probe |
| F | 722-1034 | `EnsureMockHud`, `Render*Hud`, `RecreateHudPresentation`, `SetHud*` |
| G | 2096-2357 | visibility state machine, diagnostic HUD hooks, `ReconcileHudVisibility` decision |
| H | 1645-2095 | production game-detection glue, `HandleGameDetectionTransition` |
| I | 142-342, 2428-2643, 2643-2817 | ctor/dtor, `Run()`, single instance, updates, `ProcessMessages` |

### VrrDiagnostic seam (resolved)

`VrrDiagnostic` stores an `App&` and calls seven `App` methods
(`CaptureHudVisibilityState`, `RestoreHudVisibilityState`,
`CancelPendingHudVisibilityRequests`, `RequestDiagnosticHudState`,
`RequestDiagnosticHudMode`, `RequestDiagnosticHudVisibilityMatches`,
`ExecutablePath`). Decision: **do not touch `VrrDiagnostic`.** `App` keeps these as
thin forwarding methods and delegates internally to `HudVisibilityStateMachine` once
PR 7 lands. No interface extraction.

## Progress log

- PR 0: plan committed.
- PR 1: `HexHresult` / `HwndText` moved to `Win32Format.{h,cpp}`, `ProcessAlive` to
  `ProcessLiveness.{h,cpp}`, both under `namespace clawhud`. `App.cpp` keeps
  `using`-declarations so call sites are unchanged. New tests `Win32FormatTests`,
  `ProcessLivenessTests`. Verbatim move.
  - Follow-up noted: `EcHelperClient.cpp` has an identical private `HexHresult`;
    `PresentMonApi2Diagnostic.cpp` has a *different* `ProcessAlive`
    (`OpenProcess(SYNCHRONIZE, ...)` only). Not touched here (dedup of the first, and
    any decision on the second, is separate).
