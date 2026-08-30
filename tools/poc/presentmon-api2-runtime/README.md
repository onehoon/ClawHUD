# PresentMon API2 Shared-Service Runtime POC

This directory contains the isolated build machinery for the pinned PresentMon API2 runtime foundation. It does not replace the existing PresentMon executable path or connect API2 to production telemetry.

The POC consumes the official PresentMon `v2.5.1` source at commit `3e06c7dcb922e411bae38503b51ab501be61c37f` and builds only the shared-service merge module, its service/API2 dependencies, the API2 loader, a minimal MSI wrapper, and a non-admin smoke client.

## Layout

- `scripts/prepare-upstream.ps1` — clones and verifies the exact upstream tag.
- `scripts/build-runtime.ps1` — builds the upstream service/API2 projects, merge module, loader, and wrapper MSI.
- `scripts/PresentMonVcpkg.props` — isolated MSBuild compatibility shim that imports the pinned vcpkg targets without modifying upstream projects.
- `scripts/verify-runtime.ps1` — records installed service, registry, pipe, and artifact evidence.
- `scripts/collect-artifacts.ps1` — calculates actual artifact sizes and SHA-256 values for the report.
- `installer/` — minimal WiX wrapper that consumes `PresentMonSharedService.msm`.
- `smoke-test/` — temporary loader-only `pmGetApiVersion`/`pmOpenSession`/`pmCloseSession` client.
- `PRESENTMON_API2_SHARED_RUNTIME_POC_REPORT.md` — evidence report and final decision.

## Build

Run from a Developer PowerShell or a shell with Visual Studio, MSBuild, WiX 3.11+, and the upstream vcpkg prerequisites available:

```powershell
.\tools\poc\presentmon-api2-runtime\scripts\build-runtime.ps1
```

The script does not build `PMInstaller`, CEF, the PresentMon GUI, or the full PresentMon MSI. Build outputs are written outside the Git working tree by default (`D:\temp\ClawHUD-presentmon-api2-runtime-build`).

Use `-BuildRoot` and `-UpstreamRoot` to select different external output locations. The script refuses to use a source checkout whose resolved commit differs from the pinned commit.

## Runtime validation

Install the generated `ClawHUD.PresentMonRuntime.msi` as administrator, then run:

```powershell
.\tools\poc\presentmon-api2-runtime\scripts\verify-runtime.ps1 -BuildRoot D:\temp\ClawHUD-presentmon-api2-runtime-build
```

The verification script is evidence collection only. It does not create registry values, alter service configuration, or copy `PresentMonAPI2.dll` into the smoke-test directory. Install/repair/uninstall/reinstall and non-admin smoke testing must be run on the Windows validation machine and recorded in the report.

After building, use `collect-artifacts.ps1` to populate the report with measured sizes and hashes:

```powershell
.\tools\poc\presentmon-api2-runtime\scripts\collect-artifacts.ps1 `
  -BuildRoot D:\temp\ClawHUD-presentmon-api2-runtime-build `
  -UpstreamRoot D:\temp\PresentMon-v2.5.1-clawhud-poc
```

The vetted runtime artifacts used by normal ClawHUD builds are committed under
`third_party/presentmon/2.5.1/`. The scripts remain available to reproduce and
audit the pinned source build, but normal ClawHUD builds do not run them.
