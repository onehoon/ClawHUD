# Work Order — Upgrade VeloPack 1.2.0 → 1.2.158 on ClawHUD Main and SteamAddon Integration

> **Repository:** onehoon/ClawHUD  
> **Reviewed main baseline:** main@6c6af9f5b65523ccb887610d421a8abe08865e4c  
> **Reviewed integration baseline:** integration/steamaddon@93c27135d9635839d020c4eb38d0bfb6fb9adf9f  
> **Date:** 2026-09-22  
> **Scope:** focused VeloPack native library / packaging CLI upgrade only  
> **Target branches:** `main` and `integration/steamaddon`  
> **Expected PR count:** 2 small PRs — main first, then integration/steamaddon

---

## 1. Goal

Upgrade both maintained ClawHUD branches from VeloPack 1.2.0 to VeloPack 1.2.158 while preserving each branch's existing startup/update lifecycle.

Target:

```text
main
  velopack_libc       1.2.0 -> 1.2.158
  vpk CLI             1.2.0 -> 1.2.158
  third-party notice  1.2.0 -> 1.2.158

integration/steamaddon
  velopack_libc       1.2.0 -> 1.2.158
  vpk CLI             1.2.0 -> 1.2.158
  third-party notice  1.2.0 -> 1.2.158
```

The WPF `ClawHUD.Settings` frontend is not an update owner and must not gain a VeloPack NuGet dependency.

---

## 2. Branch order

Implement in this order:

```text
1. main
2. merge main PR after validation
3. port/cherry-pick only the VeloPack dependency/packaging changes to integration/steamaddon
4. validate integration/steamaddon independently
5. merge integration PR
```

Do not pull unrelated main lifecycle changes into `integration/steamaddon` as part of this task.

Preserve branch-specific source behavior unless 1.2.158 produces a concrete compatibility failure.

---

## 3. Current verified state

Both branches currently pin the same VeloPack version.

### Native VeloPack library

`CMakeLists.txt` contains:

```cmake
set(VELOPACK_VERSION "1.2.0")
set(VELOPACK_ARCHIVE "${CMAKE_BINARY_DIR}/_deps/velopack_libc_${VELOPACK_VERSION}.zip")
set(VELOPACK_ROOT "${CMAKE_BINARY_DIR}/_deps/velopack_libc_${VELOPACK_VERSION}")
```

The archive is SHA-256 pinned.

The x64 native import is:

```text
velopack_libc_win_x64_msvc.dll
```

and is staged as `velopack_libc.dll`.

### Release packaging CLI

Both branches' `.github/workflows/Build-Release.yml` install:

```text
dotnet tool install --global vpk --version 1.2.0
```

The release workflow uses the previous full package/feed as delta base and invokes:

```text
vpk pack
--packId ClawHUD
--packTitle ClawHUD
--mainExe ClawHUD.exe
--channel stable
--delta BestSpeed
--framework net10.0-x64-desktop,vcredist145-x64
```

### Runtime API

Both branches use the native C++ VeloPack client through `Velopack.hpp` / `velopack_libc.dll`.

The APIs in use remain supported in 1.2.158:

```text
Velopack::UpdateManager
UpdatePendingRestart
CheckForUpdates
DownloadUpdates
WaitExitThenApplyUpdates
```

No API migration is required.

---

## 4. Preserve the branch-specific lifecycle difference

Do not use this dependency update to normalize the two branches.

### main

Current `main` decides restart behavior from launch mode:

```cpp
const bool restart = clawhud::ShouldRestartAfterVelopackUpdate(launchMode_);
manager.WaitExitThenApplyUpdates(*pending, true, restart);
manager.WaitExitThenApplyUpdates(*update, true, restart);
```

Preserve this.

### integration/steamaddon

The integration branch has its own current update/apply behavior and SteamAddon ownership contract.

Do not copy main's lifecycle policy into this branch as part of the version bump.

If an App.cpp cherry-pick conflicts, resolve only the version-specific comment/context change and preserve integration behavior.

### Rule

```text
dependency version changes
+ packaging CLI changes
+ archive hash / notice / stale live comment changes

!=

update lifecycle rewrite
```

Any lifecycle convergence is a separate explicitly designed task.

---

## 5. Official 1.2.158 libc artifact and hash

Official release:

```text
https://github.com/velopack/velopack/releases/tag/1.2.158
Published: 2026-09-21
```

ClawHUD downloads:

```text
velopack_libc_1.2.158.zip
```

GitHub's release asset reports:

```text
SHA-256
63438c5d87b01d93853d0259a0d86eefccea8347914bd84fb58edbb5a71e05da
```

Use the uppercase equivalent for the existing CMake style:

```text
63438C5D87B01D93853D0259A0D86EEFCCEA8347914BD84FB58EDBB5A71E05DA
```

Independently verify the exact official asset before committing the new hash:

```powershell
Invoke-WebRequest -Uri "https://github.com/velopack/velopack/releases/download/1.2.158/velopack_libc_1.2.158.zip" -OutFile ".\velopack_libc_1.2.158.zip"
Get-FileHash ".\velopack_libc_1.2.158.zip" -Algorithm SHA256
```

If the computed hash differs, stop and re-verify the official artifact. Do not weaken/remove the hash pin.

---

## 6. Why 1.2.158 is useful to ClawHUD

### PR #944 — native x64 bootstrapper output

`vpk pack` now ships architecture-appropriate 64-bit Windows bootstrapper binaries.

ClawHUD is x64, so this aligns Setup/Update/stub architecture with the application.

### PR #1010 — zstd-only delta packaging

ClawHUD's release workflow explicitly requires a delta when a previous release exists.

Target:

```text
valid zstd delta
-> packaging succeeds

supported delta cannot be generated
-> packaging fails
-> do not publish an unusable fallback delta
```

Preserve the current fatal delta assertion.

### PR #974 — update apply timing logs

Useful for diagnosing slow EDR/AV apply. Do not add duplicate ClawHUD instrumentation solely for this upgrade.

### PR #1051 — Installed Apps EstimatedSize

No application code change required.

### PR #985 is not a ClawHUD driver

ClawHUD uses:

```text
packTitle = ClawHUD
mainExe   = ClawHUD.exe
```

The basenames already match. Do not add launcher migration code around this upstream fix.

---

## 7. Required changes on main

### CMake

Update `CMakeLists.txt`:

```cmake
set(VELOPACK_VERSION "1.2.0")
```

to:

```cmake
set(VELOPACK_VERSION "1.2.158")
```

Replace the archive SHA-256 with:

```text
63438C5D87B01D93853D0259A0D86EEFCCEA8347914BD84FB58EDBB5A71E05DA
```

Preserve:

- official GitHub release URL construction;
- existing pinned-hash download design;
- x64 MSVC import/DLL names;
- staging of `velopack_libc.dll`;
- unrelated PresentMon/build configuration.

### Release workflow

Update `.github/workflows/Build-Release.yml`:

```text
dotnet tool install --global vpk --version 1.2.0
```

to:

```text
dotnet tool install --global vpk --version 1.2.158
```

Do not change delta-base retrieval, channel, framework, artifact checks, or publication policy.

### Third-party notice

Update `THIRD-PARTY-NOTICES.md` from VeloPack 1.2.0 to 1.2.158.

### Stale live source comment

`src/ClawHUD/App.cpp` contains a live comment referring specifically to the pinned VeloPack 1.2.0 GithubSource.

Update only the stale version wording needed for source accuracy.

Do not change `CheckForUpdates()` behavior.

---

## 8. Required changes on integration/steamaddon

After main is implemented and validated, carry the equivalent dependency/packaging changes into `integration/steamaddon`:

```text
CMakeLists.txt
  VELOPACK_VERSION 1.2.0 -> 1.2.158
  archive SHA-256 -> verified 1.2.158 hash

.github/workflows/Build-Release.yml
  vpk 1.2.0 -> 1.2.158

THIRD-PARTY-NOTICES.md
  1.2.0 -> 1.2.158

src/ClawHUD/App.cpp
  stale version-specific comment only, where applicable
```

Do not overwrite branch-specific update/restart logic.

---

## 9. WPF Settings is out of scope

`src/ClawHUD.Settings` is framework-dependent WPF but is not the updater owner.

Do not add:

- VeloPack NuGet;
- UpdateManager;
- update checks;
- pending apply;
- installer ownership.

VeloPack remains owned by native `ClawHUD.exe`.

---

## 10. Explicit non-goals

Do not add or modify:

- update ownership;
- launch-mode policy;
- Managed/Standalone ownership rules;
- SteamAddon IPC;
- external-owner restart semantics;
- startup task ownership;
- tray behavior;
- PresentMon bootstrap;
- EC helper lifecycle;
- game detection;
- HUD lifecycle;
- WPF settings lifecycle;
- new updater wrappers/state managers;
- MSI packaging;
- VeloPack Flow;
- speculative synchronization.

Do not update unrelated dependencies.

---

## 11. Historical work orders remain historical

Do not mass-edit old work orders that mention VeloPack 1.2.0.

Only active source, current dependency metadata, current notices, and version-specific live comments should change.

---

## 12. main validation

Run the normal x64 production path:

```text
cmake configure
Release x64 build
CTest Release
WPF Settings publish
release staging
vpk pack 1.2.158
```

Verify:

- `velopack_libc.dll` is staged;
- WPF Settings payload is unchanged;
- PresentMon runtime payload is unchanged;
- full package is generated;
- delta is generated with a previous release base;
- existing delta assertion passes;
- no legacy bsdiff fallback is relied on;
- generated bootstrapper is x64 where practical to inspect;
- Standalone behavior remains unchanged;
- Managed launch-mode behavior remains unchanged.

Perform at least one real update from a release generated by 1.2.0 to a release generated by 1.2.158.

---

## 13. integration/steamaddon validation

Run the same build/CTest/pack validation on `integration/steamaddon`.

Additionally validate:

```text
SteamAddon starts/owns managed ClawHUD
-> ClawHUD starts with existing integration mode/arguments
-> update behavior follows the integration branch's current contract
-> version bump does not import main's lifecycle policy
-> exit/restart/relaunch remains compatible with the SteamAddon owner
```

The goal is to prove no regression, not redesign the behavior.

---

## 14. Acceptance criteria

### main

- VELOPACK_VERSION = 1.2.158;
- CMake hash matches official libc ZIP;
- release vpk = 1.2.158;
- notice = 1.2.158;
- stale live source comment corrected;
- build/tests/package pass;
- old-release -> new-release update succeeds;
- existing Standalone/Managed lifecycle preserved.

### integration/steamaddon

- equivalent dependency/packaging pins = 1.2.158;
- same verified libc hash;
- build/tests/package pass;
- SteamAddon-specific lifecycle preserved;
- no unrelated main behavior imported.

### overall

- no second updater owner;
- no new state machine/wrapper;
- WPF Settings remains non-owning;
- no unrelated lifecycle refactor.

---

## 15. PR review focus

Block concrete regressions such as:

- an active 1.2.0 pin remains;
- native libc and vpk CLI versions diverge;
- libc hash is wrong/unverified;
- staged 1.2.158 DLL cannot load;
- release cannot generate a valid delta;
- old 1.2.0-built release cannot update to the 1.2.158-built release;
- main launch-mode policy changes accidentally;
- integration branch loses SteamAddon-specific lifecycle;
- WPF Settings becomes a second updater owner.

Do not block for theoretical timing interleavings or unrelated architecture improvements.
