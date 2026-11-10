# MSI Claw A8 ADLX power reader provenance

The read-only reader returns the first GPU's ADLX `GPUPower()` value used by MSI Center M's A8 OSD, truncating watts toward zero. On exact model `msi.claw.bz2em`, it is sampled through the existing visible-HUD system cadence and displayed in the legacy `TDP` slot; A8 power remains separate from Intel EC CPU package power and PresentMon GPU power. It adds no CPU, SoC, board-power or EC metric. The HUD label does not establish whole-APU package-power semantics.

## ABI and source evidence

Signatures and x64 slots were checked against AMD's public docs and ADLX 2.0.0.125 headers at `32b5a740d42295c5dfe9026b9f52683da0f3af91`. Headers, helper source, import libraries and AMD binaries are not build inputs. `amdadlx64.dll` is loaded from System32 only on explicit `Initialize()`; the local runtime's presence does not establish A8 support.

| API | ABI / x64 slot | Purpose |
| --- | --- | --- |
| ADLX exports | C `cdecl` | Version, initialize, terminate |
| `IADLXSystem::GetGPUs` | `stdcall`, 1 (`+0x08`) | GPU list |
| `IADLXSystem::GetPerformanceMonitoringServices` | `stdcall`, 9 (`+0x48`) | Metrics service |
| GPU list `Size` / `Begin` / typed `At` | 3 / 5 / 11 (`+0x58`) | Check and select first GPU |
| `GetSupportedGPUMetrics` / `IsSupportedGPUPower` | 21 (`+0xA8`) / 8 (`+0x40`) | Validate metric support |
| `GetCurrentGPUMetrics` / `GPUPower` | 18 (`+0x90`) / 9 (`+0x48`) | Fresh `double` watts sample |
| `IADLXInterface::Release` | 1 | Release acquired interfaces |

`IADLXSystem` has no reference-count prefix and is owned by ADLX; other returned interfaces are released in reverse order before termination. The reader terminates only its successful initialization. Requested API version is `min(runtime, 2.0.0.125)`.

The binding was independently authored. [MIT LibGPUDriverInteract](https://github.com/KillerPixelCrew/LibGPUDriverInteract/tree/c87cf216737cf07d6249f75f3b7a5d2879d67c31) and the [WSGM ABI provenance record](https://github.com/KillerPixelCrew/WSGM/blob/1df3f4d8987fcff5f1c8f2554c489195f1cb8b13/src/WSGM.Plugin.AmdGpu/PROVENANCE.md) informed interop and ownership review; no implementation code was copied.

## Licensing and validation limits

Compatibility of AMD's ADLX SDK agreement with ClawHUD GPLv3 remains unresolved. SDK-free declarations and other projects' precedent establish neither a license exception nor release clearance. No GPL change or AMD approval is claimed.

Fake-table tests need no AMD device. The reader is wired to the HUD, but no physical A8/MSI Center M comparison was performed. Compatibility of AMD's ADLX SDK agreement with ClawHUD GPLv3 remains unresolved; this integration is not legal or release clearance.
