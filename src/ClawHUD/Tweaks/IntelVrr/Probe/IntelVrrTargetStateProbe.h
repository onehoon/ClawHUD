#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace clawhud
{
enum class IntelVrrTargetMappingStatus
{
    Exact,
    Unknown,
    Ambiguous,
};

struct IntelVrrTargetMatch
{
    IntelVrrTargetMappingStatus status{ IntelVrrTargetMappingStatus::Unknown };
    std::optional<std::size_t> outputIndex;
};

struct IntelVrrOutputIdentity
{
    LUID adapterLuid{};
    std::uint32_t targetId{};
};

IntelVrrTargetMatch ResolveIntelVrrTarget(const LUID& windowsTargetAdapterLuid,
    std::uint32_t windowsTargetId, std::span<const IntelVrrOutputIdentity> outputs,
    bool enumerationComplete = true) noexcept;

struct IntelVrrArcSyncCapability
{
    bool supported{};
    float minimumHz{};
    float maximumHz{};
    std::uint32_t maxFrameTimeIncreaseUs{};
    std::uint32_t maxFrameTimeDecreaseUs{};
};

struct IntelVrrArcSyncProfile
{
    std::uint32_t profile{};
    float maximumHz{};
    float minimumHz{};
    std::uint32_t maxFrameTimeIncreaseUs{};
    std::uint32_t maxFrameTimeDecreaseUs{};
};

struct IntelVrrTargetState
{
    IntelVrrTargetMappingStatus mappingStatus{ IntelVrrTargetMappingStatus::Unknown };
    LUID windowsTargetAdapterLuid{};
    std::uint32_t windowsTargetId{};
    std::optional<IntelVrrArcSyncCapability> capability;
    std::optional<IntelVrrArcSyncProfile> profile;
    std::optional<std::uint32_t> capabilityResult;
    std::optional<std::uint32_t> profileResult;
    bool initialized{};
    bool enumerationComplete{};
    std::uint32_t adapterCount{};
    std::uint32_t displayOutputCount{};
    std::uint32_t displayPropertiesSuccessCount{};
    std::uint32_t targetMatchCount{};
};

struct IntelVrrProfileSetResult
{
    bool attempted{};
    bool succeeded{};
    std::optional<std::uint32_t> apiResult;
    std::string_view detail;
};

class IntelVrrTargetStateProbe
{
public:
    ~IntelVrrTargetStateProbe();

    bool Initialize() noexcept;
    IntelVrrTargetState Query(const LUID& windowsTargetAdapterLuid,
        std::uint32_t windowsTargetId) noexcept;
    IntelVrrProfileSetResult SetProfileForExactTarget(
        const IntelVrrTargetState& expectedCurrentState,
        std::uint32_t requestedProfile) noexcept;
    void Shutdown() noexcept;

private:
    struct Endpoints;
    Endpoints* endpoints_{};
    void* library_{};
    void* apiHandle_{};
    std::vector<void*> adapters_;
    // Single-use output handle from the immediately preceding exact Query.
    void* exactOutput_{};
    LUID exactOutputAdapterLuid_{};
    std::uint32_t exactOutputTargetId_{};
};
}
