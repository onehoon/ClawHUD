#include "Tma2027VrrPolicy.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace clawhud
{
namespace
{
constexpr float kToleranceHz = 0.25f;
constexpr std::uint32_t kRecommended = 1;
constexpr std::uint32_t kExcellent = 2;
constexpr std::uint32_t kGood = 3;
constexpr std::uint32_t kCompatible = 4;
constexpr std::uint32_t kOff = 5;
constexpr std::uint32_t kVesa = 6;
constexpr std::uint32_t kCustom = 7;

bool Near(float value, float expected) noexcept
{
    return std::isfinite(value) && std::abs(value - expected) <= kToleranceHz;
}

bool IsKnownStandardProfile(std::uint32_t profile) noexcept
{
    return profile == kRecommended || profile == kExcellent || profile == kGood ||
        profile == kCompatible || profile == kVesa;
}

bool IsPlausibleProfileRange(const Tma2027ProfileState& state) noexcept
{
    return std::isfinite(state.profileMinimumHz) &&
        std::isfinite(state.profileMaximumHz) &&
        state.profileMinimumHz >= 48.0f - kToleranceHz &&
        state.profileMinimumHz < state.profileMaximumHz - kToleranceHz &&
        state.profileMaximumHz <= 120.0f + kToleranceHz;
}

bool IsExactIdentity(const Tma2027TargetIdentity& left,
    const Tma2027TargetIdentity& right) noexcept
{
    if (left.adapterLuidHighPart != right.adapterLuidHighPart ||
        left.adapterLuidLowPart != right.adapterLuidLowPart ||
        left.sourceId != right.sourceId || left.targetId != right.targetId ||
        left.normalizedMonitorInstance.size() != right.normalizedMonitorInstance.size())
        return false;

    for (std::size_t index = 0; index < left.normalizedMonitorInstance.size(); ++index)
    {
        auto leftCharacter = left.normalizedMonitorInstance[index];
        auto rightCharacter = right.normalizedMonitorInstance[index];
        if (leftCharacter >= L'A' && leftCharacter <= L'Z')
            leftCharacter = static_cast<wchar_t>(leftCharacter + (L'a' - L'A'));
        if (rightCharacter >= L'A' && rightCharacter <= L'Z')
            rightCharacter = static_cast<wchar_t>(rightCharacter + (L'a' - L'A'));
        if (leftCharacter != rightCharacter)
            return false;
    }
    return !left.normalizedMonitorInstance.empty();
}

bool IsTma2027MonitorPath(std::wstring_view value) noexcept
{
    constexpr std::wstring_view prefix = L"display\\tma2027\\";
    if (value.size() <= prefix.size())
        return false;
    for (std::size_t index = 0; index < prefix.size(); ++index)
    {
        auto character = value[index];
        if (character >= L'A' && character <= L'Z')
            character = static_cast<wchar_t>(character + (L'a' - L'A'));
        if (character != prefix[index])
            return false;
    }
    return value.find(L'\\', prefix.size()) == std::wstring_view::npos;
}

bool IsNativeCapability(const Tma2027ProfileState& state) noexcept
{
    return state.capabilitySupported && Near(state.capabilityMinimumHz, 48.0f) &&
        Near(state.capabilityMaximumHz, 120.0f);
}

bool IsHalfFloorCapability(const Tma2027ProfileState& state) noexcept
{
    return state.capabilitySupported && Near(state.capabilityMinimumHz, 24.0f) &&
        Near(state.capabilityMaximumHz, 120.0f);
}

bool IsNativeProfile(const Tma2027ProfileState& state) noexcept
{
    return Near(state.profileMinimumHz, 48.0f) &&
        Near(state.profileMaximumHz, 120.0f);
}

bool IsValidForPolicy(const Tma2027ProfileState& state,
    const Tma2027Evidence* evidence) noexcept
{
    if (!state.exact || !state.profileAvailable || !state.capabilitySupported ||
        !IsKnownStandardProfile(state.profile) || !IsPlausibleProfileRange(state))
        return false;
    if (!evidence || !IsTma2027VerifiedPhysicalEdidEvidence(*evidence, state.target))
        return false;
    if (IsNativeCapability(state))
        return true;
    return IsHalfFloorCapability(state) &&
        IsTma2027VerifiedHalfFloorEvidence(*evidence, state.target);
}
}

bool IsTma2027PhysicalEdidHash(std::string_view sha256) noexcept
{
    if (sha256.size() != kTma2027PhysicalEdidSha256.size())
        return false;
    for (std::size_t index = 0; index < sha256.size(); ++index)
    {
        auto character = sha256[index];
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - ('a' - 'A'));
        if (character != kTma2027PhysicalEdidSha256[index])
            return false;
    }
    return true;
}

std::optional<std::vector<std::uint8_t>> CanonicalizeTma2027Edid(
    std::span<const std::uint8_t> bytes)
{
    constexpr std::size_t kEdidBlockSize = 128;
    constexpr std::uint8_t kHeader[] = { 0x00, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0x00 };
    if (bytes.size() != kEdidBlockSize && bytes.size() != 2 * kEdidBlockSize)
        return std::nullopt;
    if (bytes.size() == 2 * kEdidBlockSize &&
        std::any_of(bytes.begin() + kEdidBlockSize, bytes.end(),
            [](std::uint8_t value) { return value != 0; }))
        return std::nullopt;
    if (!std::equal(std::begin(kHeader), std::end(kHeader), bytes.begin()) ||
        bytes[126] != 0)
        return std::nullopt;

    std::uint8_t checksum{};
    for (std::size_t index = 0; index < kEdidBlockSize; ++index)
        checksum = static_cast<std::uint8_t>(checksum + bytes[index]);
    if (checksum != 0)
        return std::nullopt;

    return std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + kEdidBlockSize);
}

Tma2027DirectRange DecodeTma2027DirectRange(std::int32_t ntStatus,
    std::span<const std::uint8_t> payload, std::int32_t adapterLuidHighPart,
    std::uint32_t adapterLuidLowPart, std::uint32_t sourceId) noexcept
{
    Tma2027DirectRange result;
    result.attempted = true;
    result.ntStatus = ntStatus;
    result.adapterLuidHighPart = adapterLuidHighPart;
    result.adapterLuidLowPart = adapterLuidLowPart;
    result.sourceId = sourceId;
    if (ntStatus != 0 || payload.size() < 40)
        return result;

    const auto read32 = [&](std::size_t offset)
    {
        return static_cast<std::uint32_t>(payload[offset]) |
            (static_cast<std::uint32_t>(payload[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(payload[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(payload[offset + 3]) << 24);
    };
    result.supported = payload[20] == 1;
    result.vrrEnabled = payload[21] == 1;
    result.displayCount = read32(24);
    result.targetId = read32(28);
    result.minimumHz = read32(32);
    result.maximumHz = read32(36);
    result.available = result.displayCount != 0;
    return result;
}

bool IsTma2027VerifiedPhysicalEdidEvidence(const Tma2027Evidence& evidence,
    const Tma2027TargetIdentity& expectedTarget) noexcept
{
    return evidence.edidStatus == Tma2027EdidStatus::Matched &&
        IsTma2027PhysicalEdidHash(evidence.edidSha256) &&
        IsTma2027MonitorPath(evidence.target.normalizedMonitorInstance) &&
        IsExactIdentity(evidence.target, expectedTarget);
}

bool IsTma2027VerifiedHalfFloorEvidence(const Tma2027Evidence& evidence,
    const Tma2027TargetIdentity& expectedTarget) noexcept
{
    const auto& direct = evidence.directRange;
    return IsTma2027VerifiedPhysicalEdidEvidence(evidence, expectedTarget) &&
        direct.attempted && direct.available && direct.ntStatus == 0 &&
        direct.adapterAndSourceMatch && direct.supported && direct.vrrEnabled &&
        direct.displayCount > 0 &&
        direct.adapterLuidHighPart == expectedTarget.adapterLuidHighPart &&
        direct.adapterLuidLowPart == expectedTarget.adapterLuidLowPart &&
        direct.sourceId == expectedTarget.sourceId &&
        direct.targetId == expectedTarget.targetId &&
        direct.minimumHz == 48 && direct.maximumHz == 120;
}

Tma2027Decision EvaluateTma2027Profile(const Tma2027ProfileState& state,
    const Tma2027Evidence* evidence) noexcept
{
    if (!state.exact || !state.profileAvailable || !state.capabilitySupported)
        return Tma2027Decision::Unavailable;
    if (state.profile == kCustom || state.profile == kOff)
        return Tma2027Decision::PreserveUserProfile;
    if (!IsKnownStandardProfile(state.profile) || !IsPlausibleProfileRange(state))
        return Tma2027Decision::Unsupported;

    const bool native = IsNativeCapability(state);
    const bool halfFloor = IsHalfFloorCapability(state);
    if (!native && !halfFloor)
        return Tma2027Decision::Unsupported;
    if (!evidence ||
        !IsTma2027VerifiedPhysicalEdidEvidence(*evidence, state.target))
        return Tma2027Decision::Unavailable;
    if (halfFloor && (!evidence ||
        !IsTma2027VerifiedHalfFloorEvidence(*evidence, state.target)))
        return Tma2027Decision::Unavailable;
    if (IsNativeProfile(state))
        return Tma2027Decision::AlreadyCorrect;
    return Tma2027Decision::ApplyExcellent;
}

bool IsTma2027ExcellentSetterAllowed(const Tma2027ProfileState& state,
    const Tma2027Evidence* evidence) noexcept
{
    return IsValidForPolicy(state, evidence) && !IsNativeProfile(state);
}

bool IsTma2027ExcellentReadbackVerified(const Tma2027ProfileState& state,
    const Tma2027Evidence* evidence) noexcept
{
    if (!state.exact || !state.profileAvailable || state.profile != kExcellent ||
        !Near(state.profileMinimumHz, 48.0f) ||
        !Near(state.profileMaximumHz, 120.0f))
        return false;
    if (!evidence ||
        !IsTma2027VerifiedPhysicalEdidEvidence(*evidence, state.target))
        return false;
    if (IsNativeCapability(state))
        return true;
    return IsHalfFloorCapability(state) && evidence &&
        IsTma2027VerifiedHalfFloorEvidence(*evidence, state.target);
}
}
