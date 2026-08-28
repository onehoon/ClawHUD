#pragma once

// Copyright (C) 2025 Intel Corporation
// Selected read-only ABI declarations below are derived from Intel IGCL
// igcl_api.h at b6c462933502e13d1537dd5024949a51be30e63d and are redistributed
// under the Intel Software License Agreement bundled at:
// third_party/Intel-IGCL-LICENSE.txt
// ClawHUD does not redistribute ControlLib.dll.
//
// Pinned to Intel IGCL igcl_api.h v1.1 (2025 header). Only the documented
// read-only ABI used by the survey is mirrored here. Keep this file in
// lock-step with the pinned header revision.
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
struct PowerTelemetryV2 { union { std::uint32_t Size; std::uint32_t size; }; union { std::uint8_t Version; std::uint8_t version; }; Item timeStamp{},gpuEnergyCounter{},gpuVoltage{},gpuCurrentClockFrequency{},gpuCurrentTemperature{},globalActivityCounter{},renderComputeActivityCounter{},mediaActivityCounter{}; bool gpuPowerLimited{},gpuTemperatureLimited{},gpuCurrentLimited{},gpuVoltageLimited{},gpuUtilizationLimited{}; Item vramEnergyCounter{},vramVoltage{},vramCurrentClockFrequency{},vramCurrentEffectiveFrequency{},vramReadBandwidthCounter{},vramWriteBandwidthCounter{},vramCurrentTemperature{}; Item totalCardEnergyCounter{}; Psu psu[5]{}; Item fanSpeed[5]{},gpuVrTemp{},vramVrTemp{},saVrTemp{},gpuEffectiveClock{},gpuOverVoltagePercent{},gpuPowerPercent{},gpuTemperaturePercent{},vramReadBandwidth{},vramWriteBandwidth{}; PowerTelemetryV2() : Size(1016), Version(1) {} };
struct FirmwareVersion { std::uint64_t majorVersion{}; std::uint64_t minorVersion{}; std::uint64_t buildNumber{}; };
struct AdapterBdf { std::uint8_t bus{}; std::uint8_t device{}; std::uint8_t function{}; };
struct DeviceProperties { union { std::uint32_t Size; std::uint32_t size; }; std::uint8_t Version{}; void* pDeviceID{}; std::uint32_t device_id_size{}; union { std::uint32_t device_type; std::uint32_t deviceType; }; std::uint32_t supported_subfunction_flags{}; union { std::uint64_t driver_version; std::uint64_t driverVersion; }; FirmwareVersion firmware_version{}; union { std::uint32_t pci_vendor_id; std::uint32_t vendor; }; union { std::uint32_t pci_device_id; std::uint32_t device; }; union { std::uint32_t rev_id; std::uint32_t revision; }; std::uint32_t num_eus_per_sub_slice{},num_sub_slices_per_slice{}; std::uint32_t num_slices{}; char name[100]{}; union { std::uint32_t graphics_adapter_properties; std::uint32_t graphicsFlags; }; std::uint32_t Frequency{}; std::uint16_t pci_subsys_id{},pci_subsys_vendor_id{}; AdapterBdf adapter_bdf{}; std::uint32_t num_xe_cores{}; char reserved[108]{}; };
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
struct PciSpeed { std::uint32_t Size{}; std::uint8_t Version{}; std::int32_t gen{}; std::int32_t width{}; std::int64_t maxBandwidth{}; };
struct PciAddress { std::uint32_t Size{}; std::uint8_t Version{}; std::uint32_t domain{}; std::uint32_t bus{}; std::uint32_t device{}; std::uint32_t function{}; };
struct PciProperties { union { std::uint32_t Size; std::uint32_t size; }; std::uint8_t Version{}; union { PciAddress address; std::uint8_t data[16]; }; PciSpeed maxSpeed{}; bool resizable_bar_supported{}; bool resizable_bar_enabled{}; };
struct PciState { std::uint32_t Size{}; std::uint8_t Version{}; PciSpeed speed{}; };
struct GenericVoidDatatype { void* pData{}; std::uint32_t size{}; };
union OsDisplayEncoderIdentifier { std::uint32_t WindowsDisplayEncoderID{}; GenericVoidDatatype DisplayEncoderID; };
struct DisplayTiming { std::uint32_t Size{}; std::uint8_t Version{}; std::uint64_t PixelClock{}; std::uint32_t HActive{},VActive{},HTotal{},VTotal{},HBlank{},VBlank{},HSync{},VSync{}; float RefreshRate{}; std::uint32_t SignalStandard{}; std::uint8_t VicId{}; };
struct DisplayProperties { std::uint32_t Size{}; std::uint8_t Version{}; OsDisplayEncoderIdentifier Os_display_encoder_handle{}; std::uint32_t type{}; std::uint32_t attachedMux{}; std::uint32_t protocolConverterOutput{}; struct { std::uint8_t major_version{},minor_version{},revision_version{}; } supportedSpec{}; std::uint32_t supportedOutputBpcFlags{}; std::uint32_t protocolConverterType{}; std::uint32_t displayConfigFlags{}; std::uint32_t featureEnabledFlags{}; std::uint32_t featureSupportedFlags{}; std::uint32_t advancedFeatureEnabledFlags{}; std::uint32_t advancedFeatureSupportedFlags{}; DisplayTiming displayTiming{}; std::uint32_t reserved[16]{}; };
struct LiveState { std::uint32_t gfxApi{},targetFps{},framePacingStatus{},reserved[4]{}; };
struct FeatureRequest { std::uint32_t size{}; std::uint8_t version{}; std::uint32_t featureType{}; char* applicationName{}; std::int8_t applicationNameLength{}; bool set{}; std::uint32_t valueType{}; std::uint64_t value{}; std::int32_t customValueSize{}; void* customValue{}; };
static_assert(offsetof(PowerTelemetryV2, vramEnergyCounter) > offsetof(PowerTelemetryV2, gpuUtilizationLimited));
static_assert(offsetof(PowerTelemetryV2, psu) < offsetof(PowerTelemetryV2, fanSpeed));
static_assert(offsetof(PowerTelemetryV2, fanSpeed) < offsetof(PowerTelemetryV2, gpuVrTemp));
static_assert(sizeof(Item) == 24);
static_assert(offsetof(Item, value) == 16);
static_assert(offsetof(PowerTelemetryV2, totalCardEnergyCounter) == 376);
static_assert(offsetof(PowerTelemetryV2, psu) == 400);
static_assert(offsetof(PowerTelemetryV2, fanSpeed) == 680);
static_assert(offsetof(PowerTelemetryV2, gpuEffectiveClock) == 872);
static_assert(sizeof(PowerTelemetryV2) == 1016);
static_assert(offsetof(DeviceProperties, supported_subfunction_flags) > offsetof(DeviceProperties, device_type));
static_assert(offsetof(DeviceProperties, firmware_version) > offsetof(DeviceProperties, driver_version));
static_assert(offsetof(DeviceProperties, pci_vendor_id) > offsetof(DeviceProperties, firmware_version));
static_assert(offsetof(DeviceProperties, graphics_adapter_properties) > offsetof(DeviceProperties, name));
static_assert(offsetof(DeviceProperties, adapter_bdf) > offsetof(DeviceProperties, pci_subsys_vendor_id));
static_assert(sizeof(PciAddress) == 24);
static_assert(sizeof(OsDisplayEncoderIdentifier) == 16);
static_assert(offsetof(DisplayProperties, type) == 24);
static_assert(sizeof(DisplayProperties) == 200);
}
