#pragma once

// Pinned to Intel IGCL igcl_api.h v1.1 (2025 header).  Only the documented
// read-only ABI used by the survey is mirrored here; no ControlLib runtime is
// bundled.  Keep this file in lock-step with the pinned header revision.
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace clawhud::igcl
{
using Result = std::uint32_t;
using Api = void*; using Device = void*; using Output = void*;
using Frequency = void*; using Engine = void*; using Memory = void*;
using Temperature = void*; using Power = void*; using Fan = void*;
using InitFn = Result(__cdecl*)(void*, Api*);
using CloseFn = Result(__cdecl*)(Api);
using EnumDevicesFn = Result(__cdecl*)(Api, std::uint32_t*, Device*);
using EnumFn = Result(__cdecl*)(Device, std::uint32_t*, void*);
using DevicePropsFn = Result(__cdecl*)(Device, void*);
using DevicePciFn = Result(__cdecl*)(Device, void*);
using OutputPropsFn = Result(__cdecl*)(Output, void*);
using TelemetryFn = Result(__cdecl*)(Device, void*);
using PropsFn = Result(__cdecl*)(void*, void*);
using StateFn = Result(__cdecl*)(void*, void*);
using FanStateFn = Result(__cdecl*)(Fan, std::uint32_t, std::int32_t*);

struct InitArgs { std::uint32_t size{}; std::uint8_t version{}; std::uint32_t appVersion{}; std::uint32_t flags{}; std::uint32_t supportedVersion{}; std::uint8_t applicationUid[16]{}; };
union DataValue { std::int8_t i8; std::uint8_t u8; std::int16_t i16; std::uint16_t u16; std::int32_t i32; std::uint32_t u32; std::int64_t i64; std::uint64_t u64; float f; double d; };
struct Item { bool supported{}; std::uint32_t units{}; std::uint32_t type{}; DataValue value{}; };
struct Psu { bool supported{}; std::uint32_t type{}; Item energy{}; Item voltage{}; };
struct PowerTelemetryV2 { std::uint32_t size{}; std::uint8_t version{}; Item timeStamp{},gpuEnergyCounter{},gpuVoltage{},gpuCurrentClockFrequency{},gpuCurrentTemperature{},globalActivityCounter{},renderComputeActivityCounter{},mediaActivityCounter{}; bool gpuPowerLimited{},gpuTemperatureLimited{},gpuCurrentLimited{},gpuVoltageLimited{},gpuUtilizationLimited{}; Item vramEnergyCounter{},vramVoltage{},vramCurrentClockFrequency{},vramCurrentEffectiveFrequency{},vramReadBandwidthCounter{},vramWriteBandwidthCounter{},vramCurrentTemperature{}; bool vramPowerLimited{},vramTemperatureLimited{},vramCurrentLimited{},vramVoltageLimited{},vramUtilizationLimited{}; Item totalCardEnergyCounter{}; Psu psu[5]{}; Item fanSpeed[5]{},gpuVrTemp{},vramVrTemp{},saVrTemp{},gpuEffectiveClock{},gpuOverVoltagePercent{},gpuPowerPercent{},gpuTemperaturePercent{},vramReadBandwidth{},vramWriteBandwidth{}; };
struct DeviceProperties { std::uint32_t Size{}; std::uint8_t Version{}; void* pDeviceID{}; std::uint32_t device_id_size{}; std::uint32_t device_type{}; std::uint64_t supported_subfunction_flags{}; std::uint64_t driver_version{}; std::uint8_t firmware_version[32]{}; std::uint32_t pci_vendor_id{},pci_device_id{},rev_id{},num_eus_per_sub_slice{},num_sub_slices_per_slice{},num_slices{}; char name[100]{}; std::uint64_t graphics_adapter_properties{}; std::uint32_t Frequency{}; std::uint16_t pci_subsys_id{},pci_subsys_vendor_id{}; std::uint8_t adapter_bdf[4]{}; std::uint32_t num_xe_cores{}; std::uint8_t reserved[108]{}; };
struct FrequencyProperties { std::uint32_t Size{}; std::uint8_t Version{}; std::uint32_t type{}; bool canControl{}; double min{},max{}; };
struct FrequencyState { std::uint32_t Size{}; std::uint8_t Version{}; double currentVoltage{},request{},tdp{},efficient{},actual{}; std::uint32_t throttleReasons{}; };
struct EngineProperties { std::uint32_t Size{}; std::uint8_t Version{}; std::uint32_t type{}; };
struct EngineStats { std::uint32_t Size{}; std::uint8_t Version{}; std::uint64_t activeTime{},timestamp{}; };
struct MemoryProperties { std::uint32_t Size{}; std::uint8_t Version{}; std::uint32_t type{},location{}; std::uint64_t physicalSize{}; std::int32_t busWidth{},numChannels{}; };
struct MemoryState { std::uint32_t Size{}; std::uint8_t Version{}; std::uint64_t free{},size{}; };
struct MemoryBandwidth { std::uint32_t Size{}; std::uint8_t Version{}; std::uint64_t maxBandwidth{},timestamp{},readCounter{},writeCounter{}; };
struct TemperatureProperties { std::uint32_t Size{}; std::uint8_t Version{}; std::uint32_t type{}; double maxTemperature{}; };
struct PowerProperties { std::uint32_t Size{}; std::uint8_t Version{}; bool canControl{}; std::int32_t defaultLimit{},minLimit{},maxLimit{}; };
struct PowerEnergy { std::uint32_t Size{}; std::uint8_t Version{}; std::uint64_t energy{},timestamp{}; };
struct FanProperties { std::uint32_t Size{}; std::uint8_t Version{}; bool canControl{}; std::uint32_t supportedModes{},supportedUnits{}; std::int32_t maxRPM{},maxPoints{}; };
struct PciProperties { std::uint32_t Size{}; std::uint8_t Version{}; std::uint8_t raw[128]{}; };
struct LiveState { std::uint32_t gfxApi{},targetFps{},framePacingStatus{},reserved[4]{}; };
struct FeatureRequest { std::uint32_t size{}; std::uint8_t version{}; std::uint32_t featureType{}; char* applicationName{}; std::int8_t applicationNameLength{}; bool set{}; std::uint32_t valueType{}; std::uint64_t value{}; std::int32_t customValueSize{}; void* customValue{}; };
static_assert(offsetof(PowerTelemetryV2, vramEnergyCounter) > offsetof(PowerTelemetryV2, gpuUtilizationLimited));
static_assert(offsetof(PowerTelemetryV2, psu) < offsetof(PowerTelemetryV2, fanSpeed));
static_assert(offsetof(PowerTelemetryV2, fanSpeed) < offsetof(PowerTelemetryV2, gpuVrTemp));
static_assert(sizeof(Item) == 24);
static_assert(offsetof(Item, value) == 16);
}
