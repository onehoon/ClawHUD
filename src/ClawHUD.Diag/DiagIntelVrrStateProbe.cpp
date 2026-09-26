#include "DiagIntelVrrStateProbe.h"
#include "DiagIntelVrrStateProbeAbi.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <optional>
#include <string_view>
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

void AddFailure(DiagIntelVrrState& state, DiagIgclProbeFailureStage stage,
    std::string_view detail, DiagIgclProbeResultDomain domain = DiagIgclProbeResultDomain::None,
    std::optional<std::uint32_t> result = std::nullopt,
    std::size_t adapterIndex = DiagIgclProbeFailure::NoIndex,
    std::size_t outputIndex = DiagIgclProbeFailure::NoIndex) noexcept
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

std::uint32_t Count32(std::size_t count) noexcept
{
    return count > std::numeric_limits<std::uint32_t>::max()
        ? std::numeric_limits<std::uint32_t>::max()
        : static_cast<std::uint32_t>(count);
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
    attempted_ = true;
    initializationFailure_ = {};
    library_ = LoadLibraryExW(L"ControlLib.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library_)
    {
        initializationFailure_ = { DiagIgclProbeFailureStage::LoadLibrary,
            DiagIgclProbeResultDomain::Win32, GetLastError(), "ControlLib.dll" };
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
        Resolve<ArcSyncProfileFn>(library, "ctlGetIntelArcSyncProfile") };
    if (!endpoints_)
    {
        initializationFailure_ = { DiagIgclProbeFailureStage::InternalError,
            DiagIgclProbeResultDomain::None, std::nullopt,
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
        initializationFailure_ = { DiagIgclProbeFailureStage::ResolveFunction,
            DiagIgclProbeResultDomain::None, std::nullopt, missingFunction };
        Shutdown();
        return false;
    }

    InitArgs args{};
    args.size = sizeof(args);
    args.appVersion = 0x00010000;
    const auto initResult = endpoints_->init(&args, &apiHandle_);
    if (initResult != 0)
    {
        initializationFailure_ = { DiagIgclProbeFailureStage::Initialize,
            DiagIgclProbeResultDomain::ControlLibrary, initResult, "ctlInit" };
        Shutdown();
        return false;
    }
    if (!apiHandle_)
    {
        initializationFailure_ = { DiagIgclProbeFailureStage::Initialize,
            DiagIgclProbeResultDomain::None, std::nullopt,
            "ctlInit returned success with a null API handle" };
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
    state.attempted = attempted_;
    state.initialized = apiHandle_ && endpoints_;
    if (!state.initialized)
    {
        if (initializationFailure_.stage != DiagIgclProbeFailureStage::None)
            AddFailure(state, initializationFailure_.stage, initializationFailure_.detail,
                initializationFailure_.resultDomain, initializationFailure_.result,
                initializationFailure_.adapterIndex, initializationFailure_.outputIndex);
        else if (attempted_)
            AddFailure(state, DiagIgclProbeFailureStage::NotInitialized,
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
            AddFailure(state, DiagIgclProbeFailureStage::EnumerateDevices,
                deviceEnumeration.detail, DiagIgclProbeResultDomain::ControlLibrary,
                deviceEnumeration.result);
            return state;
        }
        state.adapterCount = Count32(adapters_.size());
        struct Output
        {
            Handle handle{};
            DiagIgclOutputIdentity identity;
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
                AddFailure(state, DiagIgclProbeFailureStage::GetDeviceProperties,
                    "ctlGetDeviceProperties", DiagIgclProbeResultDomain::ControlLibrary,
                    devicePropertiesResult, adapterIndex);
                continue;
            }
            if (deviceProperties.pDeviceId != &adapterLuid ||
                deviceProperties.deviceIdSize != sizeof(adapterLuid))
            {
                complete = false;
                AddFailure(state, DiagIgclProbeFailureStage::GetDeviceProperties,
                    "unexpected device ID pointer or size", DiagIgclProbeResultDomain::None,
                    std::nullopt, adapterIndex);
                continue;
            }

            std::vector<Handle> adapterOutputs;
            const auto outputEnumeration = Enumerate<Handle>(
                adapter, endpoints_->enumerateOutputs, adapterOutputs);
            if (!outputEnumeration.success)
            {
                complete = false;
                AddFailure(state, DiagIgclProbeFailureStage::EnumerateDisplayOutputs,
                    outputEnumeration.detail, DiagIgclProbeResultDomain::ControlLibrary,
                    outputEnumeration.result, adapterIndex);
                continue;
            }
            const auto firstOutputIndex = static_cast<std::size_t>(state.displayOutputCount);
            state.displayOutputCount = Count32(firstOutputIndex + adapterOutputs.size());
            for (std::size_t outputIndex = 0; outputIndex < adapterOutputs.size(); ++outputIndex)
            {
                const auto output = adapterOutputs[outputIndex];
                diag_igcl_abi::DisplayProperties properties{};
                diag_igcl_abi::InitializeDisplayProperties(properties);
                const auto displayPropertiesResult = endpoints_->getDisplayProperties(
                    output, &properties);
                if (displayPropertiesResult != 0)
                {
                    complete = false;
                    AddFailure(state, DiagIgclProbeFailureStage::GetDisplayProperties,
                        "ctlGetDisplayProperties", DiagIgclProbeResultDomain::ControlLibrary,
                        displayPropertiesResult, adapterIndex, firstOutputIndex + outputIndex);
                    continue;
                }
                ++state.displayPropertiesSuccessCount;
                outputs.push_back({ output, { adapterLuid,
                    properties.osDisplayEncoder.windowsDisplayEncoderId } });
            }
        }

        std::vector<DiagIgclOutputIdentity> identities;
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
        const auto match = ResolveDiagIgclTarget(windowsTargetAdapterLuid,
            windowsTargetId, identities, complete);
        state.mappingStatus = match.status;
        if (match.status != DiagIgclTargetMappingStatus::Exact || !match.outputIndex)
        {
            if (match.status == DiagIgclTargetMappingStatus::Ambiguous)
                AddFailure(state, DiagIgclProbeFailureStage::TargetMapping,
                    "multiple exact adapter LUID and target ID matches");
            else if (complete)
                AddFailure(state, DiagIgclProbeFailureStage::TargetMapping,
                    "no exact adapter LUID and target ID match");
            return state;
        }

        const auto output = outputs[*match.outputIndex].handle;
        ArcSyncCapabilityAbi capability{};
        capability.size = sizeof(capability);
        const auto capabilityResult = endpoints_->getArcSyncInfo(output, &capability);
        state.capabilityResult = capabilityResult;
        if (capabilityResult == 0)
            state.capability = DiagArcSyncCapability{ capability.supported,
                capability.minimumHz, capability.maximumHz,
                capability.maxFrameTimeIncreaseUs, capability.maxFrameTimeDecreaseUs };
        else
            AddFailure(state, DiagIgclProbeFailureStage::GetArcSyncInfo,
                "ctlGetIntelArcSyncInfoForMonitor",
                DiagIgclProbeResultDomain::ControlLibrary, capabilityResult);

        ArcSyncProfileAbi profile{};
        profile.size = sizeof(profile);
        const auto profileResult = endpoints_->getArcSyncProfile(output, &profile);
        state.profileResult = profileResult;
        if (profileResult == 0)
            state.profile = DiagArcSyncProfile{ profile.profile, profile.maximumHz,
                profile.minimumHz, profile.maxFrameTimeIncreaseUs,
                profile.maxFrameTimeDecreaseUs };
        else
            AddFailure(state, DiagIgclProbeFailureStage::GetArcSyncProfile,
                "ctlGetIntelArcSyncProfile",
                DiagIgclProbeResultDomain::ControlLibrary, profileResult);
        return state;
    }
    catch (...)
    {
        state.mappingStatus = DiagIgclTargetMappingStatus::Unknown;
        state.capability.reset();
        state.profile.reset();
        state.enumerationComplete = false;
        AddFailure(state, DiagIgclProbeFailureStage::InternalError,
            "exception during IGCL query or output enumeration");
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
