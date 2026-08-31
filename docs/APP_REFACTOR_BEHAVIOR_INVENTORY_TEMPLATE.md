# Behavior Inventory Template

Every `App.cpp` refactor PR (see `APP_REFACTOR_PLAN.md`) copies this template into the
PR description, filled in for the code being moved. It exists because the moved code is
not runtime-tested on the dev machine and PRs auto-merge without human diff review. The
inventory is the artifact a later reviewer or the next session audits against.

Before editing any HUD lifecycle/presentation-related code, also read
`HUD_PRESENTATION_REFACTOR_GUARDRAIL.md` and `APP_REFACTOR_PLAN.md` §6.
The HUD presentation contract is a **hard stop boundary**, not a cleanup target.

Fill one row per moved function. Be specific: name the exact guard, the exact order,
the exact side effect. "Handles errors" is not an entry; "returns early without calling
`RenderProductionHud` when `suspended_` is true" is.

## Moved functions

| Function | Guard clauses (early returns / conditions) | Ordering constraints | Side effects (timers, PostMessage, I/O, member writes) | Preserved verbatim? |
|----------|--------------------------------------------|----------------------|--------------------------------------------------------|---------------------|
| `Example::Foo` | returns if `!x_` or `y_.empty()` | must call `A()` before `B()` (A arms the state B reads) | `SetTimer(win, kFooId, 500)`, writes `fooActive_ = true`, `Log(...)` | yes |

## Logic that changed (must be empty for a pure-move PR)

- none

## HUD Presentation Contract Gate — MANDATORY

Complete this section for every refactor PR that touches HUD lifecycle, HUD visibility,
HUD settings that trigger recreation, suspend/resume HUD handling, renderer integration,
or any `HudPresentation` call site.

The protected contract may **not** be modified, weakened, replaced, bypassed, or worked
around to make the refactor easier. If any item below cannot be checked truthfully, stop the
implementation and report the conflict for design review. Do not merge the PR.

- [ ] `ProductionHudPresentationContract()` values are unchanged.
- [ ] HUD `windowExStyle` is unchanged.
- [ ] `WS_EX_TRANSPARENT` is preserved.
- [ ] `WS_EX_NOACTIVATE` is preserved.
- [ ] `WS_EX_TOPMOST` is preserved.
- [ ] existing `WS_EX_LAYERED` behavior is preserved.
- [ ] `WM_NCHITTEST -> HTTRANSPARENT` is preserved.
- [ ] `WM_MOUSEACTIVATE -> MA_NOACTIVATE` is preserved.
- [ ] existing Presentation API / DirectComposition production path is unchanged.
- [ ] independent flip remains required.
- [ ] premultiplied-alpha presentation behavior is unchanged.
- [ ] background opacity still affects background only; no HWND/visual-wide opacity shortcut was introduced.
- [ ] no fallback/workaround presentation path was added that weakens any protected invariant.
- [ ] presentation contract/lifecycle tests were not weakened or removed.

For `HudController` extraction specifically:

- [ ] the existing concrete `HudPresentation` implementation is treated as a VRR-critical black box;
- [ ] only ownership/lifecycle call placement changed;
- [ ] no backend/window/alpha/buffer/presentation-contract redesign is included;
- [ ] if ownership could not move without a contract change, that ownership was left in place instead.

## New tests added

| Test file | What it pins down |
|-----------|-------------------|
| | |

## Local verification

- [ ] `cmake --build` full (includes `ClawHUD.exe`) - passed
- [ ] `ctest` full suite - passed (N/N)
- [ ] `git diff --color-moved=zebra` reviewed: every moved line is identical
- [ ] callers unchanged where required by the PR (`SettingsWindow`, `TrayIcon`, `main`)
- [ ] `HudPresentationContractTests` pass when the PR touches HUD/presentation lifecycle
- [ ] `HudPresentationLifecycleTests` pass when the PR touches HUD/presentation lifecycle
