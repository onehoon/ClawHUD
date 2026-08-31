# HUD Presentation Refactor Guardrail — NON-NEGOTIABLE

Status: **HARD STOP / MUST PRESERVE**  
Applies to: every ClawHUD refactor, cleanup, HUD feature, renderer change, lifecycle change, opacity change, sizing/alignment change, telemetry change, suspend/resume change, and game-detection change that can reach HUD presentation.

This document supplements `docs/APP_REFACTOR_PLAN.md` §6 and is intentionally repetitive.
The production HUD presentation contract is a VRR-critical invariant and is **not a refactorable implementation detail**.

## Absolute rule

**Do not modify, replace, weaken, bypass, emulate, reinterpret, or work around the existing production HUD presentation contract.**

If a refactor appears to require changing any item in this contract, **stop implementation**. Preserve the current production path unchanged and report the conflict for explicit design review.

There is no acceptable refactor outcome where a cleaner architecture is obtained by changing the presentation contract.

## Protected production contract

The following are frozen for this refactor and must remain behaviorally unchanged:

- HUD `windowExStyle`
- `WS_EX_TRANSPARENT`
- `WS_EX_NOACTIVATE`
- `WS_EX_TOPMOST`
- existing `WS_EX_LAYERED` behavior
- `WM_NCHITTEST -> HTTRANSPARENT`
- `WM_MOUSEACTIVATE -> MA_NOACTIVATE`
- `ProductionHudPresentationContract()`
- independent-flip requirement
- existing Presentation API production path
- existing DirectComposition production path
- premultiplied-alpha presentation contract
- current production buffer/texture semantics required by `ProductionHudPresentationContract()`
- current click-through behavior
- current no-activation behavior
- current topmost behavior

`HudPresentation` is therefore a **VRR-critical black box** during the architecture refactor.

A future `HudController` may own the lifetime of the existing concrete `HudPresentation` object and may move existing calls such as `Initialize`, `Render`, `Show`, `Hide`, `SetHudOpacity`, and `Shutdown` behind a clearer owner. It must not redesign how those operations are implemented.

## Explicitly prohibited during refactor

Do not use a refactor PR to:

- change `CreateWindowExW` extended styles for the HUD;
- remove or alter click-through/no-activation flags;
- change hit-testing or mouse activation behavior;
- switch the production presentation backend;
- replace Presentation API with a normal swap chain, layered-window renderer, DComp-only substitute, or another presentation mechanism;
- weaken or make optional the independent-flip requirement;
- change premultiplied alpha to straight/ignore/unspecified alpha;
- change the production path because another implementation is easier to wrap in a controller;
- apply window-wide or visual-wide opacity as a shortcut for background opacity;
- change buffer/texture/presentation semantics solely to simplify ownership or testing;
- add a fallback production presentation path that silently violates the existing contract;
- bypass `ProductionHudPresentationContract()` with duplicated constants in another controller;
- move presentation constants into a new abstraction and change their values while claiming the change is mechanical;
- alter presentation behavior as part of `HudController`, suspend/resume, telemetry, game-session, settings, or message-loop extraction.

## Background opacity rule

`Background Opacity` means **background only**.

Do not implement or refactor background opacity by applying opacity to the whole HWND, whole DirectComposition visual, or any other level that also fades HUD text, outlines, separators, or other foreground content.

An opacity problem is never justification to modify click-through, activation, hit testing, independent flip, the Presentation API path, or any other protected presentation invariant.

## Refactor-specific rule for `HudController`

The intended architecture is:

```text
HudController
  `-- owns lifecycle/state around
       `-- existing HudPresentation concrete implementation
```

Not:

```text
HudController
  `-- new presentation abstraction/backend
       `-- altered HUD contract
```

Allowed refactor work includes:

- moving ownership of `std::unique_ptr<HudPresentation>` out of `App`;
- moving existing lifecycle call sites while preserving their order and conditions;
- moving HUD options/manual visibility state into the new owner;
- preserving the current recreate/rollback behavior;
- forwarding the same `HudRenderOptions` / `HudTelemetrySnapshot` inputs;
- preserving the same Show/Hide/Initialize/Shutdown semantics.

Not allowed:

- changing presentation implementation to make the move easier;
- changing the contract to reduce dependencies;
- changing behavior because the controller boundary would otherwise be awkward.

If ownership extraction cannot be completed without contract changes, leave that ownership in `App` and report the boundary as unresolved. A partial refactor is preferable to a presentation regression.

## Required PR gate

Every refactor PR that touches any HUD lifecycle, HUD state, HUD visibility, presentation call site, suspend/resume path, or renderer integration must explicitly confirm all of the following:

- [ ] `ProductionHudPresentationContract()` values are unchanged.
- [ ] HUD `windowExStyle` is unchanged.
- [ ] `WS_EX_TRANSPARENT` is preserved.
- [ ] `WS_EX_NOACTIVATE` is preserved.
- [ ] `WS_EX_TOPMOST` is preserved.
- [ ] existing `WS_EX_LAYERED` behavior is preserved.
- [ ] `WM_NCHITTEST -> HTTRANSPARENT` is preserved.
- [ ] `WM_MOUSEACTIVATE -> MA_NOACTIVATE` is preserved.
- [ ] Presentation API / DirectComposition production path is unchanged.
- [ ] independent flip remains required.
- [ ] premultiplied alpha remains unchanged.
- [ ] background opacity still affects background only.
- [ ] no fallback/workaround path was added that weakens any invariant above.
- [ ] existing HUD presentation contract/lifecycle tests remain green.

If any checkbox cannot be truthfully checked, **the PR is not a valid refactor PR and must not be merged**.

## Regression requirement

Existing tests/assertions for these invariants are mandatory regression gates:

- click-through behavior;
- no activation;
- topmost behavior;
- transparent hit testing;
- independent flip;
- premultiplied alpha;
- production presentation contract;
- presentation lifecycle behavior.

Tests may be mechanically moved/rewired if file ownership changes, but their asserted production behavior must not be weakened, deleted, or rewritten to accommodate a refactor.

## Conflict handling

If a future task appears to need a contract change:

1. stop the implementation that would modify the contract;
2. preserve the current production presentation code and tests;
3. first attempt the fix in the renderer, HUD state/options, buffer contents, telemetry, lifecycle orchestration, or another non-contract layer;
4. if no solution exists without a contract change, document the exact conflict and request explicit design review;
5. do not merge a workaround that silently weakens VRR, click-through, no-activation, topmost, hit-testing, or alpha behavior.

## Review policy

For PR review, any actual modification or weakening of this contract is a **blocking defect** regardless of whether the code compiles or ordinary HUD tests appear visually correct.

The architecture refactor exists to clarify ownership around production behavior, not to redefine that behavior.
