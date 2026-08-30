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

## RTX 4070 desktop validation addendum

Validation date: 2026-08-30. The desktop GPU was identified as an NVIDIA GeForce RTX 4070 SUPER, not a base RTX 4070. These results apply to the installed RTX 4070 SUPER system only.

### Executive summary

| Check | Result |
|---|---|
| Slim runtime build and MSI | PASS; MSI 2,064,384 bytes (1.969 MiB) |
| Install/service/registry discovery | PASS; `PresentMonSharedService` RUNNING and `sharedMiddlewarePath` points to installed `PresentMonAPI2.dll` |
| Non-admin loader/API | PASS; `pmGetApiVersion=SUCCESS` (`3.3.0`), `pmOpenSession=SUCCESS`, `pmCloseSession=SUCCESS` |
| Introspection | PASS; 3 devices, 92 metrics, 13 enum tables |
| GPU telemetry | PASS; utilization, power, clock, temperature, memory used/frequency/utilization returned values |
| FPS/frame metrics | Query registration PASS, but this Control DX12 run produced no frame records and FPS remained zero |
| MSI lifecycle | PASS; uninstall/reinstall and repair were functionally checked; repair returned 1641 and requested reboot, after which service and smoke test passed |
| Production isolation | PASS; no ClawHUD production files changed |

### Environment

| Field | Observed value |
|---|---|
| Windows | Windows 11 Pro, build 26200, 64-bit |
| CPU | AMD Ryzen 5 7600 6-Core Processor |
| GPUs | SudoMaker Virtual Display Adapter; NVIDIA GeForce RTX 4070 SUPER |
| NVIDIA driver | 32.0.16.1656 |
| API2 NVIDIA device | `id=1`, `NVIDIA GeForce RTX 4070 SUPER` |
| User/elevation | `DESKTOP\\onehoon`; non-elevated diagnostic process |
| Toolchain | MSVC 14.44.35207, MSBuild 17.14.51, Windows SDK 10.0.28000.0 |
| WiX | Installed 3.14.1.8722; POC MSI build used compatible 3.11.2.4516 binaries |

### API2 and introspection

The dedicated diagnostic loaded only `PresentMonAPI2Loader.dll` from its own directory. It did not copy or link the service-installed `PresentMonAPI2.dll` app-locally.

```text
pmGetApiVersion status=SUCCESS raw=0 version=3.3.0
pmOpenSession status=SUCCESS raw=0
pmGetIntrospectionRoot status=SUCCESS raw=0
devices=3 metrics=92 enums=13
pmCloseSession status=SUCCESS raw=0
```

Devices returned by introspection:

```text
id=0      Device-independent
id=1      NVIDIA GeForce RTX 4070 SUPER
id=65536  System
```

The generated `presentmon-api2-introspection-rtx4070.json` preserves metric IDs, types, units, supported statistics, device associations, availability, and array sizes. The batch query now marshals the wrapper records into a contiguous `PM_QUERY_ELEMENT[]` buffer, then copies the returned offsets and sizes back before allocating the result blob. A rerun registered all 39 available dynamic/dynamic-frame entries in one batch (`SUCCESS`, raw `0`) and collected 20 successful samples for each batch poll; no per-metric fallback was required. Frame-query registration also succeeded (`11` elements, blob size `96`), while this desktop run produced no frame records.

### RTX 4070 SUPER capability matrix

Values are min–max across 20 samples. Idle used the desktop/Edge run; load used `Control_DX12.exe` PID 24152.

| Metric | Introspection | Query | Idle | Load | Classification | Notes |
|---|---:|---:|---:|---:|---|---|
| Displayed/Presented/Application FPS | available | success | 0 | 0 | ZERO_ONLY | No frame records in this run |
| GPU Utilization | available | success | 62–67% | 100% | WORKING | Strong load response |
| GPU Frequency | available | success | 2790 MHz | 780–2580 MHz | WORKING | Dynamic under load |
| GPU Temperature | available | success | 46–47 C | 54 C | WORKING | Sensible values |
| GPU Power | available | success | 108.501–109.052 W | 28.126–54.486 W | WORKING | Initial samples were warm-up zeroes |
| GPU Memory Used | available | success | 4.990–4.993 GB | 8.837–8.842 GB | WORKING | Raw values retained |
| GPU Memory Frequency | available | success | 10502 MHz | 10502 MHz | STATIC | Constant during run |
| GPU Memory Utilization | available | success | 38.7501–38.7517% | 68.6559% | WORKING | Load response observed |
| GPU Effective Frequency | unavailable | not attempted | n/a | n/a | UNAVAILABLE | No available NVIDIA device association |
| GPU Fan Speed | unavailable | not attempted | n/a | n/a | UNAVAILABLE | No available device entry |
| FrameType/PresentMode/PresentRuntime | available/frame | registered | no data | no data | NOT_APPLICABLE | Requires frame records |

### Polling, frame query, and process tracking

```text
pmStartTrackingProcess(24152)       -> SUCCESS
pmSetTelemetryPollingPeriod(100)    -> SUCCESS
pmSetTelemetryPollingPeriod(250)    -> SUCCESS
pmSetTelemetryPollingPeriod(500)    -> SUCCESS
pmSetTelemetryPollingPeriod(1000)   -> SUCCESS
pmSetEtwFlushPeriod(100)             -> SUCCESS
pmRegisterFrameQuery(11 elements)    -> SUCCESS, blob_size=96
pmConsumeFrames                      -> SUCCESS, count=0
pmStopTrackingProcess(24152)         -> SUCCESS
```

The harness sampled dynamic metrics 20 times at 250ms. GPU telemetry changed plausibly between desktop idle and Control DX12 load. The direct Control launch produced no frame records, so this run does not claim API2 FPS/frame replacement readiness.

### Service failure/recovery and security

Stopping the service caused the non-admin smoke client to return `pmOpenSession status=12` (`PIPE_ERROR`). Starting the service again restored `pmGetApiVersion=0`, `pmOpenSession=0`, and `pmCloseSession=0`; the service was `RUNNING` with `AUTO_START` and `LocalSystem`.

The installed directory is owned by TrustedInstaller; SYSTEM and Administrators have full control and Users have read/execute access. The PresentMon registry key is Administrators-owned and readable by Users. No ACLs were changed.

### Generated artifacts

External diagnostic artifacts are under `D:\temp\ClawHUD-presentmon-api2-desktop-validation-build\diagnostic\results4` (idle) and `results5-control-dx12` (load):

- `presentmon-api2-introspection-rtx4070.json`
- `presentmon-api2-all-metrics-rtx4070.txt`
- `presentmon-api2-diagnostic-rtx4070.log`

### Remaining validation gaps

Interactive Alt+Tab continuity, tracked-process exit semantics, sequential PID A→B frame-data transition, simultaneous multi-PID separation, standalone `PresentMon.exe` comparison, and coexistence consumer A/B were not completed in this run. These are validation gaps, not conclusions that API2 is unsupported. Intel B390 metrics, Intel XeFG behavior, ClawHUD production integration, and VRR regression behavior remain separate future validation work.

### Final assessment

PARTIAL PASS — Shared Runtime works, but specific API2/runtime issues must be resolved before B390 validation.

Concrete remaining issues are incomplete end-to-end frame/process validation on this desktop run and the absence of frame records from the Control DX12 test. Shared-service discovery, non-admin connection, GPU telemetry, and MSI lifecycle passed. ClawHUD production code remains untouched.
