# Behavior Inventory Template

Every `App.cpp` refactor PR (see `APP_REFACTOR_PLAN.md`) copies this template into the
PR description, filled in for the code being moved. It exists because the moved code is
not runtime-tested on the dev machine and PRs auto-merge without human diff review. The
inventory is the artifact a later reviewer or the next session audits against.

Fill one row per moved function. Be specific: name the exact guard, the exact order,
the exact side effect. "Handles errors" is not an entry; "returns early without calling
`RenderProductionHud` when `suspended_` is true" is.

## Moved functions

| Function | Guard clauses (early returns / conditions) | Ordering constraints | Side effects (timers, PostMessage, I/O, member writes) | Preserved verbatim? |
|----------|--------------------------------------------|----------------------|--------------------------------------------------------|---------------------|
| `Example::Foo` | returns if `!x_` or `y_.empty()` | must call `A()` before `B()` (A arms the state B reads) | `SetTimer(win, kFooId, 500)`, writes `fooActive_ = true`, `Log(...)` | yes |

## Logic that changed (must be empty for a pure-move PR)

- none

## New tests added

| Test file | What it pins down |
|-----------|-------------------|
| | |

## Local verification

- [ ] `cmake --build` full (includes `ClawHUD.exe`) - passed
- [ ] `ctest` full suite - passed (N/N)
- [ ] `git diff --color-moved=zebra` reviewed: every moved line is identical
- [ ] callers unchanged (`SettingsWindow`, `TrayIcon`, `VrrDiagnostic`, `main`)
