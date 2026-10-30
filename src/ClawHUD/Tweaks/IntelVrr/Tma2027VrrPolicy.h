#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clawhud
{
inline constexpr std::string_view kTma2027PhysicalEdidSha256 =
    "3518AB4456669D12A7B8D254F63005EAE143C784DCE02EC56C3753C41A664CA1";

struct Tma2027TargetIdentity
{
    std::int32_t adapterLuidHighPart{};
    std::uint32_t adapterLuidLowPart{};
    std::uint32_t sourceId{};
    std::uint32_t targetId{};
    std::wstring normalizedMonitorInstance;
};

enum class Tma2027EdidStatus
{
    NotChecked,
    Matched,
    Mismatch,
    Unavailable,
    OverridePresent,
    WrongTarget,
};

struct Tma2027DirectRange
{
    bool attempted{};
    bool available{};
    bool adapterAndSourceMatch{};
    bool supported{};
    bool vrrEnabled{};
    std::int32_t ntStatus{};
    std::int32_t adapterLuidHighPart{};
    std::uint32_t adapterLuidLowPart{};
    std::uint32_t sourceId{};
    std::uint32_t targetId{};
    std::uint32_t displayCount{};
    std::uint32_t minimumHz{};
    std::uint32_t maximumHz{};
    std::uint32_t win32Error{};
    std::string failureDetail;
};

struct Tma2027Evidence
{
    Tma2027TargetIdentity target;
    Tma2027EdidStatus edidStatus{ Tma2027EdidStatus::NotChecked };
    std::string edidSha256;
    Tma2027DirectRange directRange;
};

struct Tma2027ProfileState
{
    bool exact{};
    bool capabilitySupported{};
    float capabilityMinimumHz{};
    float capabilityMaximumHz{};
    bool profileAvailable{};
    std::uint32_t profile{};
    float profileMinimumHz{};
    float profileMaximumHz{};
    Tma2027TargetIdentity target;
};

enum class Tma2027Decision
{
    Unavailable,
    Unsupported,
    AlreadyCorrect,
    PreserveUserProfile,
    ApplyExcellent,
};

bool IsTma2027PhysicalEdidHash(std::string_view sha256) noexcept;
std::optional<std::vector<std::uint8_t>> CanonicalizeTma2027Edid(
    std::span<const std::uint8_t> bytes);
Tma2027DirectRange DecodeTma2027DirectRange(std::int32_t ntStatus,
    std::span<const std::uint8_t> payload, std::int32_t adapterLuidHighPart,
    std::uint32_t adapterLuidLowPart, std::uint32_t sourceId) noexcept;
bool IsTma2027VerifiedPhysicalEdidEvidence(const Tma2027Evidence& evidence,
    const Tma2027TargetIdentity& expectedTarget) noexcept;
bool IsTma2027VerifiedHalfFloorEvidence(const Tma2027Evidence& evidence,
    const Tma2027TargetIdentity& expectedTarget) noexcept;
Tma2027Decision EvaluateTma2027Profile(const Tma2027ProfileState& state,
    const Tma2027Evidence* evidence) noexcept;
bool IsTma2027ExcellentSetterAllowed(const Tma2027ProfileState& state,
    const Tma2027Evidence* evidence) noexcept;
bool IsTma2027ExcellentReadbackVerified(const Tma2027ProfileState& state,
    const Tma2027Evidence* evidence) noexcept;
}
