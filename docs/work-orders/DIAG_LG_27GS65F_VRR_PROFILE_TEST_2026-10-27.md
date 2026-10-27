# Work Order — ClawHUD.Diag LG 27GS65F Intel Arc Sync VRR Profile Test

**Status:** implementation work order (not yet implemented)
**Date:** 2026-10-27
**Repository:** `onehoon/ClawHUD`
**Reviewed baseline:** `main` @ `356d55db464ffcca67f775e3751e25b7ad18986e`
**Delivery:** one implementation PR, target **under 500 changed LOC**
**Scope:** one user-triggered, model-targeted action under existing Diag main-menu item **3**. No production HUD or automatic tweak changes.

---

## 1. Decision / outcome

Add exactly one **top-level menu entry** to `ClawHUD.Diag.exe`:

~~~text
ClawHUD Diagnostic

1. Game Detection Diagnostic
2. VRR Diagnostic
3. LG 27GS65F VRR Profile Test
4. Exit
~~~

Menu 3 is a **manual, interactive hardware validation tool**, not a generic VRR optimizer. It must:

1. Identify the **single active LG UltraGear 27GS65F external display** as safely as Windows exposes its model identity.
2. Resolve that *specific Windows display target* to one Intel IGCL output using **adapter LUID + target ID**, with exactly one match.
3. Read and show Intel Arc Sync capability and active profile/range.
4. After explicit confirmation, change `RECOMMENDED -> EXCELLENT` for that output **only**, read back, and report whether the full **48–180 Hz** range was restored.
5. On another visit to menu 3, support an explicit **Restore RECOMMENDED** operation (not automatic at Diag exit) to undo this test.
6. Leave the existing game VRR diagnostic and Claw internal-panel production fix completely unchanged.

Do not turn this into a configurable user-selected-display framework, broad external-display support, scheduled repair, INI setting, or background service.

## 2. Verified hardware evidence (2026-10-27 field capture)

Hardware context supplied with the diagnostic:

- Display: **LG UltraGear 27GS65F**, USB-C -> DisplayPort, monitor Adaptive-Sync mode **Extended**.
- Claw internal display **disabled**; only the LG external monitor in use.
- Windows Advanced Display reports **180 Hz** selected.
- Intel Graphics software shows active VRR **90–180 Hz**.
- ClawHUD VRR diagnostic was captured at 20:55:37 KST, `vrr-20261027-205537`. The raw capture stays in the user's private diagnostic storage; do not copy private artifacts or local device paths into this repository.

The field `report.txt` contains:

~~~text
Intel Arc Sync:
  Probe status           INITIALIZED
  Target mapping         EXACT
  Enumeration complete   YES
  Supported              YES
  Capability API result  0x00000000
  Profile                RECOMMENDED
  Profile API result     0x00000000
  Capability range       48.00-180.00 Hz
  Active range           90.00-180.00 Hz
  Configuration          ENABLED
  Probe failures         0
~~~

**Interpretation:** IGCL already acknowledges the full 48–180 Hz hardware capability but currently applies a narrower RECOMMENDED active range. Reusing the existing production fix's `ctlSetIntelArcSyncProfile(EXCELLENT)` technique is a justified **test hypothesis**, not an already proven fix. The only success criterion is actual IGCL readback of EXCELLENT + 48–180 Hz on the same display target.

**Separate issue, explicitly out of scope:** this run also reported nominal 59.999 Hz, ~60 Hz D3DKMT VBlank, `COMPOSED_FLIP` 100%, Independent Flip 0%, and `NOT_VRR_SAFE` for Control DX12. The user says Windows selected 180 Hz. Preserve that discrepancy as a later investigation; do not alter PresentMon, D3DKMT, the VRR classifier, or production presentation to fix it as part of this work.

## 3. Code review / existing reuse points

### Current Diag main menu

`src/ClawHUD.Diag/main.cpp` currently has `1 Game Detection Diagnostic`, `2 VRR Diagnostic`, `3 Exit`. Add the LG test as 3 and move Exit to 4. Preserve 1 and 2 unchanged.

### Current display-to-IGCL mapping (MUST reuse)

- `src/ClawHUD.Diag/DisplayPathProbe.{h,cpp}` already maps an `HMONITOR` / Windows display source to an active `QueryDisplayConfig` path with `targetAdapterLuid` and `targetId`; it rejects duplicate/ambiguous display-source matches.
- `src/ClawHUD.Diag/DiagIntelVrrStateProbe.{h,cpp}` already gets adapter LUID via `ctlGetDeviceProperties`, output target ID via `ctlGetDisplayProperties`, and uses `ResolveDiagIgclTarget` for the **exact pair**. The current field capture found one match out of 10 enumerated IGCL outputs.
- It loads `ControlLib.dll` from `SYSTEM32` and calls `ctlGetIntelArcSyncInfoForMonitor` and `ctlGetIntelArcSyncProfile`. Its IGCL ABI structures (`ArcSyncCapabilityAbi` and `ArcSyncProfileAbi`) and offset checks already exist.

**Do not** identify a write target using IGCL enumeration order, first output, 48–180 range alone, the persisted diagnostic target ID `4165`, `DISPLAY1` alone, or "one monitor" alone. Handles and IDs must be resolved **fresh** on each invocation.

### Existing profile mutation code for reference only

`src/ClawHUD/Tweaks/IntelVrr/IntelArcSyncClient.cpp` already implements `ctlSetIntelArcSyncProfile` and the enum `IntelArcSyncProfile::{Recommended,Excellent,...}`. `IntelVrrRangeTweak.cpp` applies EXCELLENT and verifies readback, but it is **specifically guarded for the MSI Claw internal 48–120 Hz panel**.

Do **not** relax its panel detector, change its native range constants, call the production tweak from Diag, or wire LG handling to startup. Prefer adding a small controlled IGCL write to the existing Diag probe, sharing its exact output-resolution logic; avoid copying the entire independent IGCL implementation to a new module.

### Build and tests

- The `ClawHUD.Diag` source list is in root `CMakeLists.txt`, with `cxx_std_20`, existing `user32` and `gdi32` links.
- `cmake/ClawHUDTests.cmake` already registers `ClawHUD.DiagIntelVrrStateProbeTests` and `ClawHUD.DiagDisplayPathProbeTests`.
- Extend existing tests and register only the small additional test target(s) genuinely needed; do not change other products' dependency graph.

## 4. LG-targeted identity and preflight

Menu 3 must **not require a game to be running**. It must not use `VrrTargetAcquisition` or start PresentMon capture.

1. Read current **active** Windows display paths (`QDC_ONLY_ACTIVE_PATHS`). Require exactly **one active target path**, and identify a single corresponding desktop `HMONITOR`. On multiple targets (including clone configurations), disconnected outputs, unknown state, or ambiguous mapping: **read-only message + return**.
2. Read the active target's `DISPLAYCONFIG_TARGET_DEVICE_NAME` using `DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME)`. Display its actual `monitorFriendlyDeviceName` and monitor device path. For this user-specific test, identify an **LG** monitor (EDID manufacturer commonly `GSM`, validated through the monitor device path / Windows monitor identity), with a friendly/model identity compatible with `27GS65F` or the actual LG UltraGear identity returned by the machine.
3. Do not invent or hard-code an LG EDID product code, and do not assume Windows exposes the exact retail model string. Because some LG EDIDs say only `LG ULTRAGEAR`, accept that actual alias **only with the matching LG manufacturer, a single active display, the 48–180 Hz capability check below, and explicit user confirmation**. Never treat a generic monitor name as sufficient. Print the observed identity for field validation.
4. Obtain the Windows target adapter LUID + target ID from `DisplayPathProbe`, and require `ResolveDiagIgclTarget` result **EXACT**, complete device/output enumeration, exactly one matching output, successful capability/profile reads.
5. Require supported IGCL VRR and capability approximately **48.0–180.0 Hz** (small floating-point tolerance, e.g. ±0.25 Hz). Do not write ranges outside the capability or invent a driver override.
6. For Apply, permit only current profile `RECOMMENDED` with the observed **90–180 Hz** narrow active range (approximately). If already `EXCELLENT / 48–180`, report success without writing. If `CUSTOM`, `OFF`, unexpected profile, unexpected range, or failed preflight: show current values and **skip**. Never overwrite user-selected CUSTOM/OFF.
7. Show all identifiers/profile/ranges and ask for affirmative console confirmation **after** preflight. Blank/invalid input means no write. Avoid permanently storing adapter/output handles or identity in configuration.

Note: the exact LG name and any EDID product code have **not** been verified by the existing VRR report (it does not contain them). If Windows does not expose enough identity to satisfy this targeted test, print a clear "identity not verified; no change" result plus the observed values. Do not silently downgrade to a range-only match.

## 5. Menu-3 user flow

Suggested single top-level entry with a small in-command choice:

~~~text
=== LG 27GS65F VRR Profile Test ===
Active monitor: <Windows-reported name>
Monitor identity: <manufacturer/device path details>
Windows target: <LUID> / <target ID>
IGCL mapping: EXACT
Capability range: 48.00-180.00 Hz
Current profile: RECOMMENDED
Current active range: 90.00-180.00 Hz

1. Apply EXCELLENT (try native 48-180 Hz)
2. Restore RECOMMENDED
3. Back
Select:
~~~

**Apply:** enter 1, print targeted change and ask explicit `y/N`. Revalidate target identity, exact IGCL mapping, current profile, and capability immediately before invoking setter. Set only `EXCELLENT`. Read back the **same** target after the call, and print:

~~~text
Apply result: SUCCESS | FAILED | VERIFICATION FAILED | SKIPPED
Before: RECOMMENDED / 90.00-180.00 Hz
After:  EXCELLENT   / 48.00-180.00 Hz
IGCL API result: 0xXXXXXXXX
~~~

Mark SUCCESS **only** if the setter succeeded, the same output still maps exactly, and readback returns `EXCELLENT` and approximately `48–180`. "Set returned SUCCESS" alone is insufficient. If verification fails but identity is still safe and known, an immediate best-effort revert to the pre-write `RECOMMENDED` state is acceptable; report the rollback result independently, with no unbounded retry logic. If display topology/identity changed, **do not perform a further write**.

**Restore:** explicitly choose 2, re-run all LG identity, single-active-path and exact-mapping checks, and only permit `EXCELLENT -> RECOMMENDED` for this LG capability. Confirm `y/N` and read back; report the actual resulting profile/range, which may again be `RECOMMENDED / 90–180`. Do not assume reverting causes the Windows refresh rate to change, or claim the old active range is guaranteed.

**Persistence:** changing an Intel driver profile may persist beyond this Diag run; do not auto-revert when exiting the menu or closing the process. Make the restoration action discoverable. Driver reconnect/restart persistence is **to be observed**, not assumed.

## 6. Recommended minimal implementation

- Add `src/ClawHUD.Diag/Lg27gs65fVrrProfileCommand.{h,cpp}`: menu-3 orchestration, identification/preflight, interactive confirmation, result text.
- Extend `DiagIntelVrrStateProbe` with a **target-scoped** write method (name is implementation choice), sharing the existing `LUID + target ID` output resolution rather than opening an independent second IGCL enumeration path. The method must reject non-exact/incomplete selection and return the raw IGCL result for diagnostics.
- Resolve `ctlSetIntelArcSyncProfile` from the same system32 `ControlLib.dll` and use the existing ABI layout; initialize size/version correctly. Review how the existing production `IntelArcSyncClient::SetArcSyncProfile` constructs the setter payload.
- Amend `main.cpp` and the root `CMakeLists.txt` Diag target source list.
- Put the *testable preflight decision* in a small pure function or equivalent isolated logic so tests need no real LG hardware. Keep it small; no generic display-manager abstraction.

Illustrative control flow (pseudocode, **not** copy/paste production-ready C++):

~~~cpp
const auto target = FindSingleActiveLgUltraGearTarget();
if (!target || !target->identityVerified)
    return PrintSkipped("LG target not uniquely identified");

auto before = probe.Query(target->adapterLuid, target->targetId);
if (!EligibleForApply(*target, before))
    return PrintSkipped("Unsafe or unexpected preflight state");

if (!ConfirmUser("Apply EXCELLENT? [y/N]"))
    return PrintSkipped("Cancelled");

// Re-check identity, active display topology and IGCL state just before writing.
if (!RecheckSameExactTargetAndPreflight(*target, before))
    return PrintSkipped("Display or profile changed");

auto setResult = probe.SetProfileOnExactTarget(
    target->adapterLuid, target->targetId, /* EXCELLENT */ 2);
auto after = probe.Query(target->adapterLuid, target->targetId);
ReportRawResultAndVerifiedRange(setResult, after);
~~~

The `2` in this sketch is the existing IGCL `EXCELLENT` enum value; use a named enum or constant in actual code. The same exact-target and readback discipline applies to Restore.

**Do not** add any new write capability to the passive VRR diagnostic (menu 2). Keep its probe and report format read-only.

## 7. Explicit non-goals / safety invariants

**Out of scope:** production Claw panel Fix behavior, settings UI, automatic external-monitor fixes, EDID/CRU overrides, registry monitor overrides, display mode forcing, refresh-rate changes, game PresentMon metrics, D3DKMT cadence logic, VRR verdict logic, LG OSD manipulation, GPU driver updates.

**Absolutely preserve** production HUD `windowExStyle`, `WS_EX_TRANSPARENT`, `WS_EX_NOACTIVATE`, `WS_EX_TOPMOST`, `WS_EX_LAYERED` behavior, `WM_NCHITTEST -> HTTRANSPARENT`, `WM_MOUSEACTIVATE -> MA_NOACTIVATE`, `ProductionHudPresentationContract()`, independent-flip requirement, Presentation API / DirectComposition path and premultiplied-alpha contract. Do not "fix" the 60 Hz diagnostic issue by weakening any HUD presentation contract.

No broad architecture change. No thread, timer, polling loop, retry/state machine or elevation workflow for a one-shot manual test.

## 8. Test checklist

Automated unit tests should cover:

1. LG + one active Windows path + exact LUID/target matching + supported capability 48–180 + RECOMMENDED 90–180: Apply permitted **only after confirmation**.
2. EXCELLENT 48–180: idempotent, no second setter call.
3. CUSTOM, OFF, unsupported, insufficient/failed IGCL read, unrecognized profile, mismatched capability: no mutation.
4. Multiple active displays / clone paths / ambiguous or missing IGCL output / incomplete enumeration: no mutation.
5. Wrong adapter LUID with same numeric target ID: no mutation.
6. Non-LG or unverified identity, missing manufacturer, generic Windows display label: no mutation.
7. Cancelled/invalid stdin input: no mutation.
8. Successful setter but unchanged profile/range on readback: **not SUCCESS**. Failed setter preserves precise IGCL error; optional rollback is properly reported.
9. Restore option requires same LG/exact-target preflight and user confirmation; prints observed range.
10. Existing `DiagIntelVrrStateProbeTests`, `DiagDisplayPathProbeTests` and `IntelVrrRangeTweakTests` remain passing. HUD presentation/click-through/independent-flip contract regression tests remain unchanged/passing.

Manual test on the user's Claw (LG-only, USB-C -> DP, Extended mode):

- Before: `RECOMMENDED / capability 48–180 / active 90–180`.
- Apply: `EXCELLENT / active 48–180` is the **expected hypothesis**, verify via Diag readback **and** Intel Graphics UI.
- Restore: check `RECOMMENDED` readback and actual resulting active range.
- Re-run menu 2 **later** in a game to separately investigate why prior diagnostic observed 60 Hz despite Windows 180 Hz. This is not an acceptance blocker for profile setter correctness.
- On hardware failure, retain exact returned codes and observed monitor identity instead of declaring success.

## 9. Definition of done

- The main menu contains exactly the requested LG-specific option 3 and Exit 4.
- It operates without launching a game, only on one verified LG display and an exact matching Intel output.
- It can confirm/apply EXCELLENT and explicitly restore RECOMMENDED with readback.
- It never writes to other displays, CUSTOM/OFF states, or unknown identities.
- Menu 2 remains passive and unchanged; production internal-panel 48–120 Fix and VRR-critical HUD presentation are untouched.
- Relevant tests pass; one implementation PR under 500 changed LOC if feasible.

**Do not claim the 48–180 Hz correction is proven until the real hardware test succeeds.**
