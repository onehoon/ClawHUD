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
