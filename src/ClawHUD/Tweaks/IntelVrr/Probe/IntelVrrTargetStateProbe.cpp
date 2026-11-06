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
struct EnumerationOutcome
{
    bool success{};
    std::optional<Result> result;
    std::string_view detail;
};

template<class T>
EnumerationOutcome<T> Enumerate(Handle parent, Result(__cdecl* function)(Handle, std::uint32_t*, T*),
    std::vector<T>& values)
{
    std::uint32_t count{};
    const auto countResult = function(parent, &count, nullptr);
    if (countResult != 0)
    {
        values.clear();
        return { false, countResult, "count query" };
    }
    if (count == 0) { values.clear(); return { true, std::nullopt, {} }; }
    values.resize(count);
    const auto capacity = count;
    const auto valuesResult = function(parent, &count, values.data());
    if (valuesResult != 0)
    {
        values.clear();
        return { false, valuesResult, "handle query" };
    }
    if (count > capacity)
    {
        values.clear();
        return { false, std::nullopt, "returned count exceeds buffer capacity" };
    }
    values.resize(count);
    return { true, std::nullopt, {} };
}

std::uint32_t Count32(std::size_t count) noexcept
{
    return count > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(count);
}
}

bool detail::HasExactIntelVrrDeviceId(const void* expectedPointer,
    std::size_t expectedSize, const void* returnedPointer,
    std::uint32_t returnedSize) noexcept
{
    return returnedPointer == expectedPointer && returnedSize == expectedSize;
}

void detail::RecordIntelVrrProbeFailure(IntelVrrTargetState& state,
    IntelVrrProbeFailureStage stage, std::string_view detail,
    IntelVrrProbeResultDomain domain, std::optional<std::uint32_t> result,
    std::size_t adapterIndex, std::size_t outputIndex) noexcept
{
    if (state.failureRecordCount < state.failures.size())
    {
        state.failures[state.failureRecordCount++] = {
            stage, domain, result, detail, adapterIndex, outputIndex };
    }
    else if (state.suppressedFailureCount < std::numeric_limits<std::uint32_t>::max())
    {
        ++state.suppressedFailureCount;
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
    attempted_ = true;
    initializationFailure_ = {};
    library_ = LoadLibraryExW(L"ControlLib.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library_)
    {
        initializationFailure_ = { IntelVrrProbeFailureStage::LoadLibrary,
            IntelVrrProbeResultDomain::Win32, GetLastError(), "ControlLib.dll" };
        return false;
    }

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
        initializationFailure_ = { IntelVrrProbeFailureStage::InternalError,
            IntelVrrProbeResultDomain::None, std::nullopt,
            "allocate IGCL API endpoint table" };
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
        initializationFailure_ = { IntelVrrProbeFailureStage::ResolveFunction,
            IntelVrrProbeResultDomain::None, std::nullopt, missingFunction };
        Shutdown();
        return false;
    }

    InitArgs args{};
    args.size = sizeof(args);
    args.appVersion = 0x00010000;
    const auto initResult = endpoints_->init(&args, &apiHandle_);
    if (initResult != 0)
    {
        initializationFailure_ = { IntelVrrProbeFailureStage::Initialize,
            IntelVrrProbeResultDomain::ControlLibrary, initResult, "ctlInit" };
        Shutdown();
        return false;
    }
    if (!apiHandle_)
    {
        initializationFailure_ = { IntelVrrProbeFailureStage::Initialize,
            IntelVrrProbeResultDomain::None, std::nullopt,
            "ctlInit returned success with a null API handle" };
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
    state.attempted = attempted_;
    state.initialized = apiHandle_ && endpoints_;
    if (!state.initialized)
    {
        if (initializationFailure_.stage != IntelVrrProbeFailureStage::None)
            detail::RecordIntelVrrProbeFailure(state, initializationFailure_.stage, initializationFailure_.detail,
                initializationFailure_.resultDomain, initializationFailure_.result,
                initializationFailure_.adapterIndex, initializationFailure_.outputIndex);
        else if (attempted_)
            detail::RecordIntelVrrProbeFailure(state, IntelVrrProbeFailureStage::NotInitialized,
                "IGCL API is not initialized");
        return state;
    }

    try
    {
        state.enumerationComplete = true;
        adapters_.clear();
        const auto deviceEnumeration = Enumerate<Handle>(apiHandle_,
            endpoints_->enumerateDevices, adapters_);
        if (!deviceEnumeration.success)
        {
            state.enumerationComplete = false;
            detail::RecordIntelVrrProbeFailure(state, IntelVrrProbeFailureStage::EnumerateDevices,
                deviceEnumeration.detail, IntelVrrProbeResultDomain::ControlLibrary,
                deviceEnumeration.result);
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
                detail::RecordIntelVrrProbeFailure(state, IntelVrrProbeFailureStage::GetDeviceProperties,
                    "ctlGetDeviceProperties", IntelVrrProbeResultDomain::ControlLibrary,
                    devicePropertiesResult, adapterIndex);
                continue;
            }
            if (!detail::HasExactIntelVrrDeviceId(&adapterLuid, sizeof(adapterLuid),
                deviceProperties.pDeviceId, deviceProperties.deviceIdSize))
            {
                complete = false;
                detail::RecordIntelVrrProbeFailure(state, IntelVrrProbeFailureStage::GetDeviceProperties,
                    "unexpected device ID pointer or size", IntelVrrProbeResultDomain::None,
                    std::nullopt, adapterIndex);
                continue;
            }

            std::vector<Handle> adapterOutputs;
            const auto outputEnumeration = Enumerate<Handle>(
                adapter, endpoints_->enumerateOutputs, adapterOutputs);
            if (!outputEnumeration.success)
            {
                complete = false;
                detail::RecordIntelVrrProbeFailure(state, IntelVrrProbeFailureStage::EnumerateDisplayOutputs,
                    outputEnumeration.detail, IntelVrrProbeResultDomain::ControlLibrary,
                    outputEnumeration.result, adapterIndex);
                continue;
            }
            const auto firstOutputIndex = static_cast<std::size_t>(state.displayOutputCount);
            state.displayOutputCount = Count32(firstOutputIndex + adapterOutputs.size());
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
                    detail::RecordIntelVrrProbeFailure(state, IntelVrrProbeFailureStage::GetDisplayProperties,
                        "ctlGetDisplayProperties", IntelVrrProbeResultDomain::ControlLibrary,
                        displayPropertiesResult, adapterIndex, firstOutputIndex + outputIndex);
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
        {
            if (match.status == IntelVrrTargetMappingStatus::Ambiguous)
                detail::RecordIntelVrrProbeFailure(state, IntelVrrProbeFailureStage::TargetMapping,
                    "multiple exact adapter LUID and target ID matches");
            else if (complete)
                detail::RecordIntelVrrProbeFailure(state, IntelVrrProbeFailureStage::TargetMapping,
                    "no exact adapter LUID and target ID match");
            return state;
        }

        const auto output = outputs[*match.outputIndex].handle;
        ArcSyncCapabilityAbi capability{};
        capability.size = sizeof(capability);
        const auto capabilityResult = endpoints_->getArcSyncInfo(output, &capability);
        state.capabilityResult = capabilityResult;
        if (capabilityResult == 0)
            state.capability = IntelVrrArcSyncCapability{ capability.supported,
                capability.minimumHz, capability.maximumHz,
                capability.maxFrameTimeIncreaseUs, capability.maxFrameTimeDecreaseUs };
        else
            detail::RecordIntelVrrProbeFailure(state, IntelVrrProbeFailureStage::GetArcSyncInfo,
                "ctlGetIntelArcSyncInfoForMonitor",
                IntelVrrProbeResultDomain::ControlLibrary, capabilityResult);

        ArcSyncProfileAbi profile{};
        profile.size = sizeof(profile);
        const auto profileResult = endpoints_->getArcSyncProfile(output, &profile);
        state.profileResult = profileResult;
        if (profileResult == 0)
            state.profile = IntelVrrArcSyncProfile{ profile.profile, profile.maximumHz,
                profile.minimumHz, profile.maxFrameTimeIncreaseUs,
                profile.maxFrameTimeDecreaseUs };
        else
            detail::RecordIntelVrrProbeFailure(state, IntelVrrProbeFailureStage::GetArcSyncProfile,
                "ctlGetIntelArcSyncProfile",
                IntelVrrProbeResultDomain::ControlLibrary, profileResult);
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
        detail::RecordIntelVrrProbeFailure(state, IntelVrrProbeFailureStage::InternalError,
            "exception during IGCL query or output enumeration");
        return state;
    }
}

bool IsIntelVrrProfileChangeAllowed(const IntelVrrTargetState& state,
    IntelVrrPanelClass panelClass, std::uint32_t requestedProfile,
    const clawhud::Tma2027Evidence* tmaEvidence) noexcept
{
    constexpr std::uint32_t recommended = 1;
    constexpr std::uint32_t excellent = 2;
    constexpr std::uint32_t good = 3;
    constexpr std::uint32_t compatible = 4;
    constexpr std::uint32_t vesa = 6;
    constexpr float toleranceHz = 0.25f;
    const float expectedMaximum = panelClass == IntelVrrPanelClass::Lg27gs65f
        ? 180.0f : 120.0f;
    const auto isNear = [=](float value, float expected)
    {
        return std::isfinite(value) && std::abs(value - expected) <= toleranceHz;
    };

    if (!state.initialized || !state.enumerationComplete ||
        state.mappingStatus != IntelVrrTargetMappingStatus::Exact ||
        state.targetMatchCount != 1 || state.displayOutputCount == 0 ||
        state.displayPropertiesSuccessCount != state.displayOutputCount ||
        !state.capability || !state.profile || state.capabilityResult != 0 ||
        state.profileResult != 0 || !state.capability->supported ||
        !std::isfinite(state.capability->minimumHz) ||
        !std::isfinite(state.capability->maximumHz) ||
        !std::isfinite(state.profile->minimumHz) ||
        !std::isfinite(state.profile->maximumHz))
        return false;

    if (panelClass == IntelVrrPanelClass::Claw7Internal)
    {
        if (requestedProfile != excellent || !tmaEvidence ||
            tmaEvidence->target.adapterLuidLowPart !=
                state.windowsTargetAdapterLuid.LowPart ||
            tmaEvidence->target.adapterLuidHighPart !=
                state.windowsTargetAdapterLuid.HighPart ||
            tmaEvidence->target.targetId != state.windowsTargetId ||
            !clawhud::IsTma2027VerifiedPhysicalEdidEvidence(*tmaEvidence,
                tmaEvidence->target))
            return false;
        const bool halfFloor = isNear(state.capability->minimumHz, 24.0f) &&
            isNear(state.capability->maximumHz, 120.0f);
        const clawhud::Tma2027ProfileState tmaState{
            true, state.capability->supported, state.capability->minimumHz,
            state.capability->maximumHz, true, state.profile->profile,
            state.profile->minimumHz, state.profile->maximumHz,
            tmaEvidence->target };
        return clawhud::IsTma2027ExcellentSetterAllowed(tmaState,
            tmaEvidence);
    }

    if (!isNear(state.capability->minimumHz, 48.0f) ||
        !isNear(state.capability->maximumHz, expectedMaximum))
        return false;

    if (panelClass == IntelVrrPanelClass::Lg27gs65f)
    {
        if (requestedProfile == excellent)
            return (state.profile->profile == recommended ||
                state.profile->profile == compatible) &&
                isNear(state.profile->minimumHz, 90.0f) &&
                isNear(state.profile->maximumHz, 180.0f);
        return (requestedProfile == recommended || requestedProfile == compatible) &&
            state.profile->profile == excellent;
    }

    if (requestedProfile != excellent)
        return false;
    const bool knownProfile = state.profile->profile == recommended ||
        state.profile->profile == excellent || state.profile->profile == good ||
        state.profile->profile == compatible || state.profile->profile == vesa;
    const bool fullRange = isNear(state.profile->minimumHz, 48.0f) &&
        isNear(state.profile->maximumHz, 120.0f);
    return knownProfile && !fullRange;
}

bool detail::IsIntelVrrFreshStateMatch(
    const IntelVrrTargetState& expected,
    const IntelVrrArcSyncCapability& capability,
    const IntelVrrArcSyncProfile& profile) noexcept
{
    if (!expected.capability || !expected.profile || !capability.supported ||
        !std::isfinite(capability.minimumHz) ||
        !std::isfinite(capability.maximumHz) ||
        !std::isfinite(profile.minimumHz) ||
        !std::isfinite(profile.maximumHz))
        return false;

    return std::abs(capability.minimumHz - expected.capability->minimumHz) <= 0.01f &&
        std::abs(capability.maximumHz - expected.capability->maximumHz) <= 0.01f &&
        profile.profile == expected.profile->profile &&
        std::abs(profile.minimumHz - expected.profile->minimumHz) <= 0.01f &&
        std::abs(profile.maximumHz - expected.profile->maximumHz) <= 0.01f;
}

IntelVrrProfileSetResult IntelVrrTargetStateProbe::SetProfileForExactTarget(
    const IntelVrrTargetState& expected, IntelVrrPanelClass panelClass,
    std::uint32_t requestedProfile,
    const clawhud::Tma2027Evidence* tmaEvidence) noexcept
{
    const auto reject = [](std::string_view detail,
        std::optional<std::uint32_t> result = std::nullopt)
    {
        return IntelVrrProfileSetResult{ false, false, result, detail };
    };

    const auto output = exactOutput_;
    exactOutput_ = nullptr;
    if (!apiHandle_ || !endpoints_ || !endpoints_->setArcSyncProfile || !output)
        return reject("IGCL profile setter is unavailable");
    if (expected.mappingStatus != IntelVrrTargetMappingStatus::Exact ||
        expected.windowsTargetAdapterLuid.LowPart != exactOutputAdapterLuid_.LowPart ||
        expected.windowsTargetAdapterLuid.HighPart != exactOutputAdapterLuid_.HighPart ||
        expected.windowsTargetId != exactOutputTargetId_ ||
        !IsIntelVrrProfileChangeAllowed(expected, panelClass, requestedProfile,
            tmaEvidence))
        return reject("expected state does not match the exact supported panel policy");

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
        const IntelVrrArcSyncCapability currentCapability{
            capability.supported, capability.minimumHz, capability.maximumHz,
            capability.maxFrameTimeIncreaseUs, capability.maxFrameTimeDecreaseUs };
        const IntelVrrArcSyncProfile currentProfile{
            current.profile, current.maximumHz, current.minimumHz,
            current.maxFrameTimeIncreaseUs, current.maxFrameTimeDecreaseUs };
        std::uint32_t setResult{};
        const bool attempted = detail::SetIntelVrrProfileIfFresh(expected,
            currentCapability, currentProfile, [&]
            {
                ArcSyncProfileAbi requested{};
                requested.size = sizeof(requested);
                requested.version = 0;
                requested.profile = requestedProfile;
                setResult = endpoints_->setArcSyncProfile(output, &requested);
            });
        if (!attempted)
            return reject("IGCL state changed after confirmation; no profile was written");
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

} // namespace clawhud
