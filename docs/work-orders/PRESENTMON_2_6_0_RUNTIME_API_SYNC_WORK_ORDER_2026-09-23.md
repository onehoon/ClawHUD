# Work Order — Upgrade ClawHUD Main to PresentMon v2.6.0 / API 3.4

> **Repository:** `onehoon/ClawHUD`  
> **Target branch:** `main` only  
> **Reviewed ClawHUD baseline:** `main@d6d17651e298533b613faaab2382f5a1f22768ad`  
> **Upstream project:** `GameTechDev/PresentMon`  
> **Required upstream tag:** `v2.6.0`  
> **Required upstream commit:** `e13fce6acdb55a808fd8318175a56863e532d95f`  
> **PresentMon API:** `3.3 -> 3.4`  
> **Date:** 2026-09-23  
> **Expected PR count:** 1 focused PR  
> **Porting plan:** merge to `main` first, then cherry-pick the squash commit to `integration/steamaddon`

---

## 1. Goal

Upgrade the production ClawHUD PresentMon shared-runtime foundation from PresentMon v2.5.1 / API 3.3 to PresentMon v2.6.0 / API 3.4.

Keep the existing architecture:

```text
ClawHUD.exe
  |
  +-- app-local PresentMonAPI2Loader.dll
  |
  +-- runtime/ClawHUD.PresentMonRuntime.msi
          |
          +-- official upstream PresentMonSharedService.msm
                  |
                  +-- PresentMonService.exe
                  +-- PresentMonAPI2.dll
                  +-- ddETWExternal.xml
```

The runtime remains a separately built machine-level MSI embedded in the ClawHUD package.

Do **not** replace this with the upstream full PresentMon MSI.

Do **not** copy `PresentMonAPI2.dll` beside ClawHUD.exe.

Do **not** change the production HUD presentation architecture or game-detection state machine as part of this dependency upgrade.

---

## 2. Exact upstream source rule

Use only:

```text
GameTechDev/PresentMon
tag:    v2.6.0
commit: e13fce6acdb55a808fd8318175a56863e532d95f
```

Do not build from PresentMon `main`.

Do not copy headers from PresentMon `main`.

This matters because upstream `main` already contains post-v2.6.0 changes. The ClawHUD runtime MSI, loader, local API header snapshots, provenance, and tests must all describe the same exact v2.6.0 source revision.

---

## 3. Verified current ClawHUD state

Current `main` pins:

```cmake
set(PRESENTMON_VERSION "2.5.1")
set(PRESENTMON_API2_ROOT "${CMAKE_SOURCE_DIR}/third_party/presentmon/2.5.1")
```

Current vetted artifacts are:

```text
third_party/presentmon/2.5.1/
  ClawHUD.PresentMonRuntime.msi
  PresentMonAPI2Loader.dll
  LICENSE
  PROVENANCE.md
  SHA256SUMS.txt
```

Production code uses an app-local loader and resolves the same public API2 endpoints required by v2.6.0:

```text
pmGetApiVersion
pmOpenSession
pmCloseSession
pmStartTrackingProcess
pmStopTrackingProcess
pmGetIntrospectionRoot
pmFreeIntrospectionRoot
pmSetTelemetryPollingPeriod
pmSetEtwFlushPeriod
pmFlushFrames
pmRegisterDynamicQuery
pmFreeDynamicQuery
pmPollDynamicQuery
pmPollStaticQuery
pmRegisterFrameQuery
pmConsumeFrames
pmFreeFrameQuery
```

The public signatures used by ClawHUD remain compatible in the exact v2.6.0 header.

No new endpoint is required for production ClawHUD.

---

## 4. Important v2.6.0 API changes that must be respected

### 4.1 API version

PresentMon v2.5.1:

```cpp
#define PM_API_VERSION_MAJOR 3
#define PM_API_VERSION_MINOR 3
```

PresentMon v2.6.0:

```cpp
#define PM_API_VERSION_MAJOR 3
#define PM_API_VERSION_MINOR 4
```

ClawHUD's runtime bootstrap intentionally requires exact API major/minor compatibility:

```cpp
version.major == PM_API_VERSION_MAJOR &&
version.minor == PM_API_VERSION_MINOR
```

Preserve this exact compatibility gate.

Do not weaken it to `>=`.

Once the local API header is updated to 3.4, an installed 2.5.1 / API 3.3 runtime must fail readiness and trigger the bundled 2.6.0 MSI upgrade.

### 4.2 Metric enum layout changed

v2.6.0 inserts these metrics before `PM_METRIC_PROCESS_ID`:

```cpp
PM_METRIC_PSO_COMPILE_COUNT,
PM_METRIC_PSO_COMPILE_TIME,
PM_METRIC_PSO_COMPILE_BUSY_PERCENT,
```

It also adds:

```cpp
PM_METRIC_CPU_CORE_TEMPERATURE
```

Therefore, keeping the v2.5.1 API header while running a v2.6.0 middleware is not acceptable.

In particular, the numeric value of `PM_METRIC_PROCESS_ID` changes.

ClawHUD frame-query code optionally includes `PM_METRIC_PROCESS_ID`, so the local header snapshots must be updated exactly with the runtime.

### 4.3 Availability enum expanded

v2.6.0 adds detailed unavailable reasons:

```cpp
PM_METRIC_AVAILABILITY_NOT_EXPORTED_BY_SOURCE
PM_METRIC_AVAILABILITY_NOT_SUPPORTED_BY_DEVICE
PM_METRIC_AVAILABILITY_NOT_IMPLEMENTED_BY_PRESENTMON
```

Current ClawHUD logic already checks:

```cpp
availability == PM_METRIC_AVAILABILITY_AVAILABLE
```

That remains correct.

Do not add special production behavior for the new unavailable reasons in this PR.

### 4.4 Polling limits changed

v2.6.0 public header:

```cpp
#define PM_TELEMETRY_PERIOD_MIN 50
#define PM_TELEMETRY_PERIOD_MAX 5000

#define PM_ETW_FLUSH_PERIOD_MIN 8
#define PM_ETW_FLUSH_PERIOD_MAX 1000
```

Current ClawHUD settings are already valid:

```text
system telemetry polling = 250 ms
ETW flush period         = 8 ms
```

Preserve the 250 ms ClawHUD system-telemetry policy.

Preserve the 8 ms ETW flush period.

Do not change system telemetry to the PresentMon UI's 100 ms default as part of this upgrade.

---

## 5. One production telemetry behavior change is required

Current ClawHUD explicitly follows the PresentMon v2.5.1 UI's FPS dynamic-query timing:

```cpp
kPresentMonFpsWindowMs = 1000.0;
kPresentMonFpsOffsetMs = 80.0;
kPresentMonEtwFlushPeriodMs = 8;
```

The exact v2.6.0 UI defaults are:

```text
metricsWindow = 1000 ms
metricsOffset = 150 ms
etwFlushPeriod = 8 ms
```

Source:

```text
IntelPresentMon/AppCef/ipm-ui-vue/src/core/preferences.ts
tag v2.6.0
```

Therefore update only:

```cpp
kPresentMonFpsOffsetMs = 80.0;
```

to:

```cpp
kPresentMonFpsOffsetMs = 150.0;
```

Keep:

```cpp
kPresentMonFpsWindowMs = 1000.0;
kPresentMonEtwFlushPeriodMs = 8;
```

Update the adjacent comment from "PresentMon v2.5.1 UI" to "PresentMon v2.6.0 UI".

This is a PresentMon query-timing synchronization only.

It is **not** a HUD presentation change.

It is **not** a game-detection change.

Add or update a unit assertion so this intentional v2.6.0 timing contract is visible in tests.

---

## 6. Game-detection behavior must remain unchanged

Production renderer verification currently uses:

```text
PM_METRIC_BETWEEN_DISPLAY_CHANGE > 0
```

through:

```text
pmRegisterFrameQuery
pmFlushFrames
pmConsumeFrames
```

The exact v2.6.0 metric definition remains:

```text
Ms Between Display Change
How long the previous frame was displayed before this Present() was displayed,
in milliseconds.
```

The v2.6.0 PSO metrics are inserted later in the metric enum and do not change the intended `PM_METRIC_BETWEEN_DISPLAY_CHANGE` semantics.

Do not modify:

```text
src/ClawHUD/GameDetection/GameSessionController.*
src/ClawHUD/GameDetection/ForegroundGameDetector.*
src/ClawHUD/GameDetection/GameRenderVerifier.*
src/ClawHUD/GameDetection/GameScreenAdmission.*
src/ClawHUD/GameDetection/KnownGameProcessCache.*
src/ClawHUD/PresentMonFrameTelemetry.cpp   behavior
```

The verifier contract stays:

```text
first valid BETWEEN_DISPLAY_CHANGE > 0
-> FirstDisplayedFrame
-> renderer verification evidence
```

Do not use new PSO metrics for game detection.

Do not add new fallback detection paths.

---

## 7. HUD presentation / VRR contract — do not touch

This PR must not modify or work around any production presentation invariant.

Protected contract:

```text
HUD windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
existing WS_EX_LAYERED behavior
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
ProductionHudPresentationContract()
independent-flip requirement
Presentation API production path
DirectComposition production path
premultiplied-alpha contract
```

Do not modify:

```text
src/ClawHUD/HudPresentation*
src/ClawHUD/HudRenderer*
src/ClawHUD/HudPresentationContract*
```

A PresentMon runtime upgrade must not be used as justification to alter the HUD window, compositor, buffers, opacity, hit testing, activation, or independent-flip behavior.

---

## 8. Replace both local API header snapshots with the exact v2.6.0 header

Current duplicated local snapshots:

```text
src/ClawHUD/PresentMonApi2Api.h
src/ClawHUD.Diag/PresentMonApi2Api.h
```

Source both from exactly:

```text
GameTechDev/PresentMon
v2.6.0
IntelPresentMon/PresentMonAPI2/PresentMonAPI.h
```

Prefer copying the exact upstream v2.6.0 file rather than manually editing selected enum values.

Do not refactor the duplicate-header ownership in this PR.

Do not copy the current upstream `main` header.

After replacement, verify at minimum:

```text
PM_API_VERSION_MAJOR == 3
PM_API_VERSION_MINOR == 4

PM_METRIC_PSO_COMPILE_COUNT exists
PM_METRIC_PSO_COMPILE_TIME exists
PM_METRIC_PSO_COMPILE_BUSY_PERCENT exists
PM_METRIC_CPU_CORE_TEMPERATURE exists

PM_METRIC_AVAILABILITY_NOT_EXPORTED_BY_SOURCE exists
PM_METRIC_AVAILABILITY_NOT_SUPPORTED_BY_DEVICE exists
PM_METRIC_AVAILABILITY_NOT_IMPLEMENTED_BY_PRESENTMON exists

PM_TELEMETRY_PERIOD_MIN == 50
PM_ETW_FLUSH_PERIOD_MIN == 8
```

Do not add post-v2.6.0 enum values from upstream `main`.

---

## 9. Update the single runtime version pin

Change:

```cmake
set(PRESENTMON_VERSION "2.5.1")
set(PRESENTMON_API2_ROOT "${CMAKE_SOURCE_DIR}/third_party/presentmon/2.5.1")
```

to:

```cmake
set(PRESENTMON_VERSION "2.6.0")
set(PRESENTMON_API2_ROOT "${CMAKE_SOURCE_DIR}/third_party/presentmon/2.6.0")
```

Keep the existing generated `PresentMonRuntimeVersion.h` path and version-floor architecture.

Do not add a second version constant elsewhere.

`PRESENTMON_VERSION` remains the ClawHUD source of truth for:

```text
vendored artifact path
runtime readiness version floor
wrapper MSI ProductVersion
```

---

## 10. Update the exact upstream build pin

### 10.1 `prepare-upstream.ps1`

Update the default external checkout path:

```text
D:\temp\PresentMon-v2.6.0-clawhud-poc
```

Pin:

```powershell
$tag = 'v2.6.0'
$expectedCommit = 'e13fce6acdb55a808fd8318175a56863e532d95f'
```

Keep the exact-commit verification.

Do not permit a floating release branch.

### 10.2 `build-runtime.ps1`

Update the default `UpstreamRoot` to the v2.6.0 path.

Remove this version-specific hardcode:

```powershell
'/p:PresentMonProductVersion=2.5.1'
```

Replace it with the already parsed `$presentMonVersion`, for example:

```powershell
('/p:PresentMonProductVersion=' + $presentMonVersion)
```

This is required so future runtime upgrades do not have two independent product-version pins.

Continue building only the minimum upstream graph:

```text
IntelPresentMon\ServiceMergeModule\ServiceMergeModule.wixproj
IntelPresentMon\PresentMonAPI2Loader\PresentMonAPI2Loader.vcxproj
```

Do not build or embed the full PresentMon UI MSI.

---

## 11. v2.6.0 shared-service MSI packaging details

The upstream `ServiceMergeModule.wixproj` / `ServiceMergeModule.wxs` merge-module contract is unchanged between v2.5.1 and v2.6.0.

Continue producing:

```text
PresentMonSharedService.msm
```

and continue wrapping that MSM with:

```text
tools/poc/presentmon-api2-runtime/installer/ClawHUD.PresentMonRuntime.wixproj
tools/poc/presentmon-api2-runtime/installer/ClawHUD.PresentMonRuntime.wxs
```

Do not redesign the wrapper MSI.

Preserve:

```text
Product Id="*"
stable UpgradeCode {4E9BE59E-7CC7-4F8F-BD00-22A44EC8B9A9}
MajorUpgrade
perMachine install
ProgramFiles64Folder\Intel\PresentMonSharedService
```

The resulting wrapper must have:

```text
ProductVersion = 2.6.0
```

### 11.1 New v2.6.0 NVIDIA manifest payload

v2.6.0's upstream installer library adds:

```text
ddETWExternal.xml
```

to the shared-service payload.

Do not manually copy it into ClawHUD.

It must arrive naturally through the official v2.6.0 `PresentMonSharedService.msm`.

Validate that the installed shared-runtime directory contains it.

### 11.2 UCI must remain optional and absent from the ClawHUD build

PresentMon v2.6.0 adds optional UCI / socwatch telemetry support.

ClawHUD does not need it for this upgrade.

Do not:

```text
set PMON_UCI_SDK_DIR
vendor a UCI SDK
commit unified-collector-interface.dll
add UCI files manually to the wrapper MSI
patch out UCI code in upstream source
```

The exact v2.6.0 packaging script supports a missing UCI SDK by generating an empty `uci_dist_files` component group.

The exact v2.6.0 service source also builds without the UCI SDK; the UCI provider remains unavailable instead of blocking the runtime.

This is the intended ClawHUD build mode.

Validate that the generated ClawHUD runtime MSI contains no UCI runtime payload.

---

## 12. Build the matching v2.6.0 loader

Build:

```text
IntelPresentMon/PresentMonAPI2Loader/PresentMonAPI2Loader.vcxproj
```

from the same exact v2.6.0 commit.

The 2.6 line includes the upstream loader/internal-API compatibility hardening.

Do not retain the v2.5.1 loader beside a v2.6.0 middleware.

The committed app-local loader and the wrapped shared middleware must come from the same v2.6.0 source checkout.

Do not change ClawHUD's app-local loader discovery logic.

---

## 13. Create the new vetted artifact directory

Create:

```text
third_party/presentmon/2.6.0/
```

with:

```text
ClawHUD.PresentMonRuntime.msi
PresentMonAPI2Loader.dll
LICENSE
PROVENANCE.md
SHA256SUMS.txt
```

`PROVENANCE.md` must record:

```text
Project: GameTechDev/PresentMon
Version: v2.6.0
Commit: e13fce6acdb55a808fd8318175a56863e532d95f
API: 3.4
```

Also record:

- artifacts are locally built from the pinned upstream source;
- binaries are not described as Intel-signed;
- wrapper ProductVersion is 2.6.0;
- stable ClawHUD wrapper UpgradeCode is preserved;
- `PresentMonSharedService.msm` is the upstream payload source;
- `ddETWExternal.xml` is included through the v2.6.0 MSM;
- UCI SDK/runtime is intentionally not bundled;
- exact SHA-256 values for the committed MSI and loader.

After all references and validation have moved to 2.6.0, remove the obsolete vendored `third_party/presentmon/2.5.1/` directory from the new tree.

Git history remains the source for the previous binary artifacts.

---

## 14. Current-document and notice updates

Update live/current references that describe the active runtime:

```text
THIRD-PARTY-NOTICES.md
tools/poc/presentmon-api2-runtime/README.md
tools/poc/presentmon-api2-runtime/diagnostic/README.md
tools/poc/presentmon-api2-runtime/scripts/PresentMonVcpkg.props   comment only if needed
tools/poc/presentmon-api2-runtime/scripts/validate-wrapper-upgrade.ps1
```

Update the PresentMon notice to:

```text
PresentMon v2.6.0
API 3.4
commit e13fce6acdb55a808fd8318175a56863e532d95f
third_party/presentmon/2.6.0/...
```

For `validate-wrapper-upgrade.ps1`, update the active expected current version from 2.5.1 to 2.6.0 and remove stale active-provenance paths.

Do not mass-replace `2.5.1` in:

```text
archive/
historical work orders
historical logs/examples
design documents whose statement explicitly describes the old 2.5.1 investigation
```

Historical evidence must remain historical.

---

## 15. Diag comments: update only what is proven

`src/ClawHUD.Diag/Api2Evidence.*` contains comments that mention v2.5.1 behavior.

The DynamicQuery per-row blob stride is verified unchanged in exact v2.6.0:

```cpp
blobSize_ = util::PadToAlignment(blobCursor, 16u);
```

That comment may be made version-neutral or updated to v2.6.0.

Do not rewrite behavioral observations such as null-address probe-row behavior as a v2.6.0 guarantee unless the generated 2.6.0 runtime is actually validated to behave the same way.

No Diag behavior expansion is required in this PR.

Do not add PSO diagnostics here as part of the runtime upgrade.

---

## 16. Production files that should not need logic changes

After the exact API header and version pin are synchronized, these production implementations should retain their current behavior:

```text
src/ClawHUD/PresentMonApi2Client.cpp
src/ClawHUD/PresentMonApi2Client.h
src/ClawHUD/PresentMonTelemetryProvider.cpp
src/ClawHUD/PresentMonTelemetryProvider.h
src/ClawHUD/PresentMonSystemTelemetry.cpp
src/ClawHUD/PresentMonFrameTelemetry.cpp
src/ClawHUD/PresentMonFrameTelemetry.h
src/ClawHUD/PresentMonRuntimeBootstrap.cpp
src/ClawHUD/PresentMonRuntimeBootstrap.h
```

The expected intentional production telemetry source change is limited to the v2.6.0 FPS query offset in `PresentMonProcessTelemetry.h`.

If implementation appears to require a larger behavior rewrite in one of these files, stop and diagnose the concrete incompatibility rather than widening the PR automatically.

---

## 17. Tests to update

### 17.1 Runtime version floor

Update `tests/PresentMonRuntimeBootstrapTests.cpp` so the generated required runtime is asserted as:

```text
2.6.0
```

Keep ABI compatibility and runtime-version-floor tests separate.

Test examples should include:

```text
2.6.0 >= 2.6.0  true
2.6.1 >= 2.6.0  true
2.7.0 >= 2.6.0  true
3.0.0 >= 2.6.0  true
2.5.1 >= 2.6.0  false
2.5.99 >= 2.6.0 false
```

### 17.2 FPS query timing

Add an assertion in the appropriate PresentMon telemetry test that the intended v2.6.0 production constants are:

```text
window = 1000 ms
offset = 150 ms
ETW flush = 8 ms
```

Do not change the 500 ms ClawHUD HUD publish cadence.

### 17.3 Existing frame-query/game-verifier tests

The existing frame telemetry and game verifier tests must continue passing without changing their expected renderer semantics.

Do not rewrite tests merely to accommodate a behavioral regression.

---

## 18. Wrapper MSI upgrade validation — mandatory

Do not treat a successful MSI build as sufficient.

Validate the real upgrade path on an elevated throwaway VM or validation machine.

### Scenario A — existing ClawHUD v2.5.1 runtime -> v2.6.0

Start with the currently committed v2.5.1 ClawHUD wrapper installed.

Install the new v2.6.0 wrapper.

Required result:

```text
exactly one "ClawHUD PresentMon Shared Runtime" product remains
DisplayVersion = 2.6.0
PresentMonSharedService is registered and running
sharedMiddlewarePath exists
sharedMiddlewarePath points to PresentMonAPI2.dll
PresentMonAPI2.dll file version = 2.6.0.x
pmGetApiVersion through the v2.6.0 loader = 3.4
```

### Scenario B — v2.6.0 repair/reinstall

Install the same v2.6.0 wrapper again.

Required result:

```text
no duplicate related product
service remains healthy
middleware remains valid
API remains 3.4
```

### Scenario C — ClawHUD bootstrap from installed 2.5.1

With 2.5.1 installed, run a new ClawHUD build containing the 2.6.0 header/pin/MSI.

Expected:

```text
2.5.1 middleware reports API 3.3
new app expects API 3.4
runtime readiness = false
runtime version floor = false
bundled 2.6.0 MSI runs
post-install validation = ready
application continues normally
```

### Scenario D — already-ready 2.6.0

Run ClawHUD again.

Expected:

```text
installedVersion=2.6.0
requiredVersion=2.6.0
API=3.4
action=reuse
no MSI launch
no unnecessary UAC prompt
```

---

## 19. Runtime payload validation

After building and installing the v2.6.0 wrapper, verify the service directory includes the expected v2.6.0 payload.

Expected:

```text
PresentMonService.exe
PresentMonAPI2.dll
ddETWExternal.xml
```

Expected absent for the ClawHUD build:

```text
unified-collector-interface.dll
UCI SDK distribution files
```

Do not infer payload correctness from file names alone.

Record file versions and SHA-256 hashes in provenance/evidence.

---

## 20. Application functional validation

Validate on a supported MSI Claw system with at least one known-good game.

Required checks:

```text
ClawHUD startup succeeds
PresentMon runtime readiness succeeds
FPS appears after a valid game target is established
DISPLAYED_FPS remains the HUD FPS authority
PRESENTED_FPS remains optional
process exit clears/retargets cleanly
relaunching the game reacquires telemetry
renderer verification still observes FirstDisplayedFrame
foreground-first game detection behavior is unchanged
Steam RunningAppID behavior is unchanged
system telemetry still initializes
no new repeated service-install/UAC loop occurs
```

Also exercise one process transition while the service remains running.

No new retry/state-machine machinery should be added unless a reproducible v2.6.0 regression requires it.

---

## 21. Full test/build validation

Configure and build Release with tests enabled, then run the complete test suite.

At minimum, explicitly confirm these tests:

```text
ClawHUD.PresentMonApi2ClientTests
ClawHUD.PresentMonTelemetryProviderTests
ClawHUD.PresentMonFrameTelemetryTests
ClawHUD.PresentMonDebugFrameTelemetryTests
ClawHUD.PresentMonRuntimeBootstrapTests
ClawHUD.GameRenderVerifierTests
ClawHUD.PresentActivitySourceTests
ClawHUD.ForegroundGameDetectorTests
ClawHUD.HudPresentationContractTests
ClawHUD.HudPresentationLifecycleTests
```

The full CTest run is still required; the list above is not a replacement.

---

## 22. Presentation regression gate

Before merge, verify that the PR diff does not alter:

```text
ProductionHudPresentationContract()
windowExStyle
WS_EX_TRANSPARENT
WS_EX_NOACTIVATE
WS_EX_TOPMOST
WS_EX_LAYERED behavior
WM_NCHITTEST -> HTTRANSPARENT
WM_MOUSEACTIVATE -> MA_NOACTIVATE
buffer count/format
shared displayable resource flags
DXGI_ALPHA_MODE_PREMULTIPLIED
identity transform
independentFlipRequired
Presentation API / DirectComposition production path
```

`ClawHUD.HudPresentationContractTests` and `ClawHUD.HudPresentationLifecycleTests` must stay green.

The PresentMon upgrade is not allowed to weaken the VRR-safe HUD contract.

---

## 23. Expected implementation file set

Expected modified text files include approximately:

```text
CMakeLists.txt

src/ClawHUD/PresentMonApi2Api.h
src/ClawHUD.Diag/PresentMonApi2Api.h
src/ClawHUD/PresentMonProcessTelemetry.h

tests/PresentMonRuntimeBootstrapTests.cpp
tests/PresentMonTelemetryProviderTests.cpp             # only if used for timing assertions

tools/poc/presentmon-api2-runtime/README.md
tools/poc/presentmon-api2-runtime/diagnostic/README.md
tools/poc/presentmon-api2-runtime/scripts/prepare-upstream.ps1
tools/poc/presentmon-api2-runtime/scripts/build-runtime.ps1
tools/poc/presentmon-api2-runtime/scripts/PresentMonVcpkg.props
tools/poc/presentmon-api2-runtime/scripts/validate-wrapper-upgrade.ps1

THIRD-PARTY-NOTICES.md

third_party/presentmon/2.6.0/PROVENANCE.md
third_party/presentmon/2.6.0/SHA256SUMS.txt
third_party/presentmon/2.6.0/LICENSE
third_party/presentmon/2.6.0/PresentMonAPI2Loader.dll
third_party/presentmon/2.6.0/ClawHUD.PresentMonRuntime.msi
```

Expected removed vendored directory after successful migration:

```text
third_party/presentmon/2.5.1/
```

A larger production source diff is a reason to re-check scope.

---

## 24. Explicitly out of scope

Do not include any of the following in this PR:

```text
HUD presentation refactor
HUD opacity changes
HUD renderer changes
window-style changes
game-detection redesign
new game-detection fallbacks
new PresentMon PSO UI/HUD metrics
new VRR diagnostic metrics
UCI support
CPU-core-temperature UI
telemetry-provider architecture refactor
PresentMonAPI2 client abstraction refactor
duplicate API-header ownership refactor
SteamAddon-specific code
integration/steamaddon workflow changes
```

Those are separate tasks if desired later.

---

## 25. Main-first / SteamAddon cherry-pick requirement

This PR targets `main` only.

Do not modify:

```text
steamaddon-runtime/
.github/workflows/Build-SteamAddon-Runtime.yml
```

The shared PresentMon source/build/artifact files currently match between `main` and `integration/steamaddon`.

After this PR is squash-merged to `main`, the intended next operation is:

```text
git checkout integration/steamaddon
git cherry-pick <main-presentmon-2.6.0-squash-commit>
```

The only known branch-local CMake difference is outside the PresentMon block. Preserve the integration branch's own `CLAWHUD_VERSION` policy if a CMake context conflict occurs.

Do not merge/rebase all of `main` into the integration branch merely to carry this dependency update.

---

## 26. Acceptance criteria

The PR is complete only when all of the following are true:

- ClawHUD pins PresentMon v2.6.0 and API 3.4 consistently.
- Both local API header snapshots are exact v2.6.0 snapshots.
- The app-local loader is built from v2.6.0.
- The wrapper MSI is built from the v2.6.0 upstream shared-service MSM.
- Wrapper ProductVersion is 2.6.0.
- Existing 2.5.1 wrapper upgrades cleanly to 2.6.0.
- `pmGetApiVersion` through the installed runtime returns API 3.4.
- The v2.6.0 runtime payload contains `ddETWExternal.xml`.
- UCI SDK/runtime is not bundled and is not required.
- FPS dynamic-query window remains 1000 ms.
- FPS dynamic-query offset is updated to the v2.6.0 UI value of 150 ms.
- ETW flush remains 8 ms.
- System telemetry polling remains 250 ms.
- Game-detection behavior is unchanged.
- `PM_METRIC_BETWEEN_DISPLAY_CHANGE > 0` remains the renderer-verification authority.
- HUD presentation / VRR contract is unchanged.
- Full Release build succeeds.
- Full CTest succeeds.
- Runtime upgrade/reuse validation succeeds on Windows.
- Provenance and SHA-256 records match the committed binary artifacts.
- No active/current runtime documentation still claims that production uses v2.5.1.
- Historical/archive evidence is not rewritten.
- The resulting squash commit is suitable for cherry-pick to `integration/steamaddon`.

---

## 27. Suggested PR title

```text
Upgrade PresentMon shared runtime to v2.6.0
```

Keep the PR focused on the runtime/API synchronization described above.
