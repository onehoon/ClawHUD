# PresentMon API2 Shared-Service Runtime

This directory contains the reproducible build machinery for ClawHUD's production PresentMon API2 shared-service runtime. The ClawHUD package keeps the loader app-local and embeds a separately built machine-level wrapper MSI around the upstream shared-service merge module.

The build is pinned to the official PresentMon `v2.6.0` tag at commit `e13fce6acdb55a808fd8318175a56863e532d95f` (API 3.4). It builds only the shared-service merge module and dependencies, the matching API2 loader, ClawHUD's wrapper MSI, and temporary smoke/diagnostic clients. It does not build or embed the full PresentMon UI MSI, and it does not copy `PresentMonAPI2.dll` beside ClawHUD.exe.

ClawHUD keeps system telemetry polling at 250 ms and ETW flush at 8 ms. The HUD FPS query follows the v2.6.0 UI's 1000 ms window and 150 ms offset; the HUD publish cadence remains 500 ms.

## Layout

- `scripts/prepare-upstream.ps1` — clones and verifies the exact upstream tag.
- `scripts/build-runtime.ps1` — builds the upstream service/API2 projects, merge module, loader, and wrapper MSI.
- `scripts/PresentMonVcpkg.props` — isolated MSBuild compatibility shim that imports the pinned vcpkg targets without modifying upstream projects.
- `scripts/verify-runtime.ps1` — records installed service, registry, pipe, and artifact evidence.
- `scripts/collect-artifacts.ps1` — calculates actual artifact sizes and SHA-256 values for provenance/evidence.
- `installer/` — minimal WiX wrapper that consumes `PresentMonSharedService.msm`.
- `smoke-test/` — temporary loader-only `pmGetApiVersion`/`pmOpenSession`/`pmCloseSession` client.
- `third_party/presentmon/2.6.0/PROVENANCE.md` — pinned source details and committed artifact hashes.

## Build

Run from PowerShell with Visual Studio 2022 C++ build tools, MSBuild, WiX 3.11+, and the v2.6.0 vcpkg manifest dependencies available. The upstream CEF, frontend, and auxiliary-test-data bootstrap is not needed for these two projects.

Prepare the exact upstream checkout and its vcpkg dependencies once:

```powershell
$upstream = 'D:\temp\PresentMon-v2.6.0-clawhud-poc'
.\tools\poc\presentmon-api2-runtime\scripts\prepare-upstream.ps1
git clone https://github.com/microsoft/vcpkg.git "$upstream\build\vcpkg"
& "$upstream\build\vcpkg\bootstrap-vcpkg.bat" -disableMetrics
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$env:VCPKG_VISUAL_STUDIO_PATH = & $vswhere -version '[17.0,18.0)' -latest -products '*' -property installationPath
& "$upstream\build\vcpkg\vcpkg.exe" install --triplet x64-windows-static --x-manifest-root "$upstream"
```

Keep `PMON_UCI_SDK_DIR` unset. The v2.6.0 packaging script then generates an empty `uci_dist_files` group; the ClawHUD build script checks that the generated group contains no files.

Build from a Developer PowerShell or a shell with the prerequisites available:

```powershell
.\tools\poc\presentmon-api2-runtime\scripts\build-runtime.ps1
```

The script builds only `ServiceMergeModule.wixproj` and `PresentMonAPI2Loader.vcxproj`, then wraps the official `PresentMonSharedService.msm`. It does not build `PMInstaller`, CEF, the PresentMon GUI, or the full PresentMon MSI. The wrapper `ProductVersion` is read from ClawHUD's single `PRESENTMON_VERSION` pin. Build outputs are written outside the Git working tree by default (`D:\temp\ClawHUD-presentmon-api2-runtime-build`).

Use `-BuildRoot` and `-UpstreamRoot` to select different external output locations. The script refuses a source checkout whose resolved commit differs from the pin or whose tracked files have local modifications; untracked build, vcpkg, and generated files are allowed.

## Runtime validation

Install the generated `ClawHUD.PresentMonRuntime.msi` as administrator, then run:

```powershell
.\tools\poc\presentmon-api2-runtime\scripts\verify-runtime.ps1 -BuildRoot D:\temp\ClawHUD-presentmon-api2-runtime-build
```

The verification script is evidence collection only. It does not create registry values, alter service configuration, or copy `PresentMonAPI2.dll` into the smoke-test directory. The wrapper-upgrade script checks the committed v2.5.1-to-v2.6.0 upgrade and v2.6.0 repair/reinstall, confirms the service payload and `ddETWExternal.xml`, rejects UCI payload files, and runs the loader smoke client to require API 3.4. Install/repair/uninstall/reinstall and non-admin smoke testing must be run on a throwaway VM or designated Windows validation machine and recorded in the evidence.

After building, use `collect-artifacts.ps1` to print measured sizes and hashes for the build evidence:

```powershell
.\tools\poc\presentmon-api2-runtime\scripts\collect-artifacts.ps1 `
  -BuildRoot D:\temp\ClawHUD-presentmon-api2-runtime-build `
  -UpstreamRoot D:\temp\PresentMon-v2.6.0-clawhud-poc
```

The vetted runtime artifacts used by normal ClawHUD builds are committed under
`third_party/presentmon/2.6.0/`. The scripts remain available to reproduce and
audit the pinned source build, but normal ClawHUD builds do not run them.

## Wrapper MSI upgrade policy

The wrapper MSI `ProductVersion` tracks the bundled PresentMon runtime revision
(`build-runtime.ps1` reads `PRESENTMON_VERSION` from `CMakeLists.txt` and passes
it as `PresentMonRuntimeVersion` to the wrapper build -- one source of truth).
The wrapper carries a WiX `<MajorUpgrade>` on the stable `UpgradeCode`: a higher
wrapper version replaces an older wrapper product, and a downgrade is blocked so
a newer compatible shared runtime is never replaced by an older one. Windows
Installer compares the first three `ProductVersion` fields, so a future runtime
revision must increment one of them (`2.6.0 -> 2.6.1`).

The downgrade guarantee is self-enforced by wrapper packages `2.5.1` and later
(the previously shipped `1.0.0.0` wrapper has no `Upgrade` table). ClawHUD's own
flow never runs an older wrapper over a newer installed runtime because it only
installs its bundled wrapper when readiness fails, and a newer ABI-compatible
runtime reads as ready. See `third_party/presentmon/2.6.0/PROVENANCE.md`.

`scripts/validate-wrapper-upgrade.ps1` executes the real msiexec matrix
(elevated, throwaway VM) when regenerating the wrapper MSI: v2.5.1 -> v2.6.0
upgrade and v2.6.0 -> v2.6.0 repair are checked; downgrade rejection is checked
only when a newer MajorUpgrade-authored wrapper is passed as
`-NewerCleanup3Msi`.

## Shared-runtime uninstall ownership

The PresentMon shared service is a **machine-level shared runtime**. Another
application may legitimately consume the same compatible installation, and
ClawHUD cannot prove exclusive ownership at uninstall time. Therefore:

- Uninstalling ClawHUD removes the ClawHUD application files (via VeloPack) and
  the ClawHUD "Start with Windows" shortcut.
- It intentionally leaves the compatible PresentMon shared runtime, its service,
  its `Program Files\Intel\PresentMonSharedService` files, and its
  `HKLM\SOFTWARE\INTEL\PresentMon` registry state installed.

ClawHUD never runs `msiexec /x` on `ClawHUD.PresentMonRuntime`, deletes the
shared service, or removes the Intel PresentMon registry keys.
