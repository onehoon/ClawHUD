#include "DiagIntelVrrStateProbe.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <new>
#include <vector>

namespace
{
using Result = std::uint32_t;
using Handle = void*;
using InitFn = Result(__cdecl*)(void*, Handle*);
using CloseFn = Result(__cdecl*)(Handle);
using EnumDevicesFn = Result(__cdecl*)(Handle, std::uint32_t*, Handle*);
using EnumOutputsFn = Result(__cdecl*)(Handle, std::uint32_t*, Handle*);
using DevicePropertiesFn = Result(__cdecl*)(Handle, void*);
using DisplayPropertiesFn = Result(__cdecl*)(Handle, void*);
using ArcSyncInfoFn = Result(__cdecl*)(Handle, void*);
using ArcSyncProfileFn = Result(__cdecl*)(Handle, void*);

struct InitArgs
{
    std::uint32_t size{};
    std::uint8_t version{};
    std::uint32_t appVersion{};
    std::uint32_t flags{};
    std::uint32_t supportedVersion{};
    GUID uid{};
};

struct GenericVoidDatatypeAbi
{
    void* pData{};
    std::uint32_t size{};
};

// Full ctl_device_adapter_properties_t layout for the pinned IGCL ABI. The
// opaque tail keeps Size accurate while only the documented device-ID prefix
// is consumed here.
struct DevicePropertiesAbi
{
    std::uint32_t size{};
    std::uint8_t version{};
    std::uint8_t prefixPadding[3]{};
    void* pDeviceId{};
    std::uint32_t deviceIdSize{};
    std::array<std::uint8_t, 300> remainder{};
};

// Size/layout for the Windows display ID prefix and opaque remainder of the
// IGCL display-properties ABI; only the Windows ID is consumed.
struct DisplayPropertiesAbi
{
    std::uint32_t size{};
    std::uint8_t version{};
    std::uint8_t prefixPadding[3]{};
    union OsDisplayEncoder
    {
        std::uint32_t windowsDisplayEncoderId;
        GenericVoidDatatypeAbi otherPlatformDisplayEncoderId;
    } osDisplayEncoder{};
    std::array<std::uint8_t, 176> remainder{};
};

struct ArcSyncCapabilityAbi
{
    std::uint32_t size{};
    std::uint8_t version{};
    bool supported{};
    float minimumHz{};
    float maximumHz{};
    std::uint32_t maxFrameTimeIncreaseUs{};
    std::uint32_t maxFrameTimeDecreaseUs{};
};

struct ArcSyncProfileAbi
{
    std::uint32_t size{};
    std::uint8_t version{};
    std::uint32_t profile{};
    float maximumHz{};
    float minimumHz{};
    std::uint32_t maxFrameTimeIncreaseUs{};
    std::uint32_t maxFrameTimeDecreaseUs{};
};

static_assert(sizeof(InitArgs) == 36);
static_assert(sizeof(GenericVoidDatatypeAbi) == 16);
static_assert(offsetof(DevicePropertiesAbi, pDeviceId) == 8);
static_assert(offsetof(DevicePropertiesAbi, deviceIdSize) == 16);
static_assert(sizeof(DevicePropertiesAbi) == 320);
static_assert(offsetof(DisplayPropertiesAbi, osDisplayEncoder) == 8);
static_assert(offsetof(DisplayPropertiesAbi, remainder) == 24);
static_assert(sizeof(DisplayPropertiesAbi) == 200);
static_assert(sizeof(ArcSyncCapabilityAbi) == 24);
static_assert(sizeof(ArcSyncProfileAbi) == 28);

template<class T>
T Resolve(HMODULE module, const char* name) noexcept
{
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

template<class T>
bool Enumerate(Handle parent, Result(__cdecl* function)(Handle, std::uint32_t*, T*),
    std::vector<T>& values)
{
    std::uint32_t count{};
    if (function(parent, &count, nullptr) != 0)
    {
        values.clear();
        return false;
    }
    if (count == 0) { values.clear(); return true; }
    values.resize(count);
    const auto capacity = count;
    if (function(parent, &count, values.data()) != 0 || count > capacity)
    {
        values.clear();
        return false;
    }
    values.resize(count);
    return true;
}
}

struct DiagIntelVrrStateProbe::Endpoints
{
    InitFn init{};
    CloseFn close{};
    EnumDevicesFn enumerateDevices{};
    EnumOutputsFn enumerateOutputs{};
    DevicePropertiesFn getDeviceProperties{};
    DisplayPropertiesFn getDisplayProperties{};
    ArcSyncInfoFn getArcSyncInfo{};
    ArcSyncProfileFn getArcSyncProfile{};
};

DiagIgclTargetMatch ResolveDiagIgclTarget(const LUID& windowsTargetAdapterLuid,
    std::uint32_t windowsTargetId, std::span<const DiagIgclOutputIdentity> outputs,
    bool enumerationComplete) noexcept
{
    std::optional<std::size_t> match;
    for (std::size_t i = 0; i < outputs.size(); ++i)
    {
        const auto& adapterLuid = outputs[i].adapterLuid;
        if (adapterLuid.LowPart != windowsTargetAdapterLuid.LowPart ||
            adapterLuid.HighPart != windowsTargetAdapterLuid.HighPart ||
            outputs[i].targetId != windowsTargetId)
            continue;
        if (match) return { DiagIgclTargetMappingStatus::Ambiguous, std::nullopt };
        match = i;
    }
    if (!enumerationComplete || !match)
        return { DiagIgclTargetMappingStatus::Unknown, std::nullopt };
    return { DiagIgclTargetMappingStatus::Exact, match };
}

DiagIntelVrrStateProbe::~DiagIntelVrrStateProbe() { Shutdown(); }

bool DiagIntelVrrStateProbe::Initialize() noexcept
{
    Shutdown();
    library_ = LoadLibraryExW(L"ControlLib.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library_) return false;

    const auto library = static_cast<HMODULE>(library_);
    endpoints_ = new (std::nothrow) Endpoints{
        Resolve<InitFn>(library, "ctlInit"),
        Resolve<CloseFn>(library, "ctlClose"),
        Resolve<EnumDevicesFn>(library, "ctlEnumerateDevices"),
        Resolve<EnumOutputsFn>(library, "ctlEnumerateDisplayOutputs"),
        Resolve<DevicePropertiesFn>(library, "ctlGetDeviceProperties"),
        Resolve<DisplayPropertiesFn>(library, "ctlGetDisplayProperties"),
        Resolve<ArcSyncInfoFn>(library, "ctlGetIntelArcSyncInfoForMonitor"),
        Resolve<ArcSyncProfileFn>(library, "ctlGetIntelArcSyncProfile") };
    if (!endpoints_ || !endpoints_->init || !endpoints_->close ||
        !endpoints_->enumerateDevices || !endpoints_->enumerateOutputs ||
        !endpoints_->getDeviceProperties ||
        !endpoints_->getDisplayProperties || !endpoints_->getArcSyncInfo ||
        !endpoints_->getArcSyncProfile)
    {
        Shutdown();
        return false;
    }

    InitArgs args{};
    args.size = sizeof(args);
    args.appVersion = 0x00010000;
    if (endpoints_->init(&args, &apiHandle_) != 0 || !apiHandle_)
    {
        Shutdown();
        return false;
    }
    return true;
}

DiagIntelVrrState DiagIntelVrrStateProbe::Query(
    const LUID& windowsTargetAdapterLuid, std::uint32_t windowsTargetId) noexcept
{
    DiagIntelVrrState state;
    state.windowsTargetAdapterLuid = windowsTargetAdapterLuid;
    state.windowsTargetId = windowsTargetId;
    if (!apiHandle_ || !endpoints_) return state;

    try
    {
        if (!Enumerate<Handle>(apiHandle_, endpoints_->enumerateDevices, adapters_)) return state;
        struct Output
        {
            Handle handle{};
            DiagIgclOutputIdentity identity;
        };
        std::vector<Output> outputs;
        bool complete = true;
        for (const auto adapter : adapters_)
        {
            LUID adapterLuid{};
            DevicePropertiesAbi deviceProperties{};
            deviceProperties.size = sizeof(deviceProperties);
            deviceProperties.version = 0;
            deviceProperties.pDeviceId = &adapterLuid;
            deviceProperties.deviceIdSize = sizeof(adapterLuid);
            if (endpoints_->getDeviceProperties(adapter, &deviceProperties) != 0 ||
                deviceProperties.pDeviceId != &adapterLuid ||
                deviceProperties.deviceIdSize != sizeof(adapterLuid))
            {
                complete = false;
                continue;
            }

            std::vector<Handle> adapterOutputs;
            if (!Enumerate<Handle>(adapter, endpoints_->enumerateOutputs, adapterOutputs))
            {
                complete = false;
                continue;
            }
            for (const auto output : adapterOutputs)
            {
                DisplayPropertiesAbi properties{};
                properties.size = sizeof(properties);
                properties.version = 1;
                if (endpoints_->getDisplayProperties(output, &properties) != 0)
                {
                    complete = false;
                    continue;
                }
                outputs.push_back({ output, { adapterLuid,
                    properties.osDisplayEncoder.windowsDisplayEncoderId } });
            }
        }

        std::vector<DiagIgclOutputIdentity> identities;
        identities.reserve(outputs.size());
        for (const auto& output : outputs) identities.push_back(output.identity);
        const auto match = ResolveDiagIgclTarget(windowsTargetAdapterLuid,
            windowsTargetId, identities, complete);
        state.mappingStatus = match.status;
        if (match.status != DiagIgclTargetMappingStatus::Exact || !match.outputIndex)
            return state;

        const auto output = outputs[*match.outputIndex].handle;
        ArcSyncCapabilityAbi capability{};
        capability.size = sizeof(capability);
        const auto capabilityResult = endpoints_->getArcSyncInfo(output, &capability);
        state.capabilityResult = capabilityResult;
        if (capabilityResult == 0)
            state.capability = DiagArcSyncCapability{ capability.supported,
                capability.minimumHz, capability.maximumHz,
                capability.maxFrameTimeIncreaseUs, capability.maxFrameTimeDecreaseUs };

        ArcSyncProfileAbi profile{};
        profile.size = sizeof(profile);
        const auto profileResult = endpoints_->getArcSyncProfile(output, &profile);
        state.profileResult = profileResult;
        if (profileResult == 0)
            state.profile = DiagArcSyncProfile{ profile.profile, profile.maximumHz,
                profile.minimumHz, profile.maxFrameTimeIncreaseUs,
                profile.maxFrameTimeDecreaseUs };
        return state;
    }
    catch (...)
    {
        state.mappingStatus = DiagIgclTargetMappingStatus::Unknown;
        state.capability.reset();
        state.profile.reset();
        return state;
    }
}

void DiagIntelVrrStateProbe::Shutdown() noexcept
{
    if (apiHandle_ && endpoints_ && endpoints_->close)
        endpoints_->close(apiHandle_);
    apiHandle_ = nullptr;
    adapters_.clear();
    delete endpoints_;
    endpoints_ = nullptr;
    if (library_) FreeLibrary(static_cast<HMODULE>(library_));
    library_ = nullptr;
}
