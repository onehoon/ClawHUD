# Work Order — Claw 7 AI+ TMA2027 Native VRR + Three-Panel Independent Startup Policy

**Status:** implementation work order; production implementation not yet performed  
**Date:** 2026-10-30 (Asia/Seoul)  
**Repository:** onehoon/ClawHUD  
**Primary target:** main @ d1fc896e46945fc470690918d7eaf40b0592f69c  
**Follow-up target:** integration/steamaddon @ ac82f14b28962cea3aeb826d6432026bc29d5428 (cherry-pick/backport)  
**Delivery:** ONE implementation PR against main. No 500-LOC limit. Split commits only when it improves implementation/review/backport.  
**Deployment:** immediate production support, followed by voluntary user field verification. No Claw 7 AI+ hardware is available to the maintainer.  
**Scope:** preserve OEM native range; do not implement 30 Hz floor, overclocking, EDID override, or LFC modification.

---

## 1. Approved decisions

1. Implement and ship MSI Claw 7 AI+ panel TMA2027 as a normal production target in the existing Intel VRR Range Fix startup pipeline. Do not gate it behind a beta toggle, manual diagnostic or a later PR.
2. On every enabled app startup, discover **all** monitor identities and active Windows display paths. Classify each active, exact-matched supported panel independently and run its own policy/readback. A failure or unsupported state on one output must not suppress a different eligible output.
3. The three supported **panel families** are:
   - MSI Claw 8 AI+ / 8 EX AI+ — CSW0801 / PN8007QB1-2, 1920x1200, native VRR 48–120 Hz.
   - MSI Claw A1M / Claw 7 AI+ — TMA2027 / TL070FVXS02-0, 1920x1080, native VRR 48–120 Hz. Both share the pinned panel identity; do not claim model-exclusive detection from EDID alone.
   - LG 27GS65F — GSM5C8C, recognized actual names LG ULTRAGEAR or LG 27GS65F, existing production 48–180 Hz policy unchanged.
4. Keep the existing single user-facing Intel VRR Range Fix setting and startup coordinator. Do not add per-monitor toggles, broad generic display support, a watcher, new service or persistent driver-setting task.
5. Main-first implementation, integration/steamaddon backport second. Keep changes easily transplantable, but do not copy main's distinct probe implementation onto the integration branch.
6. One implementation PR may have multiple ordered commits; LOC count is not a blocking constraint. Do not require additional PRs merely for code size.

## 2. Evidence versus design decisions

### Verified in current main

- AffectedPanelDetector.cpp ClassifySupportedPanel recognizes only CSW0801 and GSM5C8C. TMA2027 is absent.
- AffectedPanelDetector.cpp ResolvePanelWindowsTarget requires normalized WMI PnP instance == normalized active Windows target path, with model-specific DISPLAY\CSW0801 or DISPLAY\GSM5C8C prefix. This exact mapping must be extended to DISPLAY\TMA2027, not weakened.
- IntelVrrRangeTweak.cpp Run already enumerates all WMI monitor identities, deduplicates model/instance, maps each supported panel to an active target, deduplicates adapter-LUID/target-ID pairs, and iterates work targets. Keep and extend this architecture, rather than creating a 3-way single-panel if/else path.
- IntelVrrRangeTweak.cpp currently chooses ClawInternal (48–120) or Lg27gs65f (48–180), applies EXCELLENT conditionally, revalidates target identity immediately before and after write, and verifies IGCL readback.
- Current main's IGCL probe is src/ClawHUD.Diag/DiagIntelVrrStateProbe.{h,cpp}; main's IntelVrrRangeTweak.h aliases its types. Setter permission is checked again in IsIntelVrrProfileChangeAllowed. An upstream TMA policy change must affect BOTH the evaluator and the actual low-level setter guard.
- TweakStartupCoordinator.cpp startup stage order: Windows DRR control, maximum fixed refresh selection, Intel VRR range fix. The first two are active-path/global Windows operations; the Intel profile stage is the per-target policy loop.
- TweakStartupCoordinator.h RunRetrySequence attempts up to 4 times, with bounded 2/5/15-second delays only when the aggregate result is Unavailable.
- IntelVrrResultStore persists a single aggregate result to %LOCALAPPDATA%\ClawHUD\tweaks-intel-vrr-result.ini; VrrDiagnosticLogger writes per-attempt information separately. Avoid breaking the persisted result or IPC representation.
- Unit-test entry points: tests/IntelVrrRangeTweakTests.cpp, tests/DiagIntelVrrStateProbeTests.cpp, tests/TweakStartupCoordinatorTests.cpp, cmake/ClawHUDTests.cmake.

### Verified in integration/steamaddon

- Both branches currently have the same AffectedPanelDetector.cpp blob SHA c9f9092c4d027eba2fc43a46001185ae5a15b3be, but its header differs because branch-local display-probe includes/types differ.
- main and integration/steamaddon are diverged (review-time comparison: integration ahead by 38, behind by 30).
- integration/steamaddon has src/ClawHUD/Tweaks/IntelVrr/Probe/IntelVrrTargetStateProbe.{h,cpp} and Probe/IntelDisplayPathProbe.{h,cpp}; main has src/ClawHUD.Diag/DiagIntelVrrStateProbe.{h,cpp} and DisplayPathProbe.
- Both branches have a very similar IntelVrrRangeTweak loop and test scenarios, but the implementation files have different blobs, types, include paths and build configuration. Assume cherry-pick conflicts in these integration files; do not promise a zero-conflict whole-PR cherry-pick.
- The actual goal is a portable **policy/identity implementation** and independent per-target behavior, not making main and integration/steamaddon internally identical.

### External verified input: Zendo ClawLab 3.0.0

- Its support matrix pins physical TMA2027 EDID SHA-256:
  3518AB4456669D12A7B8D254F63005EAE143C784DCE02EC56C3753C41A664CA1
- Canonical TMA2027 physical EDID is 128 bytes. Some Windows paths append an additional 128-byte, all-zero padding block; the extension count must be zero and any non-zero tail must fail validation. Do not treat arbitrary 256-byte EDID as valid.
- Physical EDID declares 48–120 Hz. On verified TMA2027 drivers, Intel ControlLib monitor-capability query may return 24–120 Hz (a **half-floor telemetry anomaly**). The separately queried active Arc Sync profile and read-only direct Intel evidence must be considered independently. Do not translate a reported 24 Hz into a real 24 Hz native VRR floor.
- Zendo documents an exact TMA2027 OEM driver state with profile ID 7 CUSTOM 30–120, 8333/8333 us, where RECOMMENDED can report success without changing the profile and EXCELLENT is rejected with 0x40000017. Zendo itself forbids installing the official 48–120 mode in that state. Preserving this OEM state is NOT a claim that our 48–120 fix has applied.
- Zendo's 48–120 stable mode leaves the physical EDID unchanged. Its full utility also changes LFC-related driver flags and installs persistence machinery; NONE of those aspects are authorized for ClawHUD here.

Source references:
- https://github.com/Zendo-GIT/Intel-Handheld-and-ARC-GPU-laboratory/blob/master/utilities/msi-claw-8-intel-vrr-range-fix/docs/COMPATIBILITY.md
- https://github.com/Zendo-GIT/Intel-Handheld-and-ARC-GPU-laboratory/blob/master/utilities/msi-claw-8-intel-vrr-range-fix/docs/TECHNICAL_DETAILS.md
- https://github.com/Zendo-GIT/Intel-Handheld-and-ARC-GPU-laboratory/blob/master/utilities/msi-claw-8-intel-vrr-range-fix/ArcSync-Range-Policy.ps1
- https://github.com/Zendo-GIT/Intel-Handheld-and-ARC-GPU-laboratory/blob/master/utilities/msi-claw-8-intel-vrr-range-fix/MSI-Claw-VRR-Fix.ps1

**Scope distinction:** Zendo's field evidence authorizes using the verified TMA2027 telemetry interpretation and OEM fail-closed policy in production. It does NOT prove that every Claw 7 AI+ driver permits EXCELLENT. Real Claw 7 user reports after release are an operational verification step, not an implementation gate.

## 3. Architecture — discover all, match exactly, act independently

Keep one startup worker. Run a discovery pass and a per-target pass:

    Startup(enabled)
      -> existing DRR and Windows maximum-refresh stages (unchanged)
      -> enumerate WmiMonitorID candidates (all)
      -> enumerate active Windows target paths (all)
      -> for each candidate:
           exact panel-family identity
           normalize WMI PnP instance and Windows monitor path
           exact one-to-one mapping; no name-only fallback
           deduplicate by (adapter LUID, target ID)
           append independent TargetWork(panel, family, target)
      -> for each TargetWork:
           query fresh exact IGCL output
           read policy-specific additional evidence
           DecideForPanel(family, state, evidence)
           if already correct / preserved / unavailable: record, continue
           if eligible:
              revalidate exact same Windows target and driver state
              SetProfileForExactTarget(EXCELLENT) for that output only
              re-query and verify family-specific native range
              record result, continue
      -> aggregate all outcomes; persist existing summary and detailed log

Requirements:

- Do not require all three families to exist simultaneously. Discover all; process the active subset (0, 1, 2, 3 or more actual instances).
- Allow two or more physical monitors of the SAME supported model if each has a distinct PnP monitor-instance path and unique adapter LUID + target ID. The number of families is three; the number of target instances is not limited to three.
- A disabled internal panel stays non-writable even if stale WMI reports it. Existing WMI Active=false is diagnostic metadata, **not** the sole active-target gate: actual matching active QueryDisplayConfig target is authoritative.
- Never write by first matching monitor name, GDI DISPLAY1, one-Intel-output assumption, capability range alone or Windows primary-display status.
- A monitor connected to a non-Intel GPU or missing from IGCL becomes a per-output Unavailable/Unsupported/Skipped status with **zero writes**, not a global failure.
- Exact output identity must be checked again just before the setter and after it. Refresh-rate/topology changes between those checks must fail closed.
- No second thread per display. The existing bounded serial per-output loop is adequate.
- No global "last queried output" reuse across display writes; every set operation must consume the exact output handle generated by its immediately preceding query.

## 4. Identity and type design (backport-oriented)

Modify src/ClawHUD/Tweaks/IntelVrr/AffectedPanelDetector.{h,cpp}.

Prefer preserving the CURRENT ClawInternal enum value and meaning (CSW0801) to avoid broad rename churn. Append Claw7Internal as a third distinct kind; maintain existing names for LG and Claw8. Example:

    enum class SupportedPanelKind {
        ClawInternal,   // existing CSW0801 / 8-inch
        Lg27gs65f,     // existing GSM5C8C
        Claw7Internal  // new TMA2027 / 7-inch
    };

Update IsAffectedPanel to return true for either Claw internal kind. IsLg27gs65fPanel remains LG-only.

ClassifySupportedPanel should enforce all three TMA2027 fields, case-insensitively:

    manufacturer == "TMA"
    productCode  == "2027"
    panelName    == "TL070FVXS02-0"

ResolvePanelWindowsTarget should select an exact expected normalized prefix, not the existing two-way ternary:

    ClawInternal  -> display\csw0801\
    Claw7Internal -> display\tma2027\
    Lg27gs65f    -> display\gsm5c8c\

The full normalized PnP instance identity must still match the active Windows target, and duplicate matching targets must remain Ambiguous. Keep LG's existing IsLg27gs65fWindowsIdentity restrictions.

Keep separate policy labels in logging:
- Claw8-CSW0801
- Claw7-TMA2027
- LG27GS65F

Do not infer MSI device model from the panel. CSW0801 applies to the two Claw 8 families; TMA2027 also appears in Claw A1M. This task is panel-specific VRR and should support an exact, verified TMA2027 panel independent of marketing name.

## 5. Production TMA2027 evidence — read-only and exact

Introduce a small TMA2027-specific validation component (a portable pure policy and a Windows evidence adapter, not a generic EDID editor). The Windows adapter needs to obtain raw EDID for the exact ACTIVE monitor instance by supported Windows monitor interfaces / registry/WMI, and prove that the identity corresponds to the already exact-mapped target.

Mandatory evidence for the special 24–120 telemetry exception:

1. TMA2027 model identity and exact active monitor instance/Windows target.
2. Canonical physical EDID 128-byte data with expected SHA-256, valid header/checksum and declared zero extensions; optionally accept precisely 128 extra bytes of all-zero padding and canonicalize before hashing.
3. No active foreign/unknown EDID override for that exact monitor instance. Read only; never edit registry. Do not silently mistake the physical registry EDID for a currently loaded custom override.
4. Unique Intel adapter/output mapping, complete IGCL query, supported VRR, independently read ACTIVE Arc Sync profile and native full-range expectation.
5. A read-only direct Intel physical VRR range check where necessary to justify interpreting a 24–120 capability result as 48–120. Match 48–120 against the same target; if the direct evidence is unavailable, the exception is NOT established and no special-case write is permitted.

The TMA evidence object must carry enough information to tie the positive proof to the exact resolved monitor target, not merely a global Boolean flag that could accidentally be applied to a different monitor.

Non-TMA CSW0801 and GSM policy behavior must NOT depend on this new physical-EDID evidence path.

Do NOT:
- add or install EDID_OVERRIDE,
- invoke CRU/reset-all,
- apply TMA exception to all monitors with capability minimum 24,
- infer 48–120 from 24–120 by simple multiplication without the verified target and EDID,
- accept the 15 Hz half-managed 30 Hz case as native 48–120 (30 Hz is out of scope),
- modify Intel LFC flags or any other private Intel driver setting.

Implementation choices for EDID acquisition or D3DKMT read-only proof must be documented in code comments, with testable interfaces. If a required read-only check fails, a clear skip/unavailable result is acceptable; do not expand the unsafe write surface to force a successful status.

## 6. Panel-specific Arc Sync decisions

Preserve current CSW0801 and LG production policies verbatim unless a tested shared helper extraction is truly necessary. Add a distinct Claw7 evaluator and a distinct Claw7 setter policy. Required decision table:

| Panel / prestate | Decision | Setter |
|---|---|---|
| CSW0801 normal | Existing EvaluateClawInternalProfile | Existing |
| GSM5C8C normal | Existing EvaluateLg27gs65fProductionProfile | Existing |
| TMA: native capability 48–120; active STANDARD profile exact 48–120 | AlreadyCorrect | No |
| TMA: verified half-floor capability 24–120; active STANDARD profile exact 48–120 | AlreadyCorrect | No |
| TMA: native capability 48–120; recognized STANDARD profile narrower than native (e.g. RECOMMENDED 60–120) | Applied candidate | EXCELLENT only |
| TMA: verified half-floor capability 24–120; recognized STANDARD profile narrower than native; complete physical EDID + direct range proof | Applied candidate | EXCELLENT only |
| TMA: OEM/custom profile 7 at 30–120, or any other CUSTOM | SkippedUserProfile / explicitly preserved | No |
| TMA: OFF profile 5 | SkippedUserProfile | No |
| TMA: missing/broken/unmapped EDID, unknown physical range, unsupported cap/profile or incomplete IGCL | Unavailable/Unsupported | No |
| TMA: attempted set rejected, e.g. 0x40000017 | ApplyFailed, with actual error code | No fallback setter |
| TMA: API success but readback did not become EXCELLENT and 48–120 | VerificationFailed | Do not claim success |

A "STANDARD" profile here means only existing recognized Intel profile IDs (RECOMMENDED=1, EXCELLENT=2, GOOD=3, COMPATIBLE=4, VESA=6). Verify numeric ranges are finite, plausible and not outside native maximum; reject unknown/malformed state. Keep CUSTOM=7 and OFF=5 strictly non-writable.

Crucially, the existing public helper IsIntelVrrProfileChangeAllowed in main's DiagIntelVrrStateProbe.cpp currently requires capability minimum == 48. It will reject the real TMA half-floor state even if the high-level evaluator permits it. Extend the low-level permission **only for the explicit Claw7 path with bound verified evidence**.

Preferred strategy:
- Extend IntelVrrPanelClass with Claw7Internal, preserve existing two enum values and policies.
- Extract narrow, pure TMA range/profile decision helpers that are independent of whether the calling branch uses DiagIntelVrrStateProbe or IntelVrrTargetStateProbe.
- Pass verified per-target TMA evidence into the actual setter permission boundary when the 24–120 exception is used. Recheck dynamic Intel state just before the write.
- Refrain from adding a blanket relaxed global capability test such as minHz == 48 OR minHz == 24 for all panels.
- If the shared main probe API change would modify an existing manual LG Diag caller, maintain a backward-compatible overload/default for the legacy path and test it.

For a writable candidate, target ID and adapter LUID must still match immediately before SetProfileForExactTarget. Profile success requires a new exact query on that same target showing profile ID EXCELLENT, minimum approximately 48 and maximum approximately 120. A verified ControlLib 24 Hz half-floor can remain in capability telemetry; it is NOT the active desired floor. Check exact EDID/monitor proof before any special-case write, and no unverified post-write state may be reported as success.

For an unexpected post-write mismatch, report clearly. Optional best-effort restore is permissible ONLY if the original state is a known writable standard profile AND the exact target remains proven stable, and only if it is separately verified; do not use a failed 7-AI+ setter as a reason to add retry or normalize CUSTOM. Do not add a new generic rollback framework.

## 7. Multi-monitor completion semantics and bounded retry

Existing multi-output structure is reusable; strengthen result reporting and actual eligibility accounting rather than replacing the coordinator.

- Keep TargetWork as an item per exact active supported panel and one independent PanelResult per attempted/mapped target.
- Preserve all successful sibling results when another target fails. Continue to next work item even after ApplyFailed, VerificationFailed, Unavailable, or SkippedUserProfile.
- Keep WMI candidates without active target out of the write list; distinguish stale NotActive from an active target whose mapping/IGCL query failed.
- Current Summarize(panelResults) aggregates only targets that reached the work list; unavailableMappings/ambiguousMappings are separately counted in summary_ but may be absent from persisted overall result. Ensure an active supported target's mapping or state failure is not lost from the **aggregate decision**. Do not let an absent/stale disconnected WMI monitor incorrectly make every healthy output Unavailable.
- Preserve aggregate status as the current IPC contract; keep full per-target outcomes in normal diagnostics. Only add per-panel structured persisted data if truly needed and without breaking the existing INI schema or IPC.
- Existing RunRetrySequence retries only if the aggregate status is Unavailable. Confirm that one temporarily unavailable **active eligible** output can receive the existing bounded 2/5/15 s retry while already-correct siblings remain idempotent. Do not add unbounded per-monitor retries or an always-on watcher.
- If all supported candidates are inactive/disconnected: no setters and remove stale aggregate last-result data as existing.
- "Both displays independent" means independently evaluated IGCL outputs; it does not mean concurrent calls or a success despite failed verification.

Example mixed topology:

    Claw 7 AI+ internal (TMA2027) : AlreadyCorrect / no setter
    LG 27GS65F external (GSM5C8C): Applied / verified 48–180
    Third unknown external panel : Unsupported / no IGCL setter
    Aggregate                    : per-panel counts logged, no hidden failure

## 8. Preserve existing startup/global Windows controls

Do not reinterpret Windows DRR or maximum-refresh operations as Intel profile policy. Preserve TweakStartupCoordinator's stage order and the current active-Windows-path enumeration.

- Windows DRR step currently operates on active paths globally.
- MaximizeFixedRefreshRateForActiveDisplays currently has its own safety/topology policy and may act on more than the three supported IGCL panels. Do not silently broaden or narrow it in this TMA feature.
- New TMA-specific work happens in IntelVrrRangeTweak's selected exact output branch.
- User toggle disabled -> no new TMA mutation; keep existing setting behavior (settings change takes effect according to existing startup semantics, not instant restore).
- Avoid forcing a 120 Hz Windows mode for all monitors. Existing maximum fixed-refresh stage may correctly choose LG 180 Hz; the TMA Arc Sync fix is independent.
- Do not introduce startup Task Scheduler operations, persistent service or Intel Graphics Software startup interception into ClawHUD.

## 9. File plan (main)

Primary:

1. src/ClawHUD/Tweaks/IntelVrr/AffectedPanelDetector.h/.cpp
   - New distinct TMA panel kind; exact WMI, PnP mapping, family names and tests.
2. src/ClawHUD/Tweaks/IntelVrr/IntelVrrRangeTweak.cpp/.h
   - New Claw7 evaluation branch; policy-scoped evidence; same per-target loop; verification; per-target status/summary.
3. src/ClawHUD.Diag/DiagIntelVrrStateProbe.h/.cpp
   - Main-only low-level TMA setter permission, bound to verified target/evidence. Existing Claw and LG semantics unchanged.
4. A small src/ClawHUD/Tweaks/IntelVrr/Tma2027VrrPolicy.{h,cpp} (suggested)
   - Pure 48–120 / 24–120 interpretation, standard-profile eligibility, OEM CUSTOM preservation, final range validation.
   - Keep it independent from Diag probe implementation and Windows-specific source directories to ease backport.
5. A small TMA physical EDID / direct range **read-only** adapter (exact location at implementer's discretion)
   - Testable injection boundary; handle expected 128-byte EDID and all-zero padding; hash, no override.
6. tests/IntelVrrRangeTweakTests.cpp, tests/DiagIntelVrrStateProbeTests.cpp, tests/TweakStartupCoordinatorTests.cpp and focused TMA adapter/policy tests.
7. cmake/ClawHUDTests.cmake and root CMakeLists.txt, only as needed to register new sources and tests.

Keep production HUD renderer, window creation and graphics presentation sources untouched.

## 10. Implementation PR / commit design

One main-targeted implementation PR. Commit grouping is a convenience, not a LOC requirement:

**Commit A — portable supported-panel identification**
- Extend shared detector and exact Windows path matching.
- Add TMA fixtures, duplicate-instance and mixed-display mapping tests.
- Minimal shared helper additions only.

**Commit B — portable TMA policy + evidence**
- Tma2027VrrPolicy pure decision functions and read-only physical panel evidence.
- 24–120 telemetry handling, OEM CUSTOM refusal/preservation, profile-specific positive/negative tests.
- Keep main's Diag and integration's Probe implementations out of the portable policy source.

**Commit C — main adapter + production wiring**
- Wire TMA-specific guard through main's DiagIntelVrrStateProbe and IntelVrrRangeTweak.
- Per-panel results, existing bounded retries, end-to-end mock tests, build integration.
- Do not rewrite CSW0801 or LG code to a new framework.

**Commit D — optional regression/documentation polish**
- Only when actually useful. Don't split an artificial PR for LOC.

Before each commit, run relevant targeted tests. At final PR review, inspect whether portable commits can be cherry-picked without accepting main's branch-local Probe refactor.

## 11. integration/steamaddon backport contract

Do not start development on integration/steamaddon and do not create a second unrelated feature design. Implement/land against main first; cherry-pick/backport the **same behavior** afterward.

Base branch at review-time: integration/steamaddon @ ac82f14b28962cea3aeb826d6432026bc29d5428. Refresh HEAD before any cherry-pick.

Branch-specific porting map:

| Concern | main source | integration/steamaddon source |
|---|---|---|
| WMI panel/identity | Tweaks/IntelVrr/AffectedPanelDetector | same path, header include differs |
| Windows path probe | ClawHUD.Diag/DisplayPathProbe | Tweaks/IntelVrr/Probe/IntelDisplayPathProbe |
| IGCL state and setter | ClawHUD.Diag/DiagIntelVrrStateProbe | Tweaks/IntelVrr/Probe/IntelVrrTargetStateProbe |
| Intel VRR orchestrator | Tweaks/IntelVrr/IntelVrrRangeTweak | same path with branch-specific type/include wiring |
| Tests | tests/IntelVrrRangeTweakTests + Diag probe tests | tests/IntelVrrRangeTweakTests + branch-local probe tests |
| Build | main CMake source/test organization | integration branch's actual CMake source/test organization |

Sequence:
1. Land main implementation PR (squash only according to final review policy). Capture original feature commit SHA(s); if main squashes, use the resulting main commit as the source and selectively apply compatible changes.
2. Create a dedicated integration backport branch from current integration/steamaddon. Try cherry-picking Commit A and B if they are independent; otherwise use git cherry-pick -n or the equivalent to transfer **only** the feature hunks. Preserve proof and behavior. Resolve expected probe/headers/CMake conflicts using the integration branch's own abstractions.
3. Port Commit C's **policy conditions** to IntelVrrTargetStateProbe instead of importing or linking main's DiagIntelVrrStateProbe. Do not replace the addon branch's IGCL stack, CMake organization, startup behavior or unrelated history to force a clean cherry-pick.
4. Use a small adapter/mapper between portable TMA policy/evidence and each branch's state types. Keep the same panel-ID checks, no-CUSTOM-write guarantee, exact output matching, readback and tests.
5. Run branch-specific unit and build tests. Compare main vs integration outcome matrices for each TMA scenario; report deviations explicitly.
6. Open a follow-up integration-targeted PR only when the main implementation is complete. One main implementation PR is the priority; do not merge unrelated branch changes.

Note: a literal whole-main-PR cherry-pick is NOT expected to be conflict free because these two branches have diverged. The acceptance criterion is **portable commits and a bounded, reviewable cherry-pick/backport**, not byte-identical probe files.

## 12. Mandatory test matrix

Tests must be executable with mocked WMI/Windows paths, IGCL probe states and injected TMA evidence. Do not depend on local Claw 7 AI+ hardware or external network for CI.

### TMA identity and safety
- TMA/2027/TL070FVXS02-0 exact case-insensitive triple -> Claw7Internal.
- Incorrect manufacturer/product/name, misleading TMA-looking friendly name -> unsupported.
- Correct WMI TMA instance and exact normalized active Windows path -> one exact target.
- Wrong suffix, same model on another instance, duplicate target or incomplete enumeration -> no setter.
- A1M/Claw 7 shared TMA panel classification explicit.
- Physical 128-byte SHA match with valid EDID header/checksum -> verified.
- Exactly one appended 128-byte all-zero block + zero declared extensions -> verified only after canonicalization.
- Non-zero padding, altered EDID, mismatch SHA, installed unknown EDID override, wrong monitor binding -> no TMA exception setter.

### TMA Intel range states
- capability 48–120 / standard active EXCELLENT 48–120 -> AlreadyCorrect, zero write.
- capability 48–120 / standard active RECOMMENDED 60–120 -> one EXCELLENT write and readback 48–120.
- capability 24–120 / standard active 48–120 + full TMA evidence -> AlreadyCorrect, zero write.
- capability 24–120 / standard active 60–120 + full EDID/direct evidence -> one EXCELLENT write and verified 48–120.
- capability 24–120 without direct physical range or without exact EDID -> no write.
- capability 24–120 on CSW0801 or LG -> existing refusal behavior.
- OEM CUSTOM ID 7 / 30–120 / 8333 us -> preserved, no EXCELLENT/RECOMMENDED normalization.
- any CUSTOM (even deceptively 48–120), OFF, 15–120 telemetry, malformed active range or unsupported profile -> no write.
- simulated 0x40000017 setter error -> ApplyFailed, no second setter/no success.
- simulated IGCL success with unchanged CUSTOM or wrong readback -> VerificationFailed, no false-positive success.

### Multi-monitor
- TMA alone -> exactly one policy and target.
- CSW0801 alone -> existing behavior unchanged.
- LG alone -> existing behavior unchanged.
- TMA + LG exact active -> each independently processed, native ranges 48–120 vs 48–180.
- CSW0801 + LG -> existing behavior preserved.
- TMA + same-model TMA, different instance/target -> two independent results.
- TMA + another panel on non-Intel adapter -> no cross-GPU write; TMA still processed.
- One active panel fails setter/query, another succeeds -> failure retained in summary/log; sibling not suppressed.
- Stale WMI/disabled panel plus valid active sibling -> stale panel cannot cause IGCL write or block sibling.
- Target changes before setter or after setter -> safe failure, no write to wrong output.
- Bounded retry of one transiently unavailable active panel while healthy sibling remains AlreadyCorrect with no duplicate setter.
- ResultStore legacy aggregate fields and existing Settings IPC result remain readable.

### Existing regressions
- tests/IntelVrrRangeTweakTests.cpp
- tests/DiagIntelVrrStateProbeTests.cpp
- tests/TweakStartupCoordinatorTests.cpp
- existing DisplayPathProbe, Windows DRR, maximum refresh, LG production policy tests
- production HUD click-through/no activation/topmost/transparent hit testing/independent flip/premultiplied-alpha/presentation-contract tests
- main build and integration branch build after backport

## 13. Diagnostics and post-release field reports

Because maintainer lacks the Claw 7 hardware, produce enough per-panel logs for self-contained user bug reports.

Record:
- classification (Claw7-TMA2027, Claw8-CSW0801, LG27GS65F)
- WMI manufacturer/product/panelName and normalized PnP instance
- active Windows target adapter LUID/target ID and exact matching result
- physical EDID verification class: MATCHED / MISMATCH / UNAVAILABLE / OVERRIDE_PRESENT (do not log full raw EDID or unrelated private registry data)
- ControlLib reported capability, active profile ID/range, TMA half-floor classification
- if available, read-only direct Intel physical range
- action (NO_WRITE_ALREADY_CORRECT / PRESERVED_OEM_CUSTOM / SKIPPED_UNSAFE / APPLY_EXCELLENT / APPLY_FAILED / VERIFY_FAILED)
- exact IGCL API HRESULT/return code, if any; BEFORE and AFTER per target
- final per-target status and aggregate counts, timestamp and ClawHUD app version

Do not imply that applying EXCELLENT proves game runtime VRR or Independent Flip; this feature is Intel profile range policy only.

After release, request redacted logs from Claw 7 users and confirm: actual TMA panel identity, IGCL capability (48 or 24), active profile before/after, driver version, setter result, and any reported user-visible regression. Do not delay main PR or production enablement solely because maintainer has no Claw 7 device.

## 14. HUD presentation / VRR safety contract — non-negotiable

No modification, workaround, weakening or replacement of:
- HUD windowExStyle;
- WS_EX_TRANSPARENT, WS_EX_NOACTIVATE, WS_EX_TOPMOST or WS_EX_LAYERED behavior;
- WM_NCHITTEST -> HTTRANSPARENT;
- WM_MOUSEACTIVATE -> MA_NOACTIVATE;
- ProductionHudPresentationContract();
- independent-flip requirement;
- Presentation API / DirectComposition production presentation path;
- premultiplied-alpha presentation contract.

Do not use window opacity or visual opacity as a VRR fix; Background Opacity only affects background. Changes are confined to Intel panel detection, VRR state/options, model policy, safe IGCL read/write and diagnostics. Existing HUD assertions/tests must remain passing.

## 15. Definition of done

- [ ] One implementation PR targets main; no artificial 500-LOC or PR-count cap.
- [ ] TMA2027 is enabled as production in normal enabled startup; no manual hidden gate.
- [ ] Exactly three supported panel families with correct OEM native ranges and model-specific decisions.
- [ ] All active, exact-matched supported panel INSTANCES are independently evaluated; same-model duplicate physical monitors supported.
- [ ] Read-only TMA physical EDID verification and 24 Hz telemetry exception are bound to the exact active panel before any special write.
- [ ] Standard eligible Claw 7 state may apply EXCELLENT and must verify true 48–120 active profile.
- [ ] OEM CUSTOM / OFF / unknown EDID / unsupported driver / setter error never force a new profile or produce false success.
- [ ] LG 48–180 and CSW 48–120 previous behavior and tests unchanged.
- [ ] Windows DRR / maximize-refresh semantics preserved; no global 120 Hz override.
- [ ] Multi-monitor errors isolate per target; aggregate and logs do not silently hide active supported target failure.
- [ ] Existing INI, Settings IPC and bounded retry behavior remain compatible.
- [ ] All targeted and production HUD contract regressions pass on main.
- [ ] Portable commit boundaries and branch-specific adaptation notes enable later integration/steamaddon cherry-pick.
- [ ] No EDID write, 30–120 modification, LFC patch, overclock, persistent VRR service or game hook was added.

**Implementation scope stops here.** User hardware confirmation is a post-release observational follow-up, not a condition for merging the completed, tested main implementation.
