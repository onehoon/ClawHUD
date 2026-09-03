# PresentMon API2 Runtime Provenance

Project: GameTechDev/PresentMon
Version: v2.5.1
Commit: 3e06c7dcb922e411bae38503b51ab501be61c37f
API: 3.3

The committed artifacts are built from the pinned upstream commit using the
isolated runtime POC build scripts in
`tools/poc/presentmon-api2-runtime/scripts/`. The runtime MSI wraps the
official `PresentMonSharedServiceModule` built from the same upstream source
and version. `PresentMonAPI2Loader.dll` is kept app-local and is not the
service middleware.

These locally built binaries are not described as Intel-signed binaries.

## Wrapper MSI upgrade metadata (Cleanup 3)

`ClawHUD.PresentMonRuntime.msi` now carries:

- `ProductVersion = 2.5.1` (the bundled PresentMon runtime revision, supplied by
  `build-runtime.ps1` from the single `PRESENTMON_VERSION` pin in
  `CMakeLists.txt`), replacing the previous fixed `1.0.0.0`.
- A WiX `<MajorUpgrade>` policy on the unchanged `UpgradeCode`
  `{4E9BE59E-7CC7-4F8F-BD00-22A44EC8B9A9}`: a higher wrapper `ProductVersion`
  replaces an older wrapper product; a lower version is blocked
  (`AllowDowngrades` / `AllowSameVersionUpgrades` are not set).
- A fresh `ProductCode` (`Product Id="*"`), as every real major-upgrade package
  must.

The embedded payload (`PresentMonService.exe`, `PresentMonAPI2.dll`, both
`2.5.1.0`) is byte-identical to the previous committed MSI's `File` table -- only
the product/upgrade metadata changed. Rebuilt with WiX 3.14 `candle`/`light`
against the same `PresentMonSharedServiceModule.msm`; `ICE60` and the
merge-module `ICE82` sequence warnings are expected and non-fatal.

A future runtime revision (e.g. `2.5.2`) is one intentional change to
`PRESENTMON_VERSION` plus the vetted replacement `third_party/presentmon/<ver>/`
artifacts, then regenerate this MSI and update `SHA256SUMS.txt`.

### Scope of the downgrade guarantee

`<MajorUpgrade>` authoring lives in the MSI that is *run*. The previously
shipped `1.0.0.0` wrapper has **no** `Upgrade` table, so:

- **old -> new works**: the `2.5.1` package's `FindRelatedProducts` /
  `RemoveExistingProducts` detect and replace an installed `1.0.0.0` wrapper
  (`VersionMax = 2.5.1`).
- **new -> old is only self-enforced from `2.5.1` onward**: the shipped `1.0.0.0`
  package cannot itself detect or reject a newer installed product. Running it
  after `2.5.1` could register a second related product.

ClawHUD's own flow does not hit that second case: a ClawHUD build runs its
bundled wrapper MSI **only when `IsPresentMonRuntimeReady()` is false**. An
installed `2.5.1` runtime is ABI-compatible (2.5.1 is the ABI baseline), so a
pre-Cleanup-3 ClawHUD sees the runtime as ready and never executes its older
bundled MSI over it. A Cleanup-3+ ClawHUD additionally applies the
runtime-version floor, so it never triggers an "upgrade" against an equal or
newer installed runtime. A manual `msiexec /i old.msi` outside ClawHUD is out of
scope.

The real old<->new execution matrix (single product after upgrade, downgrade
rejected, service/API2 still valid) is scripted at
`tools/poc/presentmon-api2-runtime/scripts/validate-wrapper-upgrade.ps1`; run it
on a throwaway VM when regenerating this MSI.
