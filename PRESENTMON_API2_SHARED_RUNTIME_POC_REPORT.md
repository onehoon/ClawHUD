# PresentMon API2 Shared Runtime POC Report

Status: implementation scaffolded; Windows build and install validation must be completed on the validation machine.

## Upstream

| Field | Value |
|---|---|
| Repository | `https://github.com/GameTechDev/PresentMon` |
| Version | `2.5.1` |
| Tag | `v2.5.1` |
| Resolved commit | `3e06c7dcb922e411bae38503b51ab501be61c37f` |
| License | MIT; see upstream `LICENSE` and repository copy `third_party/PresentMon-LICENSE.txt` |

All service, middleware, loader, merge-module, and installer inputs are obtained from this pinned revision. No Intel-signed binary is implied by this source build.

## Toolchain

| Field | Result |
|---|---|
| Visual Studio/MSVC | To be recorded by `vswhere`/build log |
| Windows SDK | To be recorded by MSBuild build log |
| WiX | WiX 3.11+ required; exact version to be recorded |
| Architecture/configuration | x64 / Release; MSI package platform x86 as required by WiX merge-module authoring |

## Generated artifacts

Populate this table from the actual build output. Do not substitute estimates.

| Artifact | Size | SHA-256 |
|---|---:|---|
| `PresentMonSharedService.msm` | pending | pending |
| `ClawHUD.PresentMonRuntime.msi` | pending | pending |
| `PresentMonService.exe` | pending | pending |
| `PresentMonAPI2.dll` | pending | pending |
| `PresentMonAPI2Loader.dll` | pending | pending |

## Installed state

| Evidence | Result |
|---|---|
| Install directory | `C:\Program Files\Intel\PresentMonSharedService\` — pending dynamic validation |
| Service | `PresentMonSharedService` — pending dynamic validation |
| Startup type | `auto` from pinned upstream authoring — pending service query |
| Middleware registry | `HKLM\SOFTWARE\INTEL\PresentMon\Service\sharedMiddlewarePath` — pending dynamic validation |
| Default control pipe | `\\.\pipe\sharedpresentmonsvcnamedpipe` — pending dynamic validation |

## Smoke Test

The isolated client loads only `PresentMonAPI2Loader.dll` from its own directory. It does not ship or load `PresentMonAPI2.dll` app-locally, modify `PATH`, or hard-code the Program Files middleware path.

| Call | Exact `PM_STATUS` | Result |
|---|---:|---|
| `pmGetApiVersion` | pending | pending |
| `pmOpenSession` | pending | pending |
| `pmCloseSession` | pending | pending |

## Install lifecycle

| Scenario | Result |
|---|---|
| Fresh install | pending |
| Manual service restart | pending |
| Reboot/autostart | pending; can be marked not dynamically tested if impractical |
| Repair | pending |
| Uninstall | pending |
| Reinstall | pending |
| Coexistence consumer A/B | pending; inspect fixed component GUIDs and `SharedDllRefCount` before any destructive test |

## Size comparison

| Package | Size |
|---|---:|
| Full PresentMon v2.5.1 MSI | pending local upstream artifact / reference approximately 157.7 MB |
| ClawHUD slim runtime MSI | pending actual build output |
| Installed slim runtime | pending actual installed directory |

## Problems

- Build prerequisites are intentionally detected rather than assumed by `build-runtime.ps1`.
- Dynamic service, registry, pipe, non-admin, lifecycle, repair, and coexistence evidence is not fabricated; it must be collected on the target Windows machine.
- The ClawHUD environment currently has no WiX 3.x executable on PATH, so a successful local MSI build cannot be claimed until WiX 3.11+ is installed or supplied through the documented build environment.

## Final Decision

FAIL — Shared Runtime POC has blockers.

Concrete blocker: the required WiX 3.x toolchain and target-machine install/service/smoke validation evidence are not currently available in this checkout environment. The POC remains isolated and does not alter ClawHUD production telemetry, HUD, renderer, game detection, VRR, or existing PresentMon fallback behavior.
