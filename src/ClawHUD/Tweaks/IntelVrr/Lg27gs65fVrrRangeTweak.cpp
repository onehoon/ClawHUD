#include "Lg27gs65fVrrRangeTweak.h"

#include <cmath>

namespace clawhud
{
namespace
{
constexpr float kToleranceHz = 0.25f;
constexpr std::uint32_t kRecommended = 1;
constexpr std::uint32_t kExcellent = 2;
constexpr std::uint32_t kCompatible = 4;

bool Near(float value, float expected) noexcept
{
    return std::isfinite(value) && std::abs(value - expected) <= kToleranceHz;
}

bool EqualsInsensitive(std::wstring_view left, std::wstring_view right) noexcept
{
    return left.size() == right.size() &&
        CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
            right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

bool ContainsInsensitive(std::wstring_view value, std::wstring_view token) noexcept
{
    if (token.size() > value.size()) return false;
    for (std::size_t i = 0; i <= value.size() - token.size(); ++i)
        if (EqualsInsensitive(value.substr(i, token.size()), token)) return true;
    return false;
}
}

// Observed hardware identity: \\?\DISPLAY#GSM5C8C#... / LG ULTRAGEAR.
// Product code, manufacturer and friendly name must all agree.
bool IsLg27gs65fWindowsIdentity(std::wstring_view path, std::wstring_view friendly) noexcept
{
    const auto first = path.find(L'#');
    if (first == std::wstring_view::npos ||
        !ContainsInsensitive(path.substr(0, first), L"DISPLAY"))
        return false;
    const auto second = path.find(L'#', first + 1);
    if (second == std::wstring_view::npos ||
        !EqualsInsensitive(path.substr(first + 1, second - first - 1), L"GSM5C8C"))
        return false;
    return ContainsInsensitive(friendly, L"27GS65F") ||
        ContainsInsensitive(friendly, L"LG ULTRAGEAR");
}

Lg27gs65fProfileDecision EvaluateLg27gs65fProductionProfile(
    const DiagIntelVrrState& state) noexcept
{
    if (!state.initialized || !state.enumerationComplete ||
        state.mappingStatus != DiagIgclTargetMappingStatus::Exact ||
        state.targetMatchCount != 1 || state.displayOutputCount == 0 ||
        state.displayPropertiesSuccessCount != state.displayOutputCount ||
        !state.capability || !state.profile || state.capabilityResult != 0 ||
        state.profileResult != 0)
        return Lg27gs65fProfileDecision::Unavailable;

    if (!state.capability->supported ||
        !Near(state.capability->minimumHz, 48.0f) ||
        !Near(state.capability->maximumHz, 180.0f))
        return Lg27gs65fProfileDecision::Unsupported;

    const auto& profile = *state.profile;
    if (Near(profile.minimumHz, 48.0f) && Near(profile.maximumHz, 180.0f) &&
        (profile.profile == kExcellent || profile.profile == kRecommended))
        return Lg27gs65fProfileDecision::AlreadyCorrect;
    if ((profile.profile == kRecommended || profile.profile == kCompatible) &&
        Near(profile.minimumHz, 90.0f) && Near(profile.maximumHz, 180.0f))
        return Lg27gs65fProfileDecision::ApplyExcellent;
    // Preserve other user-selected profiles and unexpected active ranges.
    return Lg27gs65fProfileDecision::SkipUserProfile;
}
}
