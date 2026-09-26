#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

enum class DiagIgclTargetMappingStatus
{
    Exact,
    Unknown,
    Ambiguous,
};

struct DiagIgclTargetMatch
{
    DiagIgclTargetMappingStatus status{ DiagIgclTargetMappingStatus::Unknown };
    std::optional<std::size_t> outputIndex;
};

struct DiagIgclOutputIdentity
{
    LUID adapterLuid{};
    std::uint32_t targetId{};
};

DiagIgclTargetMatch ResolveDiagIgclTarget(const LUID& windowsTargetAdapterLuid,
    std::uint32_t windowsTargetId, std::span<const DiagIgclOutputIdentity> outputs,
    bool enumerationComplete = true) noexcept;

struct DiagArcSyncCapability
{
    bool supported{};
    float minimumHz{};
    float maximumHz{};
    std::uint32_t maxFrameTimeIncreaseUs{};
    std::uint32_t maxFrameTimeDecreaseUs{};
};

struct DiagArcSyncProfile
{
    std::uint32_t profile{};
    float maximumHz{};
    float minimumHz{};
    std::uint32_t maxFrameTimeIncreaseUs{};
    std::uint32_t maxFrameTimeDecreaseUs{};
};

enum class DiagIgclProbeFailureStage
{
    None,
    NotInitialized,
    LoadLibrary,
    ResolveFunction,
    Initialize,
    EnumerateDevices,
    GetDeviceProperties,
    EnumerateDisplayOutputs,
    GetDisplayProperties,
    TargetMapping,
    GetArcSyncInfo,
    GetArcSyncProfile,
    InternalError,
};

enum class DiagIgclProbeResultDomain
{
    None,
    Win32,
    ControlLibrary,
};

struct DiagIgclProbeFailure
{
    static constexpr std::size_t NoIndex = static_cast<std::size_t>(-1);

    DiagIgclProbeFailureStage stage{ DiagIgclProbeFailureStage::None };
    DiagIgclProbeResultDomain resultDomain{ DiagIgclProbeResultDomain::None };
    std::optional<std::uint32_t> result;
    std::string_view detail;
    std::size_t adapterIndex{ NoIndex };
    std::size_t outputIndex{ NoIndex };
};

inline constexpr std::size_t kMaximumDiagIgclFailureRecords = 16;

struct DiagIntelVrrState
{
    DiagIgclTargetMappingStatus mappingStatus{ DiagIgclTargetMappingStatus::Unknown };
    LUID windowsTargetAdapterLuid{};
    std::uint32_t windowsTargetId{};
    std::optional<DiagArcSyncCapability> capability;
    std::optional<DiagArcSyncProfile> profile;
    std::optional<std::uint32_t> capabilityResult;
    std::optional<std::uint32_t> profileResult;
    bool attempted{};
    bool initialized{};
    bool enumerationComplete{};
    std::uint32_t adapterCount{};
    std::uint32_t displayOutputCount{};
    std::uint32_t displayPropertiesSuccessCount{};
    std::uint32_t targetMatchCount{};
    std::array<DiagIgclProbeFailure, kMaximumDiagIgclFailureRecords> failures{};
    std::size_t failureRecordCount{};
    std::uint32_t suppressedFailureCount{};
};

class DiagIntelVrrStateProbe
{
public:
    ~DiagIntelVrrStateProbe();

    bool Initialize() noexcept;
    DiagIntelVrrState Query(const LUID& windowsTargetAdapterLuid,
        std::uint32_t windowsTargetId) noexcept;
    void Shutdown() noexcept;

private:
    struct Endpoints;
    Endpoints* endpoints_{};
    void* library_{};
    void* apiHandle_{};
    std::vector<void*> adapters_;
    bool attempted_{};
    DiagIgclProbeFailure initializationFailure_;
};
