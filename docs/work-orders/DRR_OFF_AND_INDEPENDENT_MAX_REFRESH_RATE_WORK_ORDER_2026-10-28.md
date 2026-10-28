# ClawHUD Work Order — DRR OFF + Independent Maximum Fixed Refresh Rate

> **Repository:** `onehoon/ClawHUD`
> **Date:** 2026-10-28
> **Status:** implementation work order only; this commit must not change production behavior
> **Implementation first:** `integration/steamaddon`, then user hardware verification
> **Next:** `main` — cherry-pick the verified addon VRR series, **not** a whole-branch merge
> **Prerequisite addon squash:** PR #276 (multi-panel IGCL exact-target + unified VRR diagnostic log), `8d788977f39ee2002e8282d5274a7e4989ee74b6`
> **Prior DRR integration:** addon PR #274 / main PR #275
> **Scope:** Windows 11 DRR state and fixed refresh-rate selection for *active Windows displays*, independent of Intel IGCL panel support
> **No UI change; no HUD presentation-contract change**

## 0. User decision / non-negotiable behavior

**DRR OFF and maximum fixed refresh rate are separate operations.** They run during the same enabled VRR Fix startup, but the fixed-rate operation **must run even if DRR was already OFF**. Do not preserve a manually selected 60 Hz merely because DRR was already disabled; the explicit product policy is **maximize the available fixed refresh rate** while VRR Fix is enabled.

| Initial state | Stage A: Windows DRR | Stage B: fixed refresh |
| --- | --- | --- |
| DRR ON, currently 60 Hz virtual | Turn BOOST off | Select/apply maximum compatible fixed Hz |
| DRR OFF, currently below max | `AlreadyOff`; no DRR write | **Still select/apply maximum** |
| DRR OFF, already at max | No DRR write | `AlreadyMax`; no mode write |
| DRR ON, no valid higher mode | Attempt DRR OFF | `NoCompatibleMode` or `AlreadyMax`; no forced mode |
| DRR query/validate/apply fails | Log separate DRR status | Run independent rate stage **only if the active target/topology can still be freshly and safely verified**; otherwise log rate-stage skip |
| VRR Fix toggle OFF | No DRR write | **No refresh-rate write** |

A Stage B failure must not rewrite Stage A’s success as failure; conversely Stage A failure must not silently be reported as complete success. Keep **two result fields and one final per-display verification summary**.

**Do not gate maximum fixed-Hz adjustment on `DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE`.** Do not gate either Windows stage on Claw/LG WMI names, IGCL availability, or `IntelVrrRunStatus`.

## 1. Source-level finding in the addon branch

Read-only reviewed source: `integration/steamaddon`, following addon PR #276.

1. `src/ClawHUD/Tweaks/WindowsDrr/WindowsDrrDisable.cpp`:
   - `kReadFlags` = `QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE | QDC_VIRTUAL_REFRESH_RATE_AWARE`.
   - Stage currently copies `QueryDisplayConfig` paths/modes, clears **only** `DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE`, validates with `SDC_VALIDATE`, applies with `SDC_APPLY | SDC_SAVE_TO_DATABASE`, then re-queries.
   - `changedPathCount == 0` causes early `AlreadyOff` return without attempting a fixed-rate change.
   - `VerifyBoostDisabledForOriginallyBoostedPaths` checks that the original target mode and source configuration are unchanged; this is correct for the DRR-only stage and must **not** be weakened merely to permit Stage B.
   - `LogSnapshot` already emits monitor, device path, source/target adapter/ID, BOOST flag, virtual refresh and physical signal refresh to the dedicated VRR log.
2. `src/ClawHUD/Tweaks/TweakStartupCoordinator.cpp`:
   - The enabled startup sequence calls DRR **once**, then runs Intel VRR with existing bounded `2/5/15s` retries.
   - DRR detail and Intel per-panel detail now go to `VrrDiagnosticLogger`.
   - There is not yet a separate fixed refresh-rate stage; DRR status is only a one-line string in the final summary.
3. `src/ClawHUD/Tweaks/VrrDiagnosticLogger.cpp`:
   - File: `%LOCALAPPDATA%\ClawHUD\logs\tweaks-vrr.log`.
   - Run ID, append, UTF-8, bounded rotation, best-effort errors implemented in PR #276.
4. `tests/WindowsDrrDisableTests.cpp` currently assert clearing only BOOST and preserving target signal information, and `AlreadyOff` makes zero `SetDisplayConfig` calls. **Keep these assertions for Stage A**. Add separate tests for Stage B, not by making DRR tests require an unrelated mode change.

**Problem demonstrated by the user's hardware report:** after DRR OFF the selected fixed mode remains 60 Hz. The code **never selects a maximum compatible fixed refresh mode**. This is a missing operation, not proof that `SetDisplayConfig` ignores the BOOST flag. An actual device log is required to know if 60 Hz was the former virtual rate, selected source mode, or a driver-selected fallback.

## 2. Microsoft Learn documentation reviewed

Official references (URLs preserved for implementers):

1. `DISPLAYCONFIG_PATH_INFO` — https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_path_info
   - `DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE` can be set/cleared by `SetDisplayConfig` on Windows 11.
   - Virtual refresh is `DISPLAYCONFIG_PATH_TARGET_INFO.refreshRate`; physical refresh is determined by `DISPLAYCONFIG_TARGET_MODE.targetVideoSignalInfo`.
   - **The documentation does not promise that clearing BOOST selects the monitor’s maximum fixed Hz.**
2. `QueryDisplayConfig` — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig
   - Returns active source/target paths, and virtual refresh aware flags exist on Windows 11.
   - Mode indices must be interpreted with the returned mode array and its virtual-mode-aware unions.
3. `SetDisplayConfig` — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setdisplayconfig
   - `SDC_VALIDATE` validates, `SDC_APPLY` changes configuration, `SDC_SAVE_TO_DATABASE` persists mode/topology.
   - Existing Stage A uses this API; do not invent a new flag that automatically maximizes fixed Hz.
4. `DisplayConfigGetDeviceInfo` — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-displayconfiggetdeviceinfo
   - `DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME` maps a Windows display source to its GDI name, e.g. `\\.\DISPLAY1`; target IDs and source IDs identify paths.
5. `EnumDisplaySettingsExW` — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsexw
   - `ENUM_CURRENT_SETTINGS` obtains the current mode; mode indices starting at 0 enumerate supported modes.
   - Without `EDS_RAWMODE` the returned modes are compatible with the current monitor. Without `EDS_ROTATEDMODE` enumeration normally follows the current orientation.
   - `DEVMODEW.dmDisplayFrequency` is an **integer Hz** value; zero/one means driver default and must not be treated as a maximum.
6. `ChangeDisplaySettingsExW` — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changedisplaysettingsexw
   - `CDS_TEST` tests a proposed mode without applying it; `CDS_UPDATEREGISTRY` applies a mode and persists it if possible.
   - `DISP_CHANGE_SUCCESSFUL` is the only accepted success; `DISP_CHANGE_RESTART` / `BADMODE` / `BADPARAM` / `NOTUPDATED` etc. are distinct outcomes.
   - Microsoft's **multi-display example** stages each target using `CDS_UPDATEREGISTRY | CDS_NORESET` and then commits once using `ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr)`.
   - `WM_DISPLAYCHANGE` may be sent during dynamic mode changes.
7. `DISPLAYCONFIG_DEVICE_INFO_TYPE` — https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ne-wingdi-displayconfig_device_info_type
   - Query-only advanced color/HDR state is available where supported. Modifying advanced color/HDR is *out of scope*.

**Evidence boundaries:** These documents establish APIs and semantics, not that every real GPU/monitor combination can change to the largest advertised mode without another Windows setting changing. Hardware verification remains the user’s next step.

## 3. Intended architecture: two independent Windows stages, one VRR diagnostic run

```text
VRR Fix enabled at app startup
  |
  +-- Stage A: DisableWindowsDrrForActivePaths()   [existing, once]
  |       - DRR BOOST ON -> attempt OFF
  |       - already OFF -> AlreadyOff, no SDC write
  |       - errors -> log, do not swallow outcome
  |
  +-- Stage B: MaximizeFixedRefreshRateForActiveDisplays() [NEW, once]
  |       - enumerate fresh active sources/targets AFTER Stage A
  |       - independent of Stage A == AlreadyOff/Applied
  |       - current resolution/orientation/compatible modes
  |       - validate -> commit -> read back
  |       - error/unsafe/ambiguous -> skip that target safely
  |
  +-- Stage C: Intel VRR per-panel IGCL fix  [existing]
          - Claw 48-120 / LG 48-180 exact mapping
          - existing 2/5/15s Unavailable retry contract
```

Stage B is not part of `DisableWindowsDrrForActivePaths`: do not blend its mode-changing API into the existing BOOST-only function or change its `AlreadyOff` meaning. Avoid repeating Stage B on each IGCL retry. Keep all Windows configuration writes on the existing tweak startup worker, not the HUD frame/render thread.

### Proposed small component boundaries

- New `src/ClawHUD/Tweaks/WindowsDrr/WindowsMaxRefreshRate.{h,cpp}` (filename may vary), defining:
  - `MaximizeFixedRefreshRateForActiveDisplays()` production entry.
  - `IWindowsRefreshRateApi` injected fake/test adapter for active path discovery, `EnumDisplaySettingsExW`, `ChangeDisplaySettingsExW`, and applicable readbacks. Keep separate from existing `IWindowsDrrDisplayConfigApi`.
  - Explicit return type with total active/eligible/already-max/applied/skipped/failed counts and target-specific diagnostic entries. Do not encode Stage B failures as `DrrDisableStatus`.
- Minimal changes in `TweakStartupCoordinator` to call both stage callbacks separately (each guarded so a Stage A exception does not prevent a safe Stage B attempt), retaining Intel retry/Stop behavior.
- Reuse `VrrDiagnosticLogger` as the sole detailed sink. No new logfile, general logger rewrite, new UI, IPC schema change or telemetry.
- Keep production HUD presentation path completely untouched.

## 4. Stage B: target enumeration, “maximum” policy and safe mode application

### 4.1 Enumerate only real, currently active desktop sources

1. Fresh `QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE | QDC_VIRTUAL_REFRESH_RATE_AWARE)` after DRR attempt. Use existing bounded buffer retry conventions. A path must be active and have a valid source/target.
2. Use each `DISPLAYCONFIG_PATH_INFO`’s **source adapter LUID/source ID** with `DISPLAYCONFIG_SOURCE_DEVICE_NAME` (`DisplayConfigGetDeviceInfo`) to get the nonempty `viewGdiDeviceName`; retain target LUID/target ID and the target device path for identity, logging and cross-checks.
3. Validate source identity using `EnumDisplayDevicesW` when appropriate; avoid passing NULL to mode APIs, which would silently target the default/primary display.
4. Deduplicate by source adapter LUID + source ID (and normalized GDI name); do not issue multiple conflicting changes for the same GDI display source.
5. **Clone/mirrored topology:** a source shared by two distinct targets does not imply both targets support the same per-target maximum. Do not pick the max of only one target. Unless a common max can be proven safe for every cloned target, **skip that cloned source and log `ClonedSourceSkipped`**. Do not attempt independent rates on one shared source.
6. An active target with missing/ambiguous name, incomplete display enumeration, changed target identity, remote/virtual device, or impossible mode readback must be skipped rather than accidentally changing the primary monitor.
7. If no active targets can be safely resolved, Stage B returns its own `Unavailable`/`Skipped` and **Stage C Intel Fix still runs**.

### 4.2 Define maximum safely

“Maximum refresh” means:

> Highest **currently enumerated, monitor-compatible, fixed** refresh rate at the **display’s current resolution, current orientation and current desktop color depth**, subject to Windows `CDS_TEST` validation.

Required steps for each unique active GDI source:

1. `EnumDisplaySettingsExW(deviceName, ENUM_CURRENT_SETTINGS, &current, 0)`; initialize `dmSize`, verify `DM_PELSWIDTH` / `DM_PELSHEIGHT` / `DM_DISPLAYFREQUENCY` and relevant returned fields.
2. Enumerate modes with `EnumDisplaySettingsExW(deviceName, i, &candidate, 0)` for indices 0..N (no `EDS_RAWMODE`, no `EDS_ROTATEDMODE`).
3. Filter to `dmPelsWidth == current.dmPelsWidth`, `dmPelsHeight == current.dmPelsHeight`, matching `dmDisplayOrientation` (where present), same `dmBitsPerPel` and compatible/interlaced/scan flags as represented by `DEVMODE`. Keep current `dmPosition` and other layout fields unchanged. Only use modes actually enumerated for **this** source.
4. Ignore frequency 0/1 and malformed modes. Choose highest `dmDisplayFrequency`. If several candidates tie, prefer a current-mode-compatible candidate. **Do not** hardcode “LG 180 / Claw 120 / AlphaScan 165” or choose a maximum across *all resolutions*.
5. Validate selected candidate with `ChangeDisplaySettingsExW(deviceName, &candidate, nullptr, CDS_TEST, nullptr)`. If rejected, optionally try next-highest compatible candidate, bounded by the finite enumerated list; no guessed Hz values, no unsafe-mode flags.
6. If current fixed mode already reports the highest mode (or higher), do **no ChangeDisplaySettingsEx apply call**, return `AlreadyMax`. **Do this even when Stage A returned `AlreadyOff`.**
7. For a distinct selected mode, build the requested `DEVMODEW` from an enumerated supported mode but preserve all current fields not intended to change (at minimum dimensions/orientation/desktop position/bit depth); include the appropriate `DM_DISPLAYFREQUENCY` bit and necessary current-mode fields. Do not set `DM_POSITION`, `DM_PELSWIDTH` or `DM_PELSHEIGHT` to new values by accident.
8. Freshly check the active source/target identity and current desktop configuration immediately before the write. If topology, resolution, orientation, or candidate validity changed, re-enumerate once or fail closed; never apply to a stale GDI source name.

**Precision note:** `DEVMODE.dmDisplayFrequency` is integer Hz, while `QueryDisplayConfig` exposes rational refresh (e.g., 120000/1001). Store both exact reported values in diagnostics and use a clearly documented narrow tolerance for verification rather than insisting that 119.88 numerically equals 120. Do not invent fractional frequency values for `DEVMODE`.

### 4.3 Applying the mode (single and multiple monitors)

- **One eligible unique source:** `CDS_TEST`, then `ChangeDisplaySettingsExW(deviceName, &mode, nullptr, CDS_UPDATEREGISTRY, nullptr)`. Require `DISP_CHANGE_SUCCESSFUL`, then re-read.
- **Two or more independent sources:** use the documented batch pattern where appropriate: `CDS_UPDATEREGISTRY | CDS_NORESET` for each tested source; commit once with `ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr)`. Treat it as one batch and verify **each source** afterward.
- If batch staging fails partway through, or final apply fails, **do not leave knowingly staged per-user registry settings pointing to uncommitted modes**. Record originals beforehand, perform a bounded best-effort staged restoration to original modes and commit/verify restoration, and log the outcome; avoid custom persistent recovery machinery.
- Distinguish `DISP_CHANGE_RESTART` (not applied now), `BADMODE`, `FAILED`, `BADFLAGS` and registry-write failures. Do not claim success merely because `CDS_TEST` or a staging call succeeded.
- Do not force a mode switch on an `AlreadyMax` source. Do not use `CDS_RESET`/`CDS_FULLSCREEN`/`CDS_ENABLE_UNSAFE_MODES` or whole-topology `SDC_TOPOLOGY_*` changes to “make it work”.
- A screen can blink briefly on an intentional Windows mode change. No window-style/renderer/presentation contract may be altered to hide this.

### 4.4 Post-apply verification and user-visible stability

After Stage B, read fresh `EnumDisplaySettingsExW(ENUM_CURRENT_SETTINGS)` and `QueryDisplayConfig`:

- Requested fixed refresh vs actual current mode, plus physical/virtual rational values if available. Verify within a documented precision tolerance.
- Confirm the same active source/target mapping, monitor attachment, resolution, orientation, desktop position, and extend/clone topology.
- Confirm `DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE` is **still OFF** for every target successfully handled by Stage A. If a mode switch re-enables DRR, log it as a separate failed final DRR invariant, not `Applied`; do not silently loop or repeatedly toggle the display.
- Where supported, snapshot `DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO` / HDR enabled state before/after, and log any drift. This feature must not intentionally toggle HDR, advanced color, scaling, VRR setting, or output color parameters. Since `DEVMODE` does not represent every HDR/color parameter, a successful `CDS_TEST` is **not** proof that all such attributes are preserved.
- If a write unexpectedly changes resolution/orientation/layout, do a bounded best-effort restoration from the saved original mode and log if restoration cannot be verified. **Do not report fully successful Apply when display properties have drifted.**
- A monitor mode reset can trigger `WM_DISPLAYCHANGE`/device-change events. Existing HUD production renderer and its independent-flip path must be left untouched; normal Windows OS transitions may occur without changing the HUD contract.

### 4.5 Failure separation / idempotence / no forced downgrade

- Stage B runs **every VRR-Fix-enabled startup**. When DRR is already OFF at 60 Hz and the selected compatible max is 120/165/180 Hz, Stage B must still attempt that change.
- Stage B runs once per application startup, **not** per IGCL retry, per game launch, per frame, on every monitor notification, or in a daemon loop.
- If target max cannot be verified, log `NoCompatibleMode` / `SkippedUnsafeTopology` / `ValidateFailed` / `ApplyFailed` / `VerifyFailed` with Win32/display `DISP_CHANGE_*` codes; preserve existing working settings where possible.
- Never lower an already-higher fixed rate based on an incomplete mode enumeration. Do not change a disconnected/stale panel, unsupported remote/virtual display, or cloned source without a verified common mode.
- Do not change original DRR Stage A's path-level bit clearing, supplied DisplayConfig flags, existing SDC verification, or current `DrrDisableStatus` result semantics.
- Keep toggling VRR Fix OFF **non-restorative** (do not restore DRR or previous manually selected fixed Hz). This is consistent with the existing one-way startup tweak.

## 5. VRR-only diagnostic contract (`tweaks-vrr.log`)

Same run ID; Stage A, Stage B, then IGCL Stage C. No detailed VRR records in `clawhud.log`.

```text
[VRR] RunStart id=... enabled=1 ...
[DRR] display=LG ULTRAGEAR boostBefore=1 virtual=60/1 physical=180/1
[DRR] validateWin32=0 applyWin32=0 verify=BoostOff result=Applied

[RefreshRate] source=\\.\DISPLAY1 targetAdapter=... targetId=...
[RefreshRate] modeBefore=1920x1080 60Hz orientation=... bpp=...
[RefreshRate] candidates=60,120,144,180 compatibleMax=180Hz
[RefreshRate] test=DISP_CHANGE_SUCCESSFUL
[RefreshRate] apply=DISP_CHANGE_SUCCESSFUL
[RefreshRate] modeAfter=1920x1080 180Hz
[RefreshRate] resolutionPreserved=1 topologyPreserved=1 boostStillOff=1
[RefreshRate] result=Applied

[IGCL] ... per-panel discovery/mapping/capability/profile/result ...
[VRR] RunEnd DRR=Applied Refresh=Applied Intel=... ...
```

**Also test this mandatory example**:

```text
[DRR] result=AlreadyOff boostBefore=0
[RefreshRate] modeBefore=1920x1080 60Hz compatibleMax=180Hz
[RefreshRate] result=Applied modeAfter=1920x1080 180Hz
```

Logging must include per-device candidate counts, skipped mode reasons, device/GDI name, source/target LUID+ID, DRR/result status, test/stage/commit codes, final frequency, failure stage, and rollback outcome (when needed). Never combine a failure of one operation into a misleading success of the other.

No change to `tweaks-intel-vrr-result.ini` or its IPC, Settings UI and status strings.

## 6. Required test matrix

### Stage A DRR regression (preserve current tests)

1. One boosted path: supplied modes untouched, only BOOST bit cleared, validate+apply+readback.
2. DRR already OFF: Stage A makes zero `SetDisplayConfig` calls.
3. Multi path: only originally boosted active paths altered.
4. DisplayConfig failure, incomplete query, topology drift, virtual refresh readback changes and mode preservation checks continue passing.
5. Existing Windows DRR unit fixture and diagnostic log assertions unchanged except any *necessary* adapter mocks.

### Stage B new unit tests (fakes; do not change real system displays during automated tests)

1. **Already DRR OFF + current 60 + compatible max 120/165/180:** one max-mode set, no DRR set, final rate equals max.
2. DRR ON + 60 virtual + max 120/165/180: DRR stage once + refresh stage once, with full independent results.
3. DRR failed/Unsupported/Unavailable but fresh safe display target exists: refresh stage still runs; both results recorded separately.
4. VRR Fix OFF: neither stage writes.
5. Current rate == maximum: no change API called; `AlreadyMax`.
6. Wrong resolution mode has larger Hz: ignored. Different orientation, bit depth or unsupported scan mode: ignored.
7. Same monitor/driver lists multiple modes: choose highest **compatible/testable**, skip invalid/default=0/1 and rejected candidates.
8. Empty/incomplete enumeration, invalid GDI source, disconnected target, swapped target LUID/ID mid-run: no incorrect display write.
9. Two independent extended monitors: correct per-device candidates with no cross-monitor writes; staged registry entries and final batch apply verified.
10. Clone source shared between targets: no independent max application unless a verified common mode exists.
11. `CDS_TEST` failure, staging failure, `DISP_CHANGE_RESTART`, final apply failure, readback mismatch: correct status and bounded restoration where applicable.
12. Already higher than highest reliably enumerated rate: no forced downgrade.
13. After refresh switch, DRR BOOST unexpectedly re-enabled: not a clean success, logged.
14. Source resolution/orientation/position/HDR state drift detected and reported, with best-effort restoration where safe.
15. New diagnostics use the **same VRR run ID** as DRR and IGCL, and `clawhud.log` remains free of VRR detail.
16. Stage B runs once on startup while Intel 2/5/15s retries; exceptions in A/B do not terminate tray or block Stage C.
17. Existing HUD presentation-contract regression tests unchanged/passing.

### User-owned hardware verification (after addon implementation, before main)

Record `tweaks-vrr.log` and Windows Settings readings for:

- Claw internal alone (e.g. max 120), external LG alone (e.g. max 180), both active in extend mode.
- DRR ON → OFF + max fixed; DRR already OFF at manually selected 60 → max fixed.
- Already-max restart → no unnecessary mode switch.
- AlphaScan external monitor where its own connection supports maximum 165 Hz (never hardcode).
- Available HDR ON, different resolutions, and multi-monitor behavior as practical.
- Confirm after app restart both DRR OFF and fixed max persist and IGCL per-panel VRR fix still runs.

**Do not mark hardware testing complete based only on CTest.** Hardware verification belongs to the user; code PR review judges reachable code defects and CI, not unavailable hardware results.

## 7. Implementation / release order and cherry-pick plan

### Phase A — addon branch

1. Create a feature branch **from current `integration/steamaddon`**, **after PR #276 squash merge**, and implement **only this Max Fixed Refresh stage plus minimal coordinator/test/log changes**.
2. One focused addon PR into `integration/steamaddon`. Preserve existing DRR and per-panel IGCL paths. Suggested implementation commits:
   - **A1** `WindowsMaxRefreshRate` discovery/policy/isolated tests.
   - **A2** coordinator integration, VRR log aggregation, batch apply/readback/rollback tests.
3. Windows Release build, full CTest, HUD regression. Review and **squash-merge** if code passes; user then performs actual hardware validation on the addon.
4. If hardware shows issues, fix on addon and validate there before any Main port.

### Phase B — `main` must receive BOTH the earlier series and this feature

`main` already has DRR OFF from PR #275, but **does not yet have the addon PR #276 multi-panel/log implementation**. Follow this exact dependency order on a fresh feature branch based on up-to-date `main`:

**B1:** cherry-pick `8d788977f39ee2002e8282d5274a7e4989ee74b6` (addon PR #276 squash commit: Claw/LG exact-target multi-panel fixing + `tweaks-vrr.log`). **B2:** cherry-pick the *later* verified addon maximum-refresh implementation squash commit. Use `git cherry-pick -x` to preserve provenance; adapt conflicts only where `main` has its own Diag IGCL/Windows probe paths or CMake/test definitions.

- **Never merge the entire `integration/steamaddon` branch into `main`**, or replace Main's Diag probe source with addon probes wholesale.
- Preserve Main-only functionality and CMake/test configurations. The same supported-panel policy, VRR file sink, DRR state and RefreshRate stage semantics must result.
- Use a **separate Main PR** (or two focused dependent Main PRs if conflict size makes review safer), full Main CI and code review. Squash-merge after approval.
- The doc-only commit is not a runtime dependency. Do not blindly cherry-pick unrelated addon feature commits, branch merge bubbles, or changed HUD presentation logic.
- If a clean `git cherry-pick -x` of an addon squash commit is infeasible due to Main-specific directory/API differences, perform a **traceable manual equivalent adaptation** in Main and record which addon commit it corresponds to. The required *behavior* is identical; avoid introducing an alternate implementation contract.

## 8. VRR-critical production presentation contract — must not change

Never edit, replace, weaken or work around:

- `windowExStyle`; `WS_EX_TRANSPARENT`; `WS_EX_NOACTIVATE`; `WS_EX_TOPMOST`; existing `WS_EX_LAYERED`
- `WM_NCHITTEST -> HTTRANSPARENT`; `WM_MOUSEACTIVATE -> MA_NOACTIVATE`
- `ProductionHudPresentationContract()`; independent flip requirement
- existing Presentation API / DirectComposition production path
- premultiplied-alpha rendering; background-only `Background Opacity` contract

The Windows **OS display mode** may intentionally change once at startup. Do not “fix” its effects by modifying any HUD presentation contract, renderer path, window flags, hit testing, or opacity. Preserve their existing tests/assertions.

## 9. Explicit exclusions

- No CRU/EDID editing, monitor vendor-specific max-Hz hardcoding, unsupported raw/unsafe modes.
- No extra user-facing Settings switch for Stage B; it runs when existing VRR Fix is enabled.
- No DRR enable/restore on VRR Fix OFF; no previous-Hz restoration on VRR Fix OFF.
- No new periodic worker, timer, per-game/focus-triggered display mode changes.
- No modification to Intel IGCL 48–120 / 48–180 policy, user profile protection or rollback.
- No automatic HDR toggling, resolution/scaling/orientation change or topology selection.
- No HUD UI/opacity/presentation changes.
- Do not block for speculative scheduler races or defensive architecture without a realistic failure path.

---

**Acceptance:** (1) DRR OFF proceeds as before; (2) maximum compatible fixed refresh is applied even when DRR was **already OFF**; (3) correct per-monitor source targeting, current resolution/orientation/color depth and multi-monitor topology are preserved; (4) both stages independently verified and logged in `tweaks-vrr.log`; (5) after addon user hardware validation, addon PR #276 **and** this implementation reach Main in dependency order by traceable cherry-pick/adaptation.
