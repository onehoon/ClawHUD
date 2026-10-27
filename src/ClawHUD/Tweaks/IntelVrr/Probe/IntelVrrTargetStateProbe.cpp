#include "IntelVrrTargetStateProbe.h"
#include "IntelVrrTargetStateProbeAbi.h"

#include <windows.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <optional>
#include <string_view>
#include <vector>

namespace clawhud
{
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
static_assert(offsetof(DevicePropertiesAbi, pDeviceId) == 8);
static_assert(offsetof(DevicePropertiesAbi, deviceIdSize) == 16);
static_assert(sizeof(DevicePropertiesAbi) == 320);
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
    const auto countResult = function(parent, &count, nullptr);
    if (countResult != 0)
    {
        values.clear();
        return false;
    }
    if (count == 0) { values.clear(); return true; }
    values.resize(count);
    const auto capacity = count;
    const auto valuesResult = function(parent, &count, values.data());
    if (valuesResult != 0)
    {
        values.clear();
        return false;
    }
    if (count > capacity)
    {
        values.clear();
        return false;
    }
    values.resize(count);
    return true;
}

std::uint32_t Count32(std::size_t count) noexcept
{
    return count > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(count);
}
}

struct IntelVrrTargetStateProbe::Endpoints
{
    InitFn init{};
    CloseFn close{};
    EnumDevicesFn enumerateDevices{};
    EnumOutputsFn enumerateOutputs{};
    DevicePropertiesFn getDeviceProperties{};
    DisplayPropertiesFn getDisplayProperties{};
    ArcSyncInfoFn getArcSyncInfo{};
    ArcSyncProfileFn getArcSyncProfile{};
    ArcSyncProfileFn setArcSyncProfile{};
};

IntelVrrTargetMatch ResolveIntelVrrTarget(const LUID& windowsTargetAdapterLuid,
    std::uint32_t windowsTargetId, std::span<const IntelVrrOutputIdentity> outputs,
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
        if (match) return { IntelVrrTargetMappingStatus::Ambiguous, std::nullopt };
        match = i;
    }
    if (!enumerationComplete || !match)
        return { IntelVrrTargetMappingStatus::Unknown, std::nullopt };
    return { IntelVrrTargetMappingStatus::Exact, match };
}

IntelVrrTargetStateProbe::~IntelVrrTargetStateProbe() { Shutdown(); }

bool IntelVrrTargetStateProbe::Initialize() noexcept
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
        Resolve<ArcSyncProfileFn>(library, "ctlGetIntelArcSyncProfile"),
        Resolve<ArcSyncProfileFn>(library, "ctlSetIntelArcSyncProfile") };
    if (!endpoints_)
    {
        Shutdown();
        return false;
    }

    std::string_view missingFunction;
    if (!endpoints_->init) missingFunction = "ctlInit";
    else if (!endpoints_->close) missingFunction = "ctlClose";
    else if (!endpoints_->enumerateDevices) missingFunction = "ctlEnumerateDevices";
    else if (!endpoints_->enumerateOutputs) missingFunction = "ctlEnumerateDisplayOutputs";
    else if (!endpoints_->getDeviceProperties) missingFunction = "ctlGetDeviceProperties";
    else if (!endpoints_->getDisplayProperties) missingFunction = "ctlGetDisplayProperties";
    else if (!endpoints_->getArcSyncInfo) missingFunction = "ctlGetIntelArcSyncInfoForMonitor";
    else if (!endpoints_->getArcSyncProfile) missingFunction = "ctlGetIntelArcSyncProfile";
    if (!missingFunction.empty())
    {
        Shutdown();
        return false;
    }

    InitArgs args{};
    args.size = sizeof(args);
    args.appVersion = 0x00010000;
    const auto initResult = endpoints_->init(&args, &apiHandle_);
    if (initResult != 0)
    {
        Shutdown();
        return false;
    }
    if (!apiHandle_)
    {
        Shutdown();
        return false;
    }
    return true;
}

IntelVrrTargetState IntelVrrTargetStateProbe::Query(
    const LUID& windowsTargetAdapterLuid, std::uint32_t windowsTargetId) noexcept
{
    exactOutput_ = nullptr;
    IntelVrrTargetState state;
    state.windowsTargetAdapterLuid = windowsTargetAdapterLuid;
    state.windowsTargetId = windowsTargetId;
    state.initialized = apiHandle_ && endpoints_;
    if (!state.initialized) return state;

    try
    {
        state.enumerationComplete = true;
        adapters_.clear();
        const bool deviceEnumerationSucceeded = Enumerate<Handle>(apiHandle_,
            endpoints_->enumerateDevices, adapters_);
        if (!deviceEnumerationSucceeded)
        {
            state.enumerationComplete = false;
            return state;
        }
        state.adapterCount = Count32(adapters_.size());
        struct Output
        {
            Handle handle{};
            IntelVrrOutputIdentity identity;
        };
        std::vector<Output> outputs;
        bool complete = true;
        for (std::size_t adapterIndex = 0; adapterIndex < adapters_.size(); ++adapterIndex)
        {
            const auto adapter = adapters_[adapterIndex];
            LUID adapterLuid{};
            DevicePropertiesAbi deviceProperties{};
            deviceProperties.size = sizeof(deviceProperties);
            deviceProperties.version = 0;
            deviceProperties.pDeviceId = &adapterLuid;
            deviceProperties.deviceIdSize = sizeof(adapterLuid);
            const auto devicePropertiesResult = endpoints_->getDeviceProperties(
                adapter, &deviceProperties);
            if (devicePropertiesResult != 0)
            {
                complete = false;
                continue;
            }
            if (deviceProperties.pDeviceId != &adapterLuid ||
                deviceProperties.deviceIdSize != sizeof(adapterLuid))
            {
                complete = false;
                continue;
            }

            std::vector<Handle> adapterOutputs;
            const bool outputEnumerationSucceeded = Enumerate<Handle>(
                adapter, endpoints_->enumerateOutputs, adapterOutputs);
            if (!outputEnumerationSucceeded)
            {
                complete = false;
                continue;
            }
            state.displayOutputCount = Count32(
                static_cast<std::size_t>(state.displayOutputCount) + adapterOutputs.size());
            for (std::size_t outputIndex = 0; outputIndex < adapterOutputs.size(); ++outputIndex)
            {
                const auto output = adapterOutputs[outputIndex];
                intel_vrr_igcl_abi::DisplayProperties properties{};
                intel_vrr_igcl_abi::InitializeDisplayProperties(properties);
                const auto displayPropertiesResult = endpoints_->getDisplayProperties(
                    output, &properties);
                if (displayPropertiesResult != 0)
                {
                    complete = false;
                    continue;
                }
                ++state.displayPropertiesSuccessCount;
                outputs.push_back({ output, { adapterLuid,
                    properties.osDisplayEncoder.windowsDisplayEncoderId } });
            }
        }

        std::vector<IntelVrrOutputIdentity> identities;
        identities.reserve(outputs.size());
        for (const auto& output : outputs) identities.push_back(output.identity);
        for (const auto& identity : identities)
        {
            if (identity.adapterLuid.LowPart == windowsTargetAdapterLuid.LowPart &&
                identity.adapterLuid.HighPart == windowsTargetAdapterLuid.HighPart &&
                identity.targetId == windowsTargetId)
                ++state.targetMatchCount;
        }
        state.enumerationComplete = complete;
        const auto match = ResolveIntelVrrTarget(windowsTargetAdapterLuid,
            windowsTargetId, identities, complete);
        state.mappingStatus = match.status;
        if (match.status != IntelVrrTargetMappingStatus::Exact || !match.outputIndex)
            return state;

        const auto output = outputs[*match.outputIndex].handle;
        ArcSyncCapabilityAbi capability{};
        capability.size = sizeof(capability);
        const auto capabilityResult = endpoints_->getArcSyncInfo(output, &capability);
        state.capabilityResult = capabilityResult;
        if (capabilityResult == 0)
            state.capability = IntelVrrArcSyncCapability{ capability.supported,
                capability.minimumHz, capability.maximumHz,
                capability.maxFrameTimeIncreaseUs, capability.maxFrameTimeDecreaseUs };
        ArcSyncProfileAbi profile{};
        profile.size = sizeof(profile);
        const auto profileResult = endpoints_->getArcSyncProfile(output, &profile);
        state.profileResult = profileResult;
        if (profileResult == 0)
            state.profile = IntelVrrArcSyncProfile{ profile.profile, profile.maximumHz,
                profile.minimumHz, profile.maxFrameTimeIncreaseUs,
                profile.maxFrameTimeDecreaseUs };
        if (capabilityResult == 0 && profileResult == 0)
        {
            exactOutput_ = output;
            exactOutputAdapterLuid_ = windowsTargetAdapterLuid;
            exactOutputTargetId_ = windowsTargetId;
        }
        return state;
    }
    catch (...)
    {
        exactOutput_ = nullptr;
        state.mappingStatus = IntelVrrTargetMappingStatus::Unknown;
        state.capability.reset();
        state.profile.reset();
        state.enumerationComplete = false;
        return state;
    }
}

IntelVrrProfileSetResult IntelVrrTargetStateProbe::SetProfileForExactTarget(
    const IntelVrrTargetState& expected, std::uint32_t requestedProfile) noexcept
{
    constexpr std::uint32_t recommended = 1;
    constexpr std::uint32_t excellent = 2;
    constexpr float capabilityMinimumHz = 48.0f;
    constexpr float capabilityMaximumHz = 180.0f;
    constexpr float toleranceHz = 0.25f;
    const auto reject = [](std::string_view detail,
        std::optional<std::uint32_t> result = std::nullopt)
    {
        return IntelVrrProfileSetResult{ false, false, result, detail };
    };

    const auto output = exactOutput_;
    exactOutput_ = nullptr;
    if (!apiHandle_ || !endpoints_ || !endpoints_->setArcSyncProfile || !output)
        return reject("IGCL profile setter is unavailable");
    if ((requestedProfile != excellent && requestedProfile != recommended) ||
        !expected.enumerationComplete ||
        expected.mappingStatus != IntelVrrTargetMappingStatus::Exact ||
        expected.windowsTargetAdapterLuid.LowPart != exactOutputAdapterLuid_.LowPart ||
        expected.windowsTargetAdapterLuid.HighPart != exactOutputAdapterLuid_.HighPart ||
        expected.windowsTargetId != exactOutputTargetId_ ||
        expected.targetMatchCount != 1 || !expected.capability || !expected.profile ||
        expected.capabilityResult != 0 || expected.profileResult != 0 ||
        !expected.capability->supported ||
        !std::isfinite(expected.capability->minimumHz) ||
        !std::isfinite(expected.capability->maximumHz) ||
        std::abs(expected.capability->minimumHz - capabilityMinimumHz) > toleranceHz ||
        std::abs(expected.capability->maximumHz - capabilityMaximumHz) > toleranceHz ||
        !std::isfinite(expected.profile->minimumHz) ||
        !std::isfinite(expected.profile->maximumHz))
        return reject("expected state is not an exact supported LG profile target");

    const auto expectedCurrentProfile = requestedProfile == excellent
        ? recommended : excellent;
    if (expected.profile->profile != expectedCurrentProfile)
        return reject("current profile is not eligible for the requested change");

    try
    {
        ArcSyncCapabilityAbi capability{};
        capability.size = sizeof(capability);
        const auto capabilityResult = endpoints_->getArcSyncInfo(output, &capability);
        if (capabilityResult != 0)
            return reject("ctlGetIntelArcSyncInfoForMonitor failed", capabilityResult);
        ArcSyncProfileAbi current{};
        current.size = sizeof(current);
        const auto profileResult = endpoints_->getArcSyncProfile(output, &current);
        if (profileResult != 0)
            return reject("ctlGetIntelArcSyncProfile failed", profileResult);
        if (!capability.supported ||
            !std::isfinite(capability.minimumHz) ||
            !std::isfinite(capability.maximumHz) ||
            !std::isfinite(current.minimumHz) ||
            !std::isfinite(current.maximumHz) ||
            std::abs(capability.minimumHz - expected.capability->minimumHz) > 0.01f ||
            std::abs(capability.maximumHz - expected.capability->maximumHz) > 0.01f ||
            current.profile != expected.profile->profile ||
            std::abs(current.minimumHz - expected.profile->minimumHz) > 0.01f ||
            std::abs(current.maximumHz - expected.profile->maximumHz) > 0.01f)
            return reject("IGCL state changed after confirmation; no profile was written");

        ArcSyncProfileAbi requested{};
        requested.size = sizeof(requested);
        requested.version = 0;
        requested.profile = requestedProfile;
        const auto setResult = endpoints_->setArcSyncProfile(output, &requested);
        return { true, setResult == 0, setResult,
            setResult == 0 ? std::string_view{} : std::string_view("ctlSetIntelArcSyncProfile") };
    }
    catch (...)
    {
        return reject("exception while applying the exact-target IGCL profile");
    }
}

void IntelVrrTargetStateProbe::Shutdown() noexcept
{
    if (apiHandle_ && endpoints_ && endpoints_->close)
        endpoints_->close(apiHandle_);
    apiHandle_ = nullptr;
    adapters_.clear();
    exactOutput_ = nullptr;
    exactOutputAdapterLuid_ = {};
    exactOutputTargetId_ = 0;
    delete endpoints_;
    endpoints_ = nullptr;
    if (library_) FreeLibrary(static_cast<HMODULE>(library_));
    library_ = nullptr;
}
}
