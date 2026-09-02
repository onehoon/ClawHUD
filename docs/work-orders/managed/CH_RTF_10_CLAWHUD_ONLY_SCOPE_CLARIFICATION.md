# CH-RTF-10 — ClawHUD-Only Completion Scope Clarification

> **Applies to:** `CH_RTF_10_FINAL_INTEGRATION_CONTRACT_REGRESSION_AND_CLEANUP_WORK_ORDER.md`  
> **Decision:** ClawHUD itself is the completion target. SteamAddon integration is deferred and will be planned separately later.

## Scope clarification

CH-RTF-10 completes the current work when **ClawHUD itself** is stable and complete as an independently usable product/runtime.

The completion target is:

```text
ClawHUD.exe
    -> Standalone
       -> shared production runtime
       -> Control IPC
       -> tray
       -> legacy Settings

ClawHUD.exe --managed
    -> Managed
       -> same production runtime
       -> same Control IPC
       -> no tray
       -> no legacy Settings
```

The existing Control IPC and Managed launch mode are ClawHUD capabilities in their own right. They must be complete, tested, and stable without requiring any SteamAddon implementation to declare this series finished.

## SteamAddon is explicitly out of the current completion target

Do not treat any SteamAddon work as a dependency, acceptance criterion, follow-up PR in this series, or condition for closing CH-RTF-10.

The following are **not part of the current ClawHUD completion target**:

```text
SteamAddon installation discovery
SteamAddon HUD settings UI
SteamAddon Control IPC client
SteamAddon process ownership
Standalone -> Managed conversion initiated by SteamAddon
Job Object ownership
SteamAddon crash/restart supervision
SteamAddon update/restart coordination
SteamAddon uninstall integration
SA-HUD-* implementation
```

Those items may be designed and implemented later as a separate SteamAddon project/series.

## Interpretation rule for the existing CH-RTF-10 work order

Where the CH-RTF-10 work order refers to a future SteamAddon integration or a future external owner, interpret that text only as **future compatibility context**, not as the next required implementation step.

In particular, wording such as:

```text
"handoff surface for future SteamAddon integration"
"subsequent work moves to SA-HUD-*"
"future external owner relaunches --managed"
```

does **not** extend CH-RTF-10 scope beyond ClawHUD.

The authoritative completion rule is:

> **After CH-RTF-10, the ClawHUD Runtime / Frontend Separation series is complete on the ClawHUD side. No SteamAddon PR is required next.**

## CH-RTF-10 acceptance remains ClawHUD-only

CH-RTF-10 should therefore finish with ClawHUD independently satisfying all of these:

- Standalone remains fully usable with tray and legacy Settings.
- Managed launches explicitly with `--managed`, has no tray/legacy Settings, and keeps the same production runtime.
- Control IPC protocol v1 remains stable and fully functional.
- All Control mutations continue through the main-thread runtime-control authority.
- Shared settings persistence works across Standalone and Managed.
- Cross-mode single-instance behavior remains correct.
- Managed launch does not implicitly own/reconcile the Standalone startup shortcut.
- Mode-aware VeloPack behavior remains correct.
- PresentMon runtime bootstrap remains valid on normal launches.
- All existing HUD/game-detection/telemetry/tweak behavior remains mode-independent.
- All HUD/VRR production presentation invariants remain unchanged.
- Full Release build/CTest regression validation passes.
- Required Standalone/Managed manual validation passes on the target Claw hardware.

No external controller application is required to satisfy these acceptance criteria.
