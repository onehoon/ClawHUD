#include "Lg27gs65fVrrRangeTweak.h"

#include "Probe/IntelDisplayPathProbe.h"

#include <windows.h>

#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace clawhud
{
namespace
{
constexpr float kToleranceHz = 0.25f;
constexpr std::uint32_t kRecommended = 1;
constexpr std::uint32_t kExcellent = 2;

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

} // namespace

// Observed hardware identity: \\?\DISPLAY#GSM5C8C#... / LG ULTRAGEAR.
// Product code, manufacturer and friendly name must all agree.
bool IsLg27gs65fWindowsIdentity(std::wstring_view path, std::wstring_view friendly) noexcept
{
    const auto first = path.find(L'#');
    if (first == std::wstring_view::npos || !ContainsInsensitive(path.substr(0, first), L"DISPLAY"))
        return false;
    const auto second = path.find(L'#', first + 1);
    if (second == std::wstring_view::npos ||
        !EqualsInsensitive(path.substr(first + 1, second - first - 1), L"GSM5C8C"))
        return false;
    return ContainsInsensitive(friendly, L"27GS65F") ||
        ContainsInsensitive(friendly, L"LG ULTRAGEAR");
}

namespace
{
struct Monitors
{
    HMONITOR monitor{};
    std::uint32_t count{};
};

BOOL CALLBACK CountMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM user)
{
    auto& monitors = *reinterpret_cast<Monitors*>(user);
    monitors.monitor = monitor;
    ++monitors.count;
    return TRUE;
}

struct LgTarget
{
    LUID adapterLuid{};
    std::uint32_t targetId{};
    std::wstring source;
    std::wstring path;
    std::wstring friendly;
};

struct TargetLookup
{
    IntelVrrRunStatus failure{ IntelVrrRunStatus::Unavailable };
    std::string detail;
    std::optional<LgTarget> value;
};

TargetLookup FindExactLgTarget()
{
    Monitors monitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, CountMonitor,
            reinterpret_cast<LPARAM>(&monitors)))
        return { IntelVrrRunStatus::Unavailable, "EnumDisplayMonitors failed", {} };
    if (monitors.count != 1)
        return { IntelVrrRunStatus::AmbiguousDisplay,
            "LG external fix requires one active desktop monitor", {} };

    const auto path = IntelDisplayPathProbe{}.Probe(monitors.monitor);
    if (path.status != IntelDisplayPathProbeStatus::Matched || !path.path)
        return { IntelVrrRunStatus::Unavailable, "Windows display path is unavailable", {} };
    if (path.path->activePathCount != 1)
        return { IntelVrrRunStatus::AmbiguousDisplay,
            "LG external fix requires one active Windows display target", {} };

    DISPLAYCONFIG_TARGET_DEVICE_NAME display{};
    display.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    display.header.size = sizeof(display);
    display.header.adapterId = path.path->targetAdapterLuid;
    display.header.id = path.path->targetId;
    if (DisplayConfigGetDeviceInfo(&display.header) != ERROR_SUCCESS)
        return { IntelVrrRunStatus::Unavailable, "monitor identity lookup failed", {} };

    const std::wstring_view devicePath = display.monitorDevicePath;
    const std::wstring_view friendly = display.monitorFriendlyDeviceName;
    if (!IsLg27gs65fWindowsIdentity(devicePath, friendly))
        return { IntelVrrRunStatus::UnsupportedPanel, "target is not LG 27GS65F", {} };

    return { IntelVrrRunStatus::AlreadyCorrect, {},
        LgTarget{ path.path->targetAdapterLuid, path.path->targetId,
            path.path->monitorDeviceName, std::wstring(devicePath), std::wstring(friendly) } };
}

bool SameTarget(const LgTarget& a, const LgTarget& b) noexcept
{
    return a.adapterLuid.LowPart == b.adapterLuid.LowPart &&
        a.adapterLuid.HighPart == b.adapterLuid.HighPart &&
        a.targetId == b.targetId &&
        EqualsInsensitive(a.source, b.source) &&
        EqualsInsensitive(a.path, b.path) &&
        EqualsInsensitive(a.friendly, b.friendly);
}

bool MatchesCurrentTarget(const LgTarget& before)
{
    const auto current = FindExactLgTarget();
    return current.value && SameTarget(before, *current.value);
}

std::string Range(float minimum, float maximum)
{
    std::ostringstream text;
    text << minimum << '-' << maximum << " Hz";
    return text.str();
}

bool Verified(const IntelVrrTargetState& state, std::uint32_t profile)
{
    return state.initialized && state.enumerationComplete &&
        state.mappingStatus == IntelVrrTargetMappingStatus::Exact &&
        state.targetMatchCount == 1 && state.capabilityResult == 0 &&
        state.profileResult == 0 && state.capability && state.profile &&
        state.capability->supported &&
        Near(state.capability->minimumHz, 48.0f) &&
        Near(state.capability->maximumHz, 180.0f) &&
        state.profile->profile == profile;
}
}

Lg27gs65fProfileDecision EvaluateLg27gs65fProductionProfile(
    const IntelVrrTargetState& state) noexcept
{
    if (!state.initialized || !state.enumerationComplete ||
        state.mappingStatus != IntelVrrTargetMappingStatus::Exact ||
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
    if (profile.profile == kRecommended && Near(profile.minimumHz, 90.0f) &&
        Near(profile.maximumHz, 180.0f))
        return Lg27gs65fProfileDecision::ApplyExcellent;
    // Never override CUSTOM, OFF, an unexpected range, or any other user profile.
    return Lg27gs65fProfileDecision::SkipUserProfile;
}

Lg27gs65fVrrFixOutcome RunLg27gs65fProductionVrrFix()
{
    Lg27gs65fVrrFixOutcome outcome;
    auto& result = outcome.result;
    auto& log = outcome.log;
    const auto finish = [&](IntelVrrRunStatus status, std::string message) {
        result.status = status;
        result.message = std::move(message);
        return outcome;
    };

    try
    {
        const auto target = FindExactLgTarget();
        if (!target.value)
            return finish(target.failure, target.detail);
        outcome.targetVerified = true;
        log.push_back("LG 27GS65F Windows target validated via GSM5C8C identity and single active display.");

        IntelVrrTargetStateProbe probe;
        if (!probe.Initialize())
            return finish(IntelVrrRunStatus::Unavailable, "IGCL initialization failed.");

        const auto before = probe.Query(target.value->adapterLuid, target.value->targetId);
        if (before.profile)
        {
            result.rangeBefore = Range(before.profile->minimumHz, before.profile->maximumHz);
            log.push_back("LG current IGCL range=" + result.rangeBefore);
        }
        const auto decision = EvaluateLg27gs65fProductionProfile(before);
        if (decision == Lg27gs65fProfileDecision::AlreadyCorrect)
        {
            result.rangeAfter = result.rangeBefore;
            return finish(IntelVrrRunStatus::AlreadyCorrect, "LG 27GS65F already uses full 48-180 Hz VRR range.");
        }
        if (decision == Lg27gs65fProfileDecision::SkipUserProfile)
        {
            result.rangeAfter = result.rangeBefore;
            return finish(IntelVrrRunStatus::SkippedUserProfile,
                "LG Arc Sync profile is user-selected or differs from the verified 90-180 Hz state.");
        }
        if (decision == Lg27gs65fProfileDecision::Unsupported)
            return finish(IntelVrrRunStatus::UnsupportedPanel, "Unexpected LG VRR capability; no change made.");
        if (decision == Lg27gs65fProfileDecision::Unavailable)
            return finish(IntelVrrRunStatus::Unavailable, "Could not resolve exact LG IGCL output and profile.");

        // Confirm Windows target again; IGCL setter also re-reads the exact output.
        if (!MatchesCurrentTarget(*target.value))
            return finish(IntelVrrRunStatus::AmbiguousDisplay, "LG display topology changed before apply.");

        const auto applied = probe.SetProfileForExactTarget(before, kExcellent);
        if (applied.apiResult)
        {
            std::ostringstream text;
            text << "ctlSetIntelArcSyncProfile result=0x" << std::hex << *applied.apiResult;
            log.push_back(text.str());
        }
        if (!applied.succeeded)
            return finish(applied.attempted ? IntelVrrRunStatus::ApplyFailed :
                IntelVrrRunStatus::Unavailable, "LG EXCELLENT profile write was rejected.");

        if (!MatchesCurrentTarget(*target.value))
            return finish(IntelVrrRunStatus::VerificationFailed, "LG display changed after profile apply.");
        const auto after = probe.Query(target.value->adapterLuid, target.value->targetId);
        if (after.profile)
        {
            result.rangeAfter = Range(after.profile->minimumHz, after.profile->maximumHz);
            log.push_back("LG readback IGCL range=" + result.rangeAfter);
        }
        if (Verified(after, kExcellent) &&
            Near(after.profile->minimumHz, 48.0f) &&
            Near(after.profile->maximumHz, 180.0f))
            return finish(IntelVrrRunStatus::Applied, "LG 27GS65F VRR range restored to 48-180 Hz.");

        // Revert only if the same monitor still maps exactly and IGCL reports EXCELLENT.
        if (Verified(after, kExcellent) && MatchesCurrentTarget(*target.value))
        {
            const auto rollback = probe.SetProfileForExactTarget(after, kRecommended);
            if (rollback.succeeded && MatchesCurrentTarget(*target.value))
            {
                const auto reverted = probe.Query(target.value->adapterLuid, target.value->targetId);
                log.push_back(Verified(reverted, kRecommended)
                    ? "LG rollback RECOMMENDED verified" : "LG rollback could not be verified");
            }
            else log.push_back("LG rollback failed or target changed");
        }
        return finish(IntelVrrRunStatus::VerificationFailed,
            "LG profile changed but 48-180 Hz readback could not be verified.");
    }
    catch (...)
    {
        return finish(IntelVrrRunStatus::Unavailable, "LG VRR fix encountered an unexpected error.");
    }
}
}
