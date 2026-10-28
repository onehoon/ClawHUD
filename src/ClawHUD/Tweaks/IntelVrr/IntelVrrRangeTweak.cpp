#include "IntelVrrRangeTweak.h"

#include "IntelVrrResultStore.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

namespace clawhud
{
namespace
{
constexpr float kClawMinimumHz = 48.0f;
constexpr float kClawMaximumHz = 120.0f;
constexpr float kLgMinimumHz = 48.0f;
constexpr float kLgMaximumHz = 180.0f;
constexpr float kToleranceHz = 0.1f;
constexpr std::uint32_t kRecommended = 1;
constexpr std::uint32_t kExcellent = 2;
constexpr std::uint32_t kGood = 3;
constexpr std::uint32_t kCompatible = 4;
constexpr std::uint32_t kOff = 5;
constexpr std::uint32_t kVesa = 6;
constexpr std::uint32_t kCustom = 7;

bool Near(float value, float expected, float tolerance = kToleranceHz) noexcept
{
    return std::isfinite(value) && std::abs(value - expected) <= tolerance;
}

std::string Range(float minimum, float maximum)
{
    std::ostringstream text;
    text << minimum << '-' << maximum << " Hz";
    return text.str();
}

std::string ApiCode(const std::optional<std::uint32_t>& value)
{
    if (!value) return "none";
    std::ostringstream text;
    text << "0x" << std::hex << std::uppercase << *value;
    return text.str();
}

std::string NowUtc()
{
    SYSTEMTIME time{};
    GetSystemTime(&time);
    std::ostringstream text;
    text << std::setfill('0') << std::setw(4) << time.wYear << '-'
        << std::setw(2) << time.wMonth << '-' << std::setw(2) << time.wDay << 'T'
        << std::setw(2) << time.wHour << ':' << std::setw(2) << time.wMinute << ':'
        << std::setw(2) << time.wSecond << 'Z';
    return text.str();
}

std::wstring WidenUtf8(const std::string& value)
{
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size) != size)
        return {};
    return result;
}

std::string NarrowUtf8(std::wstring_view value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) != size)
        return {};
    return result;
}

bool SameLuid(const LUID& left, const LUID& right) noexcept
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

bool SameText(std::wstring_view left, std::wstring_view right) noexcept
{
    return left.size() == right.size() && CompareStringOrdinal(left.data(),
        static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()),
        TRUE) == CSTR_EQUAL;
}

bool SameTarget(const IntelVrrWindowsTarget& left,
    const IntelVrrWindowsTarget& right) noexcept
{
    return SameLuid(left.adapterLuid, right.adapterLuid) &&
        left.targetId == right.targetId && left.sourceId == right.sourceId &&
        left.available && right.available &&
        NormalizeMonitorInstancePath(left.monitorDevicePath) ==
            NormalizeMonitorInstancePath(right.monitorDevicePath) &&
        SameText(left.sourceDeviceName, right.sourceDeviceName) &&
        SameText(left.friendlyName, right.friendlyName);
}

bool ExactState(const IntelVrrTargetState& state) noexcept
{
    return state.initialized && state.enumerationComplete &&
        state.mappingStatus == IntelVrrTargetMappingStatus::Exact &&
        state.targetMatchCount == 1 && state.displayOutputCount > 0 &&
        state.displayPropertiesSuccessCount == state.displayOutputCount &&
        state.capability && state.profile && state.capabilityResult == 0 &&
        state.profileResult == 0;
}

IntelVrrRunResult PanelResult(IntelVrrRunStatus status, const PanelIdentity& panel,
    const IntelVrrTargetState* state, std::string message, const std::string& timestamp)
{
    IntelVrrRunResult result;
    result.status = status;
    result.panelName = panel.panelName;
    result.message = std::move(message);
    result.timestampUtc = timestamp;
    if (state && state->profile)
    {
        result.rangeBefore = Range(state->profile->minimumHz, state->profile->maximumHz);
        result.rangeAfter = result.rangeBefore;
    }
    return result;
}

int SummaryPriority(IntelVrrRunStatus status) noexcept
{
    switch (status)
    {
    case IntelVrrRunStatus::VerificationFailed:
    case IntelVrrRunStatus::ApplyFailed: return 6;
    case IntelVrrRunStatus::Unavailable: return 5;
    case IntelVrrRunStatus::AmbiguousDisplay: return 4;
    case IntelVrrRunStatus::UnsupportedPanel: return 3;
    case IntelVrrRunStatus::Applied: return 2;
    case IntelVrrRunStatus::AlreadyCorrect: return 1;
    case IntelVrrRunStatus::SkippedUserProfile: return 0;
    default: return -1;
    }
}

IntelVrrRunResult Summarize(const std::vector<IntelVrrRunResult>& results,
    const std::string& timestamp)
{
    auto selected = std::max_element(results.begin(), results.end(),
        [](const auto& left, const auto& right)
        { return SummaryPriority(left.status) < SummaryPriority(right.status); });
    IntelVrrRunResult summary = *selected;
    summary.timestampUtc = timestamp;
    if (results.size() > 1)
    {
        summary.panelName = "Multiple displays";
        summary.rangeBefore.clear();
        summary.rangeAfter.clear();
        const auto countStatus = [&](IntelVrrRunStatus status)
        {
            return std::count_if(results.begin(), results.end(),
                [status](const auto& item) { return item.status == status; });
        };
        summary.message = "Targets=" + std::to_string(results.size()) +
            " Applied=" + std::to_string(countStatus(IntelVrrRunStatus::Applied)) +
            " AlreadyCorrect=" + std::to_string(countStatus(IntelVrrRunStatus::AlreadyCorrect)) +
            " SkippedUserProfile=" + std::to_string(countStatus(IntelVrrRunStatus::SkippedUserProfile)) +
            " Unsupported=" + std::to_string(countStatus(IntelVrrRunStatus::UnsupportedPanel)) +
            " Unavailable=" + std::to_string(countStatus(IntelVrrRunStatus::Unavailable)) +
            " Failed=" + std::to_string(countStatus(IntelVrrRunStatus::ApplyFailed) +
                countStatus(IntelVrrRunStatus::VerificationFailed));
    }
    return summary;
}

const char* PanelKindName(SupportedPanelKind kind) noexcept
{
    return kind == SupportedPanelKind::ClawInternal ? "Claw" : "LG27GS65F";
}

IntelVrrPanelClass ProbePanelClass(SupportedPanelKind kind) noexcept
{
    return kind == SupportedPanelKind::ClawInternal
        ? IntelVrrPanelClass::ClawInternal : IntelVrrPanelClass::Lg27gs65f;
}

bool Verified(const IntelVrrTargetState& state, SupportedPanelKind kind,
    std::uint32_t profile) noexcept
{
    if (!ExactState(state) || !state.capability->supported ||
        state.profile->profile != profile)
        return false;
    const float maximum = kind == SupportedPanelKind::ClawInternal
        ? kClawMaximumHz : kLgMaximumHz;
    return Near(state.capability->minimumHz, kClawMinimumHz, 0.25f) &&
        Near(state.capability->maximumHz, maximum, 0.25f) &&
        Near(state.profile->minimumHz, kClawMinimumHz, 0.25f) &&
        Near(state.profile->maximumHz, maximum, 0.25f);
}
}

IntelVrrRunStatus EvaluateClawInternalProfile(const IntelVrrTargetState& state) noexcept
{
    if (!ExactState(state))
        return IntelVrrRunStatus::Unavailable;
    if (!state.capability->supported ||
        !Near(state.capability->minimumHz, kClawMinimumHz) ||
        !Near(state.capability->maximumHz, kClawMaximumHz))
        return IntelVrrRunStatus::UnsupportedPanel;

    const auto& profile = *state.profile;
    const bool fullRange = Near(profile.minimumHz, kClawMinimumHz) &&
        Near(profile.maximumHz, kClawMaximumHz);
    if (profile.profile == kExcellent && fullRange)
        return IntelVrrRunStatus::AlreadyCorrect;
    if (profile.profile == kCustom || profile.profile == kOff)
        return IntelVrrRunStatus::SkippedUserProfile;
    if (profile.profile == kRecommended || profile.profile == kExcellent ||
        profile.profile == kGood || profile.profile == kCompatible ||
        profile.profile == kVesa)
        return fullRange ? IntelVrrRunStatus::AlreadyCorrect : IntelVrrRunStatus::Applied;
    return IntelVrrRunStatus::UnsupportedPanel;
}

IntelVrrRangeTweak::IntelVrrRangeTweak(ProbeFactory probeFactory,
    PanelProvider panelProvider, WindowsTargetProvider windowsTargetProvider)
    : probeFactory_(std::move(probeFactory)), panelProvider_(std::move(panelProvider)),
      windowsTargetProvider_(std::move(windowsTargetProvider))
{
    if (!windowsTargetProvider_)
        windowsTargetProvider_ = [] { return EnumerateIntelVrrWindowsTargets(); };
}

IntelVrrRunResult IntelVrrRangeTweak::Run(bool enabled)
{
    log_.clear();
    summary_ = "targets=0 applied=0 alreadyCorrect=0 skipped=0 unavailable=0 failed=0";
    const std::string timestamp = NowUtc();
    IntelVrrRunResult result;
    result.timestampUtc = timestamp;

    const auto finish = [&](IntelVrrRunResult value, bool save = true)
    {
        log_.push_back(std::string("Run result=") + IntelVrrRunStatusName(value.status) +
            " panel=" + value.panelName + " message=" + value.message);
        if (save) IntelVrrResultStore::Save(value);
        return value;
    };

    if (!enabled)
    {
        result.status = IntelVrrRunStatus::Disabled;
        result.message = "Disabled by user.";
        summary_ = "targets=0 applied=0 alreadyCorrect=0 skipped=0 unavailable=0 failed=0";
        log_.push_back("Toggle is disabled. No action taken.");
        return finish(std::move(result));
    }

    std::vector<PanelIdentity> panels;
    try { panels = panelProvider_(); }
    catch (...)
    {
        result.status = IntelVrrRunStatus::Unavailable;
        result.message = "Could not enumerate WMI monitor identities.";
        summary_ = "targets=unknown applied=0 alreadyCorrect=0 skipped=0 unavailable=1 failed=0";
        log_.push_back("WMI enumeration failed.");
        return finish(std::move(result));
    }

    log_.push_back("WMI candidates=" + std::to_string(panels.size()));
    std::vector<std::pair<PanelIdentity, SupportedPanelKind>> supported;
    std::set<std::pair<int, std::wstring>> seenPanels;
    for (const auto& panel : panels)
    {
        const auto kind = ClassifySupportedPanel(panel);
        log_.push_back(std::string("WMI panel=") + panel.panelName +
            " manufacturer=" + panel.manufacturer + " product=" + panel.productCode +
            " active=" + (panel.active ? "1" : "0") +
            " instance=" + panel.instanceName + " model=" +
            (kind ? PanelKindName(*kind) : "Unsupported"));
        if (!kind) continue;

        const auto instance = NormalizeMonitorInstancePath(WidenUtf8(panel.instanceName));
        const int kindKey = static_cast<int>(*kind);
        if (!instance.empty() && !seenPanels.emplace(kindKey, instance).second)
        {
            log_.push_back(std::string("WMI duplicate panel skipped model=") + PanelKindName(*kind));
            continue;
        }
        supported.emplace_back(panel, *kind);
    }

    if (supported.empty())
    {
        IntelVrrResultStore::Clear();
        result.status = IntelVrrRunStatus::UnsupportedPanel;
        result.message = "No supported WMI panel was found.";
        summary_ = "targets=0 applied=0 alreadyCorrect=0 skipped=0 unavailable=0 failed=0";
        log_.push_back("No supported panel candidates; stale result cleared.");
        return result;
    }

    IntelVrrWindowsTargets windowsTargets;
    try { windowsTargets = windowsTargetProvider_(); }
    catch (...)
    {
        IntelVrrResultStore::Clear();
        result.status = IntelVrrRunStatus::Unavailable;
        result.message = "Could not enumerate active Windows display targets.";
        summary_ = "targets=" + std::to_string(supported.size()) +
            " applied=0 alreadyCorrect=0 skipped=0 unavailable=" +
            std::to_string(supported.size()) + " failed=0";
        log_.push_back("Windows target enumeration threw; stale result cleared.");
        return result;
    }
    log_.push_back("Windows active targets=" + std::to_string(windowsTargets.targets.size()) +
        " complete=" + std::to_string(windowsTargets.enumerationComplete) +
        " error=" + std::to_string(windowsTargets.error));

    struct TargetWork
    {
        PanelIdentity panel;
        SupportedPanelKind kind{};
        IntelVrrWindowsTarget target;
    };
    std::vector<TargetWork> work;
    std::vector<std::pair<LUID, UINT32>> seenTargets;
    std::size_t skippedNotActive{};
    std::size_t ambiguousMappings{};
    std::size_t unavailableMappings{};
    for (const auto& [panel, kind] : supported)
    {
        const auto match = ResolvePanelWindowsTarget(panel, windowsTargets);
        if (match.status != PanelTargetMappingStatus::Exact || !match.targetIndex)
        {
            const char* status = match.status == PanelTargetMappingStatus::NotActive
                ? "SkippedNotActive" : match.status == PanelTargetMappingStatus::Ambiguous
                    ? "Ambiguous" : "Unavailable";
            log_.push_back(std::string("Panel model=") + PanelKindName(kind) +
                " targetMapping=" + status + " WMIActive=" +
                (panel.active ? "1" : "0") + " setter=0");
            if (match.status == PanelTargetMappingStatus::NotActive)
                ++skippedNotActive;
            else if (match.status == PanelTargetMappingStatus::Ambiguous)
                ++ambiguousMappings;
            else
                ++unavailableMappings;
            continue;
        }

        const auto& target = windowsTargets.targets[*match.targetIndex];
        const bool duplicateTarget = std::any_of(seenTargets.begin(), seenTargets.end(),
            [&](const auto& prior)
            { return SameLuid(prior.first, target.adapterLuid) && prior.second == target.targetId; });
        if (duplicateTarget)
        {
            log_.push_back(std::string("Panel model=") + PanelKindName(kind) +
                " targetMapping=DuplicateExactTarget setter=0");
            ++ambiguousMappings;
            continue;
        }
        seenTargets.emplace_back(target.adapterLuid, target.targetId);
        log_.push_back(std::string("Panel model=") + PanelKindName(kind) +
            " targetMapping=Exact source=" + NarrowUtf8(target.sourceDeviceName) + " adapter=" +
            std::to_string(target.adapterLuid.HighPart) + ":" +
            std::to_string(target.adapterLuid.LowPart) + " target=" +
            std::to_string(target.targetId) + " monitorDevicePath=" +
            NarrowUtf8(target.monitorDevicePath) + " friendly=" +
            NarrowUtf8(target.friendlyName) +
            " mappingEvidence=normalized-WMI-PnP-instance" +
            " WMIActive=" + (panel.active ? "1" : "0"));
        work.push_back({ panel, kind, target });
    }

    if (work.empty())
    {
        IntelVrrResultStore::Clear();
        result.status = ambiguousMappings != 0 ? IntelVrrRunStatus::AmbiguousDisplay :
            unavailableMappings != 0 || !windowsTargets.enumerationComplete
                ? IntelVrrRunStatus::Unavailable : IntelVrrRunStatus::UnsupportedPanel;
        result.message = windowsTargets.enumerationComplete
            ? "No supported panel maps to an active Windows target."
            : "Active Windows target enumeration was incomplete.";
        summary_ = "targets=" + std::to_string(supported.size()) +
            " applied=0 alreadyCorrect=0 skipped=" + std::to_string(skippedNotActive) +
            " unavailable=" + std::to_string(unavailableMappings) +
            " ambiguous=" + std::to_string(ambiguousMappings) +
            " failed=0";
        log_.push_back("No exact active supported target; stale result cleared.");
        return result;
    }

    auto probe = probeFactory_ ? probeFactory_() : nullptr;
    const bool initialized = probe && probe->Initialize();
    if (!initialized)
        log_.push_back("IGCL initialization failed; no target setter called.");

    std::vector<IntelVrrRunResult> panelResults;
    for (const auto& item : work)
    {
        const auto panelClass = ProbePanelClass(item.kind);
        IntelVrrTargetState before;
        if (initialized)
            before = probe->Query(item.target.adapterLuid, item.target.targetId);
        const std::string label = PanelKindName(item.kind);
        if (!initialized)
        {
            panelResults.push_back(PanelResult(IntelVrrRunStatus::Unavailable,
                item.panel, nullptr, "IGCL initialization failed.", timestamp));
            log_.push_back("Panel=" + label + " IGCL=Unavailable setter=0");
            continue;
        }

        log_.push_back("Panel=" + label + " IGCL initialized=" +
            std::to_string(before.initialized) + " enumerationComplete=" +
            std::to_string(before.enumerationComplete) + " outputs=" +
            std::to_string(before.displayOutputCount) + " propertiesOk=" +
            std::to_string(before.displayPropertiesSuccessCount) + " targetMatches=" +
            std::to_string(before.targetMatchCount) + " mapping=" +
            (before.mappingStatus == IntelVrrTargetMappingStatus::Exact ? "Exact" :
                before.mappingStatus == IntelVrrTargetMappingStatus::Ambiguous ? "Ambiguous" : "Unknown") +
            " discoveryFailureStage=" +
            IntelVrrDiscoveryFailureStageName(before.discoveryFailureStage) +
            " discoveryFailureResult=" + ApiCode(before.discoveryFailureResult) +
            " capabilityResult=" + ApiCode(before.capabilityResult) +
            " profileResult=" + ApiCode(before.profileResult));

        log_.push_back("Panel=" + label + " capabilitySupported=" +
            (before.capability ? (before.capability->supported ? "1" : "0") : "unknown") +
            " capability=" + (before.capability
                ? Range(before.capability->minimumHz, before.capability->maximumHz) : "unknown") +
            " profile=" + (before.profile ? std::to_string(before.profile->profile) : "unknown") +
            " rangeBefore=" + (before.profile
                ? Range(before.profile->minimumHz, before.profile->maximumHz) : "unknown"));

        if (!ExactState(before))
        {
            const auto status = before.mappingStatus == IntelVrrTargetMappingStatus::Ambiguous
                ? IntelVrrRunStatus::AmbiguousDisplay : IntelVrrRunStatus::Unavailable;
            panelResults.push_back(PanelResult(status, item.panel, &before,
                "Could not query one complete exact IGCL output.", timestamp));
            log_.push_back("Panel=" + label + " decision=" +
                IntelVrrRunStatusName(status) + " setter=0");
            continue;
        }

        const auto decision = item.kind == SupportedPanelKind::ClawInternal
            ? EvaluateClawInternalProfile(before)
            : IntelVrrRunStatus::Unavailable;
        const auto lgDecision = item.kind == SupportedPanelKind::Lg27gs65f
            ? EvaluateLg27gs65fProductionProfile(before)
            : Lg27gs65fProfileDecision::Unavailable;
        IntelVrrRunStatus status = decision;
        if (item.kind == SupportedPanelKind::Lg27gs65f)
        {
            switch (lgDecision)
            {
            case Lg27gs65fProfileDecision::Unavailable: status = IntelVrrRunStatus::Unavailable; break;
            case Lg27gs65fProfileDecision::Unsupported: status = IntelVrrRunStatus::UnsupportedPanel; break;
            case Lg27gs65fProfileDecision::AlreadyCorrect: status = IntelVrrRunStatus::AlreadyCorrect; break;
            case Lg27gs65fProfileDecision::SkipUserProfile: status = IntelVrrRunStatus::SkippedUserProfile; break;
            case Lg27gs65fProfileDecision::ApplyExcellent: status = IntelVrrRunStatus::Applied; break;
            }
        }

        log_.push_back("Panel=" + label + " decision=" +
            IntelVrrRunStatusName(status));

        if (status != IntelVrrRunStatus::Applied)
        {
            panelResults.push_back(PanelResult(status, item.panel, &before,
                status == IntelVrrRunStatus::AlreadyCorrect ? "Full native VRR range already active." :
                status == IntelVrrRunStatus::SkippedUserProfile ? "Preserved user-selected profile." :
                status == IntelVrrRunStatus::UnsupportedPanel ? "Panel capability or profile is unsupported." :
                "Exact target state is unavailable.", timestamp));
            log_.push_back("Panel=" + label + " setter=0 result=" +
                IntelVrrRunStatusName(status));
            continue;
        }

        IntelVrrWindowsTargets currentTargets;
        try { currentTargets = windowsTargetProvider_(); }
        catch (...) { currentTargets.enumerationComplete = false; }
        const auto currentMatch = ResolvePanelWindowsTarget(item.panel, currentTargets);
        if (currentMatch.status != PanelTargetMappingStatus::Exact ||
            !currentMatch.targetIndex || !SameTarget(item.target,
                currentTargets.targets[*currentMatch.targetIndex]))
        {
            panelResults.push_back(PanelResult(IntelVrrRunStatus::AmbiguousDisplay,
                item.panel, &before, "Windows display target changed before apply.", timestamp));
            log_.push_back("Panel=" + label + " target revalidation failed; setter=0");
            continue;
        }

        const auto applied = probe->SetProfileForExactTarget(before, panelClass, kExcellent);
        if (applied.apiResult)
        {
            std::ostringstream api;
            api << "0x" << std::hex << *applied.apiResult;
            log_.push_back("Panel=" + label + " requestedProfile=EXCELLENT(2) setApi=" + api.str() +
                " attempted=" + std::to_string(applied.attempted) +
                " succeeded=" + std::to_string(applied.succeeded) +
                " detail=" + std::string(applied.detail));
        }
        else
            log_.push_back("Panel=" + label + " requestedProfile=EXCELLENT(2) setApi=none attempted=" +
                std::to_string(applied.attempted) + " detail=" + std::string(applied.detail));
        if (!applied.succeeded)
        {
            const auto failure = applied.attempted
                ? IntelVrrRunStatus::ApplyFailed : IntelVrrRunStatus::Unavailable;
            panelResults.push_back(PanelResult(failure, item.panel, &before,
                "EXCELLENT profile write was rejected.", timestamp));
            continue;
        }

        try { currentTargets = windowsTargetProvider_(); }
        catch (...) { currentTargets.enumerationComplete = false; }
        const auto afterMatch = ResolvePanelWindowsTarget(item.panel, currentTargets);
        if (afterMatch.status != PanelTargetMappingStatus::Exact ||
            !afterMatch.targetIndex || !SameTarget(item.target,
                currentTargets.targets[*afterMatch.targetIndex]))
        {
            panelResults.push_back(PanelResult(IntelVrrRunStatus::VerificationFailed,
                item.panel, &before, "Windows display target changed after apply.", timestamp));
            log_.push_back("Panel=" + label + " post-apply target revalidation failed.");
            continue;
        }

        const auto after = probe->Query(item.target.adapterLuid, item.target.targetId);
        if (after.profile)
            log_.push_back("Panel=" + label + " rangeAfter=" +
                Range(after.profile->minimumHz, after.profile->maximumHz) +
                " profileAfter=" + std::to_string(after.profile->profile));
        if (Verified(after, item.kind, kExcellent))
        {
            auto appliedResult = PanelResult(IntelVrrRunStatus::Applied, item.panel,
                &before, "Full native VRR range verified.", timestamp);
            appliedResult.rangeAfter = Range(after.profile->minimumHz, after.profile->maximumHz);
            panelResults.push_back(std::move(appliedResult));
            continue;
        }

        if (item.kind == SupportedPanelKind::Lg27gs65f &&
            ExactState(after) && after.capability->supported &&
            Near(after.capability->minimumHz, kLgMinimumHz, 0.25f) &&
            Near(after.capability->maximumHz, kLgMaximumHz, 0.25f) &&
            after.profile->profile == kExcellent)
        {
            IntelVrrWindowsTargets rollbackTargets;
            try { rollbackTargets = windowsTargetProvider_(); }
            catch (...) { rollbackTargets.enumerationComplete = false; }
            const auto rollbackMatch = ResolvePanelWindowsTarget(item.panel, rollbackTargets);
            if (rollbackMatch.status == PanelTargetMappingStatus::Exact &&
                rollbackMatch.targetIndex && SameTarget(item.target,
                    rollbackTargets.targets[*rollbackMatch.targetIndex]))
            {
                const auto rollback = probe->SetProfileForExactTarget(after, panelClass,
                    before.profile->profile);
                log_.push_back("Panel=LG27GS65F rollback reason=excellent-readback-mismatch requestedProfile=" +
                    std::to_string(before.profile->profile) + " attempted=" +
                    std::to_string(rollback.attempted) + " succeeded=" +
                    std::to_string(rollback.succeeded) + " api=" +
                    ApiCode(rollback.apiResult) + " detail=" + std::string(rollback.detail));
                if (rollback.succeeded)
                {
                    IntelVrrWindowsTargets restoredTargets;
                    try { restoredTargets = windowsTargetProvider_(); }
                    catch (...) { restoredTargets.enumerationComplete = false; }
                    const auto restoredMatch = ResolvePanelWindowsTarget(
                        item.panel, restoredTargets);
                    if (restoredMatch.status == PanelTargetMappingStatus::Exact &&
                        restoredMatch.targetIndex && SameTarget(item.target,
                            restoredTargets.targets[*restoredMatch.targetIndex]))
                    {
                        const auto reverted = probe->Query(item.target.adapterLuid,
                            item.target.targetId);
                        const bool restored = ExactState(reverted) && reverted.profile &&
                            reverted.profile->profile == before.profile->profile &&
                            Near(reverted.profile->minimumHz, before.profile->minimumHz, 0.25f) &&
                            Near(reverted.profile->maximumHz, before.profile->maximumHz, 0.25f);
                        log_.push_back(std::string("Panel=LG27GS65F rollbackVerified=") +
                            (restored ? "1" : "0"));
                    }
                    else
                        log_.push_back("Panel=LG27GS65F rollbackVerified=0 targetChanged=1");
                }
            }
            else log_.push_back("Panel=LG27GS65F rollback blocked: exact target changed.");
        }

        auto failedResult = PanelResult(IntelVrrRunStatus::VerificationFailed,
            item.panel, &before, "EXCELLENT profile readback did not match the model range.", timestamp);
        if (after.profile)
            failedResult.rangeAfter = Range(after.profile->minimumHz, after.profile->maximumHz);
        panelResults.push_back(std::move(failedResult));
    }

    if (probe) probe->Shutdown();
    result = Summarize(panelResults, timestamp);
    const auto count = [&](IntelVrrRunStatus status)
    {
        return std::count_if(panelResults.begin(), panelResults.end(),
            [status](const auto& item) { return item.status == status; });
    };
    summary_ = "targets=" + std::to_string(supported.size()) +
        " applied=" + std::to_string(count(IntelVrrRunStatus::Applied)) +
        " alreadyCorrect=" + std::to_string(count(IntelVrrRunStatus::AlreadyCorrect)) +
        " skipped=" + std::to_string(skippedNotActive +
            count(IntelVrrRunStatus::SkippedUserProfile)) +
        " unavailable=" + std::to_string(count(IntelVrrRunStatus::Unavailable) +
            count(IntelVrrRunStatus::AmbiguousDisplay) + unavailableMappings) +
        " ambiguous=" + std::to_string(ambiguousMappings) +
        " unsupported=" + std::to_string(count(IntelVrrRunStatus::UnsupportedPanel)) +
        " failed=" + std::to_string(count(IntelVrrRunStatus::ApplyFailed) +
            count(IntelVrrRunStatus::VerificationFailed));
    log_.push_back(std::string("Panel summary targets=") +
        summary_ + " result=" +
        IntelVrrRunStatusName(result.status) + " message=" + result.message);
    return finish(std::move(result));
}
}
