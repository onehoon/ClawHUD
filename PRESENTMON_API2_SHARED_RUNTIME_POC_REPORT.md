# PresentMon API2 Shared Runtime POC Report

Status: local Windows build, install, repair, uninstall/reinstall, service restart, and post-reboot smoke validation completed. Real hardware workload comparison and reboot/autostart evidence remain outside this run.

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
| Visual Studio/MSVC | VS 2022 BuildTools; MSVC 14.44.35207; MSBuild 17.14.51 |
| Windows SDK | 10.0.28000.0 |
| WiX | WiX Toolset 3.14.1.8722 installed; POC build used compatible WiX 3.11.2.4516 binaries |
| Architecture/configuration | x64 / Release; MSI package platform x86 as required by WiX merge-module authoring |

## Generated artifacts

Populate this table from the actual build output. Do not substitute estimates.

| Artifact | Size | SHA-256 |
|---|---:|---|
| `PresentMonSharedService.msm` | 2,064,384 bytes | `698C8641A6C81E2E432213E0FF8123D4EBB19191FE1559A24634E2C6578F0E4E` |
| `ClawHUD.PresentMonRuntime.msi` | 2,064,384 bytes | `1306518F03983B332F9D55AC91653B198FEC22A89DA71F631409D33BACC51E23` |
| `PresentMonService.exe` | 3,580,928 bytes | `4C0FDED6CFE67BA5437BE037475F54376428F26211D0D6D9174854B496038CE2` |
| `PresentMonAPI2.dll` | 1,979,904 bytes | `73B7F9B53494554D5CE5277D7E936AF5469E80E20681AEDE9B6A9772F5DE85E5` |
| `PresentMonAPI2Loader.dll` | 552,448 bytes | `0ECBC62D2555681363F3D232727030984D3A683ED9A96399990B7E64DE2C230E` |

## Installed state

| Evidence | Result |
|---|---|
| Install directory | `C:\Program Files\Intel\PresentMonSharedService\` — present; 5,560,832 bytes |
| Service | `PresentMonSharedService` — installed and RUNNING |
| Startup type | `AUTO_START`; LocalSystem; binary path points to installed `PresentMonService.exe` |
| Middleware registry | `HKLM\SOFTWARE\INTEL\PresentMon\Service\sharedMiddlewarePath` points to installed `PresentMonAPI2.dll` |
| Default control pipe | Not enumerated by the verification script; API smoke session succeeded |

## Smoke Test

The isolated client loads only `PresentMonAPI2Loader.dll` from its own directory. It does not ship or load `PresentMonAPI2.dll` app-locally, modify `PATH`, or hard-code the Program Files middleware path.

| Call | Exact `PM_STATUS` | Result |
|---|---:|---|
| `pmGetApiVersion` | 0 | returned `3.3.0` |
| `pmOpenSession` | 0 | succeeded against the installed shared service |
| `pmCloseSession` | 0 | succeeded |

## Install lifecycle

| Scenario | Result |
|---|---|
| Fresh install | PASS; elevated MSI install exit code 0 |
| Manual service restart | PASS; service returned to RUNNING |
| Reboot/autostart | PASS for this run; after repair requested reboot, service was RUNNING after reboot |
| Repair | PASS; elevated `/fa` repair returned 1641 and requested/restarted the machine |
| Uninstall | PASS; elevated MSI uninstall exit code 0, service and install directory absent |
| Reinstall | PASS; elevated MSI reinstall exit code 0, service RUNNING |
| Coexistence consumer A/B | pending; inspect fixed component GUIDs and `SharedDllRefCount` before any destructive test |

## Size comparison

| Package | Size |
|---|---:|
| Full PresentMon v2.5.1 MSI | not built by this slim-runtime-only POC; upstream reference approximately 157.7 MB |
| ClawHUD slim runtime MSI | 2,064,384 bytes (1.969 MiB) |
| Installed slim runtime | 5,560,832 bytes (5.303 MiB) |

## Problems

- Build prerequisites are intentionally detected rather than assumed by `build-runtime.ps1`.
- The repair operation requested a system restart (`msiexec` return code 1641); the machine restarted and post-reboot service/smoke checks passed.
- Coexistence consumer A/B and real game-workload validation were not performed in this run.

## Final Decision

BLOCKED — Shared Runtime POC local validation passed, but real hardware workload comparison and coexistence validation remain.

Concrete blockers: idle/game-load validation on the target B390 system and consumer A/B coexistence validation remain. The POC remains isolated and does not alter ClawHUD production telemetry, HUD, renderer, game detection, VRR, or existing PresentMon fallback behavior.
