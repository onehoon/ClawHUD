# Work Order — Cherry-pick PresentMon v2.6.0 Upgrade into SteamAddon Integration

> **Repository:** `onehoon/ClawHUD`  
> **Target branch:** `integration/steamaddon`  
> **Reviewed integration baseline:** `8c982e9a627fc576798b6836ac2b0db9f2e5bb75`  
> **Source main squash commit:** `5939e0edc106ac0e818e32effb7a817e3b4ed361`  
> **Source PR:** #252 — `Upgrade PresentMon shared runtime to v2.6.0`  
> **Date:** 2026-09-23  
> **Expected PR count:** 1  
> **Implementation method:** cherry-pick the single squash commit; do not merge/rebase `main`

---

## 1. Goal

Bring the already-reviewed and squash-merged PresentMon v2.6.0 / API 3.4 runtime synchronization from `main` into `integration/steamaddon` without changing SteamAddon-specific behavior.

Use exactly this source commit:

```text
5939e0edc106ac0e818e32effb7a817e3b4ed361
Upgrade PresentMon shared runtime to v2.6.0
```

Do **not** cherry-pick the two pre-squash PR branch commits.

Do **not** merge or rebase the full `main` branch into `integration/steamaddon`.

The goal is for both branches to use the exact same vetted PresentMon runtime artifacts, API 3.4 header snapshots, query timing, build scripts, and provenance.

---

## 2. Reviewed branch relationship

The PresentMon v2.6.0 main squash commit changes 24 paths.

Those paths were compared against the current `integration/steamaddon` branch.

### Verified result

Of the files touched by the PresentMon squash commit, the only meaningful pre-existing SteamAddon branch divergence is:

```text
CMakeLists.txt
```

The SteamAddon branch intentionally has a different `CLAWHUD_VERSION` validation policy.

Current SteamAddon policy:

```cmake
if(NOT CLAWHUD_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "CLAWHUD_VERSION must be a numeric MAJOR.MINOR.PATCH without a tag prefix or suffix")
endif()
```

Current main policy is restricted to the standalone app's `0.1.x` range.

This SteamAddon-specific general semantic-version policy is required because the Runtime release workflow passes generated versions such as `1.0.0` through:

```text
-DCLAWHUD_VERSION=<steamaddon runtime version>
```

Therefore **the SteamAddon `CLAWHUD_VERSION` block must remain unchanged after the cherry-pick**.

The PresentMon hunk is separate from that block:

```cmake
set(PRESENTMON_VERSION "2.5.1")
set(PRESENTMON_API2_ROOT "${CMAKE_SOURCE_DIR}/third_party/presentmon/2.5.1")
```

and should become:

```cmake
set(PRESENTMON_VERSION "2.6.0")
set(PRESENTMON_API2_ROOT "${CMAKE_SOURCE_DIR}/third_party/presentmon/2.6.0")
```

No other SteamAddon-specific source divergence overlaps the PresentMon implementation changes.

`THIRD-PARTY-NOTICES.md` is identical between the reviewed pre-cherry-pick baselines apart from the PresentMon update being carried by the source commit.

---

## 3. SteamAddon Runtime packaging is already compatible

Do not modify the SteamAddon Runtime packaging contract.

The current payload manifest already requires generic runtime filenames:

```text
PresentMonAPI2Loader.dll
runtime/ClawHUD.PresentMonRuntime.msi
```

Specifically:

```text
steamaddon-runtime/payload.manifest.json
```

already contains both files in `required_files`.

The existing native CMake post-build step copies:

```text
third_party/presentmon/<PRESENTMON_VERSION>/PresentMonAPI2Loader.dll
  -> Release/PresentMonAPI2Loader.dll

third_party/presentmon/<PRESENTMON_VERSION>/ClawHUD.PresentMonRuntime.msi
  -> Release/runtime/ClawHUD.PresentMonRuntime.msi
```

Therefore changing the shared CMake PresentMon pin from 2.5.1 to 2.6.0 automatically causes the SteamAddon Runtime ZIP to package the new v2.6.0 loader and MSI.

Do not modify:

```text
steamaddon-runtime/payload.manifest.json
steamaddon-runtime/package-runtime.ps1
.github/workflows/Build-SteamAddon-Runtime.yml
```

for this task.

The filenames and package layout do not change.

---

## 4. Do not rebuild a separate SteamAddon PresentMon MSI

The main squash commit already carries the vetted binary artifacts:

```text
third_party/presentmon/2.6.0/PresentMonAPI2Loader.dll
third_party/presentmon/2.6.0/ClawHUD.PresentMonRuntime.msi
```

SteamAddon must use those exact artifacts.

Do **not** independently rebuild another MSI or loader specifically for the integration branch.

The purpose of this cherry-pick is binary/runtime identity between standalone ClawHUD and the SteamAddon-managed Runtime.

Expected artifact metadata from the merged main commit:

```text
PresentMon source:
  tag: v2.6.0
  commit: e13fce6acdb55a808fd8318175a56863e532d95f
  API: 3.4

PresentMonAPI2Loader.dll SHA-256:
  547EAFA630A61BCC2FD0A87A84158893F8F2271E346C60A7E7E4EB1D1FC16492

ClawHUD.PresentMonRuntime.msi SHA-256:
  72EFDE8AC2F5F49D52A790D826D7666CA2F9D15E36ADD483370CC8E6CEEAF90E
```

The wrapper MSI remains ProductVersion `2.6.0` and preserves the existing ClawHUD wrapper UpgradeCode.

UCI remains intentionally unbundled.

---

## 5. Cherry-pick procedure

Create one implementation branch from the latest `integration/steamaddon`:

```bash
git checkout integration/steamaddon
git pull --ff-only
git checkout -b codex/steamaddon-presentmon-2.6.0
```

Cherry-pick only the merged main squash commit and record its origin:

```bash
git cherry-pick -x 5939e0edc106ac0e818e32effb7a817e3b4ed361
```

### Expected result

The cherry-pick is expected to apply cleanly.

Although `CMakeLists.txt` differs between the branches, the existing branch-specific edits are in the `CLAWHUD_VERSION` validation block, while the cherry-picked hunk changes the later PresentMon pin.

Do not proactively rewrite `CMakeLists.txt`.

Let Git apply the cherry-pick first.

---

## 6. Conflict rule

If Git reports a conflict, do not resolve it by taking all of `main` or all of `integration/steamaddon`.

For `CMakeLists.txt`, the resolved file must contain **both**:

### Preserve SteamAddon version semantics

```cmake
if(NOT CLAWHUD_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "CLAWHUD_VERSION must be a numeric MAJOR.MINOR.PATCH without a tag prefix or suffix")
endif()
```

### Take the PresentMon v2.6.0 pin from main

```cmake
set(PRESENTMON_VERSION "2.6.0")
set(PRESENTMON_API2_ROOT "${CMAKE_SOURCE_DIR}/third_party/presentmon/2.6.0")
```

Do not import main's standalone `0.1.x` version restriction.

If any unexpected conflict appears in SteamAddon-specific packaging/workflow files, stop and inspect why. PR #252 does not modify those files, so such a conflict would indicate branch state changed after this work order was prepared.

---

## 7. Required post-cherry-pick source verification

After the cherry-pick, verify:

```text
PRESENTMON_VERSION = 2.6.0
PRESENTMON_API2_ROOT = third_party/presentmon/2.6.0
PM_API_VERSION_MAJOR = 3
PM_API_VERSION_MINOR = 4
FPS query window = 1000 ms
FPS query offset = 150 ms
ETW flush = 8 ms
system telemetry polling = 250 ms
```

Both local API header snapshots must match the main v2.6.0 snapshot:

```text
src/ClawHUD/PresentMonApi2Api.h
src/ClawHUD.Diag/PresentMonApi2Api.h
```

Expected Git blob SHA for both:

```text
e60a71a12f44ce36d5c109bb636309936fdaece7
```

Verify the old vendored runtime directory is removed:

```text
third_party/presentmon/2.5.1/
```

and the new directory exists:

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

---

## 8. Verify the SteamAddon-specific contract was not overwritten

This is mandatory.

After the cherry-pick, confirm `CMakeLists.txt` still accepts arbitrary numeric semantic versions used by the Runtime release workflow.

For example, configuration with:

```text
-DCLAWHUD_VERSION=1.0.0
```

must remain valid.

Do not allow the cherry-pick or conflict resolution to replace the integration branch rule with:

```text
^0\.1\....
```

because `.github/workflows/Build-SteamAddon-Runtime.yml` generates independent Runtime versions and passes them to `CLAWHUD_VERSION`.

---

## 9. Verify SteamAddon packaging picks up v2.6.0 automatically

Do not change the packaging code.

Instead, validate its existing behavior after the new CMake pin is active.

The native Release output must contain:

```text
PresentMonAPI2Loader.dll
runtime/ClawHUD.PresentMonRuntime.msi
```

and these files must be the exact v2.6.0 artifacts from:

```text
third_party/presentmon/2.6.0/
```

Then run the package script against the local build output.

Use a valid semantic Runtime version and the current 40-character source commit, for example:

```powershell
$sourceCommit = (git rev-parse HEAD).Trim()

.\steamaddon-runtime\package-runtime.ps1 `
  -BuildDirectory .\build `
  -OutputDirectory .\artifacts\SteamAddonRuntime-Test `
  -RuntimeVersion "1.0.0" `
  -SourceCommit $sourceCommit
```

This is packaging validation only.

Do **not** publish a GitHub Runtime release merely to validate this PR.

Do **not** dispatch a Runtime update to `SteamAddonforClaw` as part of the implementation PR.

The release workflow can be run later when an actual SteamAddon Runtime publication is intended.

---

## 10. Build and test validation

Configure the integration branch with a SteamAddon-valid Runtime version:

```powershell
cmake -S . -B build -A x64 `
  -DBUILD_TESTING=ON `
  -DCLAWHUD_VERSION=1.0.0
```

Build Release:

```powershell
cmake --build build --config Release --parallel 2
```

Run the complete test suite:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

If the existing integration CI excludes the known diagnostic WinEvent test for its environment, preserve the existing CI policy; do not change tests in this cherry-pick task.

The following PresentMon/VRR-sensitive test areas must remain green:

```text
PresentMonApi2Client
PresentMonTelemetryProvider
PresentMonFrameTelemetry
PresentMonRuntimeBootstrap
GameRenderVerifier
PresentActivitySource
ForegroundGameDetector
HudPresentationContract
HudPresentationLifecycle
```

---

## 11. HUD presentation / VRR safety remains non-negotiable

This is a dependency synchronization only.

Do not modify:

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
Presentation API / DirectComposition production path
premultiplied-alpha presentation contract
```

No SteamAddon-specific workaround may change these invariants.

PR #252 did not change these paths; the cherry-pick must not introduce unrelated edits there.

---

## 12. Game detection remains unchanged

Do not modify the integration branch's game-detection behavior.

Keep the existing PresentMon renderer verification semantics:

```text
PM_METRIC_BETWEEN_DISPLAY_CHANGE > 0
-> FirstDisplayedFrame
-> renderer verification evidence
```

Do not add new detection fallbacks or use the new v2.6.0 PSO metrics for detection.

The v2.6.0 update changes the API/runtime synchronization and FPS query offset, not the target-selection or game-session policy.

---

## 13. Expected diff

The implementation PR should be almost exactly the diff represented by main squash commit:

```text
5939e0edc106ac0e818e32effb7a817e3b4ed361
```

Expected changes include:

```text
CMake PresentMon pin 2.5.1 -> 2.6.0
API 3.3 -> 3.4 header snapshots
FPS offset 80 -> 150 ms
runtime bootstrap tests updated for 2.6.0
PresentMon telemetry timing test updated
2.5.1 vendored artifacts removed
2.6.0 vetted artifacts added
PresentMon build/reproduction scripts updated
active PresentMon docs/notices updated
```

Expected unchanged integration-specific files:

```text
steamaddon-runtime/payload.manifest.json
steamaddon-runtime/package-runtime.ps1
.github/workflows/Build-SteamAddon-Runtime.yml
```

Expected unchanged production behavior:

```text
HUD presentation
VRR safety contract
game detection
SteamAddon --managed launch semantics
Runtime ZIP layout
Runtime release/dispatch protocol
```

---

## 14. Diff audit against the source squash commit

Before opening the PR, compare the cherry-picked result to the source commit.

The goal is that differences attributable to the cherry-pick consist only of unavoidable integration-branch context, especially the preserved `CLAWHUD_VERSION` policy.

Useful checks:

```bash
git show --stat --oneline HEAD
git diff HEAD^ HEAD -- CMakeLists.txt
git diff --check
```

Also verify that these SteamAddon-specific files have no diff in the implementation commit:

```bash
git diff HEAD^ HEAD --   steamaddon-runtime/payload.manifest.json   steamaddon-runtime/package-runtime.ps1   .github/workflows/Build-SteamAddon-Runtime.yml
```

Expected output: no changes.

---

## 15. PR requirements

Open one PR with:

```text
base: integration/steamaddon
head: codex/steamaddon-presentmon-2.6.0
```

Suggested title:

```text
Sync PresentMon v2.6.0 into SteamAddon integration
```

The PR description should explicitly state:

- source main squash commit `5939e0edc106ac0e818e32effb7a817e3b4ed361`;
- cherry-pick was used instead of merging/rebasing main;
- SteamAddon `CLAWHUD_VERSION` semantics remain intact;
- SteamAddon packaging/workflow files are unchanged;
- the Runtime ZIP now receives the exact same v2.6.0 loader/MSI as standalone ClawHUD;
- build, CTest, and local package validation results.

---

## 16. Acceptance criteria

The PR is complete when:

- the single main squash commit is cherry-picked into the integration implementation branch;
- no full main merge/rebase is performed;
- `PRESENTMON_VERSION` is `2.6.0`;
- API headers are API 3.4 and match the main v2.6.0 snapshot;
- the same vetted loader/MSI binaries from main are present;
- `third_party/presentmon/2.5.1/` is removed;
- SteamAddon general MAJOR.MINOR.PATCH `CLAWHUD_VERSION` policy is preserved;
- configuring with `CLAWHUD_VERSION=1.0.0` succeeds;
- full Release build succeeds;
- full applicable CTest suite succeeds;
- local SteamAddon Runtime packaging succeeds;
- the packaged loader/MSI are the v2.6.0 artifacts;
- `payload.manifest.json`, `package-runtime.ps1`, and `Build-SteamAddon-Runtime.yml` are unchanged;
- no Runtime release is published during PR validation;
- no repository dispatch is sent during PR validation;
- game detection remains unchanged;
- HUD presentation / VRR contract remains unchanged;
- `git diff --check` passes.

---

## 17. Scope discipline

Do not add cleanup or refactoring to this PR.

In particular, do not:

```text
merge main
rebase integration/steamaddon onto main
change SteamAddon Runtime versioning
change Runtime release/dispatch behavior
change --managed launch behavior
change payload layout
rebuild a separate SteamAddon PresentMon MSI
add UCI
add PSO telemetry to HUD/Diag
refactor PresentMon client ownership
change game detection
change HUD presentation
```

This PR should be a narrow, auditable synchronization of the already-merged PresentMon v2.6.0 change.
