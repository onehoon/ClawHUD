#pragma once

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "Tweaks/IntelVrr/Tma2027VrrPolicy.h"

namespace clawhud
{

enum class IntelVrrTargetMappingStatus
{
    Exact,
    Unknown,
    Ambiguous,
};

enum class IntelVrrPanelClass
{
    ClawInternal,
    Lg27gs65f,
    Claw7Internal,
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

enum class IntelVrrProbeFailureStage
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

enum class IntelVrrProbeResultDomain
{
    None,
    Win32,
    ControlLibrary,
};

struct IntelVrrProbeFailure
{
    static constexpr std::size_t NoIndex = static_cast<std::size_t>(-1);

    IntelVrrProbeFailureStage stage{ IntelVrrProbeFailureStage::None };
    IntelVrrProbeResultDomain resultDomain{ IntelVrrProbeResultDomain::None };
    std::optional<std::uint32_t> result;
    std::string_view detail;
    std::size_t adapterIndex{ NoIndex };
    std::size_t outputIndex{ NoIndex };
};

inline constexpr std::size_t kMaximumIntelVrrFailureRecords = 16;

struct IntelVrrTargetState
{
    IntelVrrTargetMappingStatus mappingStatus{ IntelVrrTargetMappingStatus::Unknown };
    LUID windowsTargetAdapterLuid{};
    std::uint32_t windowsTargetId{};
    std::optional<IntelVrrArcSyncCapability> capability;
    std::optional<IntelVrrArcSyncProfile> profile;
    std::optional<std::uint32_t> capabilityResult;
    std::optional<std::uint32_t> profileResult;
    bool attempted{};
    bool initialized{};
    bool enumerationComplete{};
    std::uint32_t adapterCount{};
    std::uint32_t displayOutputCount{};
    std::uint32_t displayPropertiesSuccessCount{};
    std::uint32_t targetMatchCount{};
    std::array<IntelVrrProbeFailure, kMaximumIntelVrrFailureRecords> failures{};
    std::size_t failureRecordCount{};
    std::uint32_t suppressedFailureCount{};
};

struct IntelVrrProfileSetResult
{
    bool attempted{};
    bool succeeded{};
    std::optional<std::uint32_t> apiResult;
    std::string_view detail;
};

bool IsIntelVrrProfileChangeAllowed(const IntelVrrTargetState& state,
    IntelVrrPanelClass panelClass, std::uint32_t requestedProfile,
    const clawhud::Tma2027Evidence* tmaEvidence = nullptr) noexcept;

namespace detail
{
bool IsIntelVrrFreshStateMatch(const IntelVrrTargetState& expected,
    const IntelVrrArcSyncCapability& capability,
    const IntelVrrArcSyncProfile& profile) noexcept;

bool HasExactIntelVrrDeviceId(const void* expectedPointer,
    std::size_t expectedSize, const void* returnedPointer,
    std::uint32_t returnedSize) noexcept;

void RecordIntelVrrProbeFailure(IntelVrrTargetState& state,
    IntelVrrProbeFailureStage stage, std::string_view detail,
    IntelVrrProbeResultDomain domain = IntelVrrProbeResultDomain::None,
    std::optional<std::uint32_t> result = std::nullopt,
    std::size_t adapterIndex = IntelVrrProbeFailure::NoIndex,
    std::size_t outputIndex = IntelVrrProbeFailure::NoIndex) noexcept;

template<class Setter>
bool SetIntelVrrProfileIfFresh(const IntelVrrTargetState& expected,
    const IntelVrrArcSyncCapability& capability,
    const IntelVrrArcSyncProfile& profile, Setter&& setter)
{
    if (!IsIntelVrrFreshStateMatch(expected, capability, profile))
        return false;
    setter();
    return true;
}
}

class IIntelVrrTargetStateProbe
{
public:
    virtual ~IIntelVrrTargetStateProbe() = default;
    virtual bool Initialize() noexcept = 0;
    virtual IntelVrrTargetState Query(const LUID& windowsTargetAdapterLuid,
        std::uint32_t windowsTargetId) noexcept = 0;
    virtual IntelVrrProfileSetResult SetProfileForExactTarget(
        const IntelVrrTargetState& expectedCurrentState,
        IntelVrrPanelClass panelClass, std::uint32_t requestedProfile,
        const Tma2027Evidence* tmaEvidence = nullptr) noexcept = 0;
    virtual void Shutdown() noexcept = 0;
};

class IntelVrrTargetStateProbe final : public IIntelVrrTargetStateProbe
{
public:
    ~IntelVrrTargetStateProbe() override;

    bool Initialize() noexcept override;
    IntelVrrTargetState Query(const LUID& windowsTargetAdapterLuid,
        std::uint32_t windowsTargetId) noexcept override;
    IntelVrrProfileSetResult SetProfileForExactTarget(
        const IntelVrrTargetState& expectedCurrentState,
        IntelVrrPanelClass panelClass, std::uint32_t requestedProfile,
        const clawhud::Tma2027Evidence* tmaEvidence = nullptr) noexcept override;
    void Shutdown() noexcept override;

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
    bool attempted_{};
    IntelVrrProbeFailure initializationFailure_;
};

} // namespace clawhud
