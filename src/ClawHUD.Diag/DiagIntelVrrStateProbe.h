#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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

struct DiagIntelVrrState
{
    DiagIgclTargetMappingStatus mappingStatus{ DiagIgclTargetMappingStatus::Unknown };
    LUID windowsTargetAdapterLuid{};
    std::uint32_t windowsTargetId{};
    std::optional<DiagArcSyncCapability> capability;
    std::optional<DiagArcSyncProfile> profile;
    std::optional<std::uint32_t> capabilityResult;
    std::optional<std::uint32_t> profileResult;
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
};
