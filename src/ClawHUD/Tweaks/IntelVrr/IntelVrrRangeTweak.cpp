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

const char* ProbeFailureStageName(DiagIgclProbeFailureStage stage) noexcept
{
    switch (stage)
    {
    case DiagIgclProbeFailureStage::NotInitialized: return "NotInitialized";
    case DiagIgclProbeFailureStage::LoadLibrary: return "LoadLibrary";
    case DiagIgclProbeFailureStage::ResolveFunction: return "ResolveFunction";
    case DiagIgclProbeFailureStage::Initialize: return "Initialize";
    case DiagIgclProbeFailureStage::EnumerateDevices: return "EnumerateDevices";
    case DiagIgclProbeFailureStage::GetDeviceProperties: return "GetDeviceProperties";
    case DiagIgclProbeFailureStage::EnumerateDisplayOutputs: return "EnumerateDisplayOutputs";
    case DiagIgclProbeFailureStage::GetDisplayProperties: return "GetDisplayProperties";
    case DiagIgclProbeFailureStage::TargetMapping: return "TargetMapping";
    case DiagIgclProbeFailureStage::GetArcSyncInfo: return "GetArcSyncInfo";
    case DiagIgclProbeFailureStage::GetArcSyncProfile: return "GetArcSyncProfile";
    case DiagIgclProbeFailureStage::InternalError: return "InternalError";
    default: return "None";
    }
}

const char* ProbeFailureDomainName(DiagIgclProbeResultDomain domain) noexcept
{
    switch (domain)
    {
    case DiagIgclProbeResultDomain::Win32: return "Win32";
    case DiagIgclProbeResultDomain::ControlLibrary: return "ControlLibrary";
    default: return "None";
    }
}

void AppendProbeFailures(std::vector<std::string>& log, const std::string& panel,
    const IntelVrrTargetState& state)
{
    for (std::size_t index = 0;
        index < state.failureRecordCount && index < state.failures.size(); ++index)
    {
        const auto& failure = state.failures[index];
        std::string line = "Panel=" + panel + " probeFailure=" +
            std::to_string(index) + " stage=" + ProbeFailureStageName(failure.stage) +
            " domain=" + ProbeFailureDomainName(failure.resultDomain) +
            " result=" + ApiCode(failure.result) +
            " detail=" + std::string(failure.detail);
        if (failure.adapterIndex != DiagIgclProbeFailure::NoIndex)
            line += " adapterIndex=" + std::to_string(failure.adapterIndex);
        if (failure.outputIndex != DiagIgclProbeFailure::NoIndex)
            line += " outputIndex=" + std::to_string(failure.outputIndex);
        log.push_back(std::move(line));
    }
    if (state.suppressedFailureCount != 0)
        log.push_back("Panel=" + panel + " suppressedProbeFailures=" +
            std::to_string(state.suppressedFailureCount));
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

bool HasHalfFloorCapability(const IntelVrrTargetState& state) noexcept
{
    return state.capability && state.capability->supported &&
        Near(state.capability->minimumHz, 24.0f, 0.25f) &&
        Near(state.capability->maximumHz, 120.0f, 0.25f);
}

Tma2027TargetIdentity TmaTargetIdentity(const IntelVrrWindowsTarget& target)
{
    return { target.adapterLuid.HighPart, target.adapterLuid.LowPart,
        target.sourceId, target.targetId,
        NormalizeMonitorInstancePath(target.monitorDevicePath) };
}

Tma2027ProfileState TmaProfileState(const IntelVrrTargetState& state,
    const IntelVrrWindowsTarget& target)
{
    return { ExactState(state), state.capability && state.capability->supported,
        state.capability ? state.capability->minimumHz : 0.0f,
        state.capability ? state.capability->maximumHz : 0.0f,
        state.profile.has_value(), state.profile ? state.profile->profile : 0,
        state.profile ? state.profile->minimumHz : 0.0f,
        state.profile ? state.profile->maximumHz : 0.0f,
        TmaTargetIdentity(target) };
}

IntelVrrRunStatus ToRunStatus(Tma2027Decision decision) noexcept
{
    switch (decision)
    {
    case Tma2027Decision::Unavailable: return IntelVrrRunStatus::Unavailable;
    case Tma2027Decision::Unsupported: return IntelVrrRunStatus::UnsupportedPanel;
    case Tma2027Decision::AlreadyCorrect: return IntelVrrRunStatus::AlreadyCorrect;
    case Tma2027Decision::PreserveUserProfile: return IntelVrrRunStatus::SkippedUserProfile;
    case Tma2027Decision::ApplyExcellent: return IntelVrrRunStatus::Applied;
    default: return IntelVrrRunStatus::Unavailable;
    }
}

Tma2027EvidenceRequest TmaEvidenceRequest(const PanelIdentity& panel,
    const IntelVrrWindowsTarget& target)
{
    return { panel.manufacturer, panel.productCode, panel.panelName,
        NormalizeMonitorInstancePath(WidenUtf8(panel.instanceName)),
        NormalizeMonitorInstancePath(target.monitorDevicePath), target.sourceDeviceName,
        target.adapterLuid.HighPart, target.adapterLuid.LowPart,
        target.sourceId, target.targetId, target.available };
}

void LogTmaEvidence(std::vector<std::string>& log,
    const std::string& label, const Tma2027Evidence& evidence)
{
    std::string line = "Panel=" + label + " EDID=" +
        Tma2027EdidStatusName(evidence.edidStatus) + " edidSha256=" +
        (evidence.edidSha256.empty() ? "none" : evidence.edidSha256) +
        " directRange=" + Tma2027DirectRangeStatusName(evidence.directRange);
    if (!evidence.directRange.failureDetail.empty())
        line += " directDetail=" + evidence.directRange.failureDetail;
    if (evidence.directRange.win32Error != 0)
        line += " directWin32=0x" + ApiCode(evidence.directRange.win32Error);
    if (evidence.directRange.attempted)
    {
        line += " directNtStatus=" + ApiCode(static_cast<std::uint32_t>(
            evidence.directRange.ntStatus)) +
            " directAdapter=" +
            std::to_string(evidence.directRange.adapterLuidHighPart) + ":" +
            std::to_string(evidence.directRange.adapterLuidLowPart) +
            " directSource=" + std::to_string(evidence.directRange.sourceId) +
            " directTarget=" + std::to_string(evidence.directRange.targetId) +
            " directDisplayCount=" + std::to_string(evidence.directRange.displayCount) +
            " directSupported=" + (evidence.directRange.supported ? "1" : "0") +
            " directVrrEnabled=" + (evidence.directRange.vrrEnabled ? "1" : "0") +
            " directHz=" + (evidence.directRange.available
                ? Range(static_cast<float>(evidence.directRange.minimumHz),
                    static_cast<float>(evidence.directRange.maximumHz)) : "unknown");
    }
    log.push_back(std::move(line));
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
    switch (kind)
    {
    case SupportedPanelKind::ClawInternal: return "Claw8-CSW0801";
    case SupportedPanelKind::Claw7Internal: return "Claw7-TMA2027";
    case SupportedPanelKind::Lg27gs65f: return "LG27GS65F";
    default: return "Unsupported";
    }
}

IntelVrrPanelClass ProbePanelClass(SupportedPanelKind kind) noexcept
{
    switch (kind)
    {
    case SupportedPanelKind::ClawInternal: return IntelVrrPanelClass::ClawInternal;
    case SupportedPanelKind::Claw7Internal: return IntelVrrPanelClass::Claw7Internal;
    default: return IntelVrrPanelClass::Lg27gs65f;
    }
}

bool Verified(const IntelVrrTargetState& state, SupportedPanelKind kind,
    std::uint32_t profile, const IntelVrrWindowsTarget& target,
    const Tma2027Evidence* tmaEvidence) noexcept
{
    if (!ExactState(state) || !state.capability->supported ||
        state.profile->profile != profile)
        return false;
    if (kind == SupportedPanelKind::Claw7Internal)
    {
        const auto tmaState = TmaProfileState(state, target);
        return IsTma2027ExcellentReadbackVerified(tmaState, tmaEvidence);
    }
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
    PanelProvider panelProvider, WindowsTargetProvider windowsTargetProvider,
    TmaEvidenceProvider tmaEvidenceProvider)
    : probeFactory_(std::move(probeFactory)), panelProvider_(std::move(panelProvider)),
      windowsTargetProvider_(std::move(windowsTargetProvider)),
      tmaEvidenceProvider_(std::move(tmaEvidenceProvider))
{
    if (!windowsTargetProvider_)
        windowsTargetProvider_ = [] { return EnumerateVrrWindowsTargets(); };
    if (!tmaEvidenceProvider_)
        tmaEvidenceProvider_ = [](const PanelIdentity& panel,
            const IntelVrrWindowsTarget& target, bool queryDirectRange)
        {
            return CollectTma2027Evidence(TmaEvidenceRequest(panel, target),
                queryDirectRange);
        };
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
        result.status = IntelVrrRunStatus::Unavailable;
        result.message = "Could not enumerate active Windows display targets.";
        summary_ = "targets=" + std::to_string(supported.size()) +
            " applied=0 alreadyCorrect=0 skipped=0 unavailable=" +
            std::to_string(supported.size()) + " failed=0";
        log_.push_back("Windows target enumeration threw; supported candidates remain unresolved.");
        return finish(std::move(result));
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
    std::vector<IntelVrrRunResult> panelResults;
    std::vector<std::pair<LUID, UINT32>> seenTargets;
    std::size_t skippedNotActive{};
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
            if (kind == SupportedPanelKind::Claw7Internal)
                log_.push_back(std::string("Panel=") + PanelKindName(kind) +
                    " action=SKIPPED_UNSAFE reason=target_mapping_" + status +
                    " setter=0");
            if (match.status == PanelTargetMappingStatus::NotActive)
                ++skippedNotActive;
            else if (match.status == PanelTargetMappingStatus::Ambiguous)
            {
                panelResults.push_back(PanelResult(IntelVrrRunStatus::AmbiguousDisplay,
                    panel, nullptr, "Active Windows target mapping is ambiguous.", timestamp));
            }
            else
            {
                panelResults.push_back(PanelResult(IntelVrrRunStatus::Unavailable,
                    panel, nullptr, "Active Windows target enumeration is incomplete.", timestamp));
            }
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
            if (kind == SupportedPanelKind::Claw7Internal)
                log_.push_back(std::string("Panel=") + PanelKindName(kind) +
                    " action=SKIPPED_UNSAFE reason=duplicate_exact_target setter=0");
            panelResults.push_back(PanelResult(IntelVrrRunStatus::AmbiguousDisplay,
                panel, nullptr, "Multiple supported panels map to one exact Windows target.", timestamp));
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
        if (!panelResults.empty())
        {
            result = Summarize(panelResults, timestamp);
            const auto count = [&](IntelVrrRunStatus status)
            {
                return std::count_if(panelResults.begin(), panelResults.end(),
                    [status](const auto& item) { return item.status == status; });
            };
            summary_ = "targets=" + std::to_string(supported.size()) +
                " applied=0 alreadyCorrect=0 skipped=" + std::to_string(skippedNotActive) +
                " unavailable=" + std::to_string(count(IntelVrrRunStatus::Unavailable)) +
                " ambiguous=" + std::to_string(count(IntelVrrRunStatus::AmbiguousDisplay)) +
                " failed=0";
            log_.push_back("No writable target; mapping failures retained in aggregate result.");
            return finish(std::move(result));
        }

        IntelVrrResultStore::Clear();
        result.status = IntelVrrRunStatus::UnsupportedPanel;
        result.message = "No supported panel maps to an active Windows target.";
        summary_ = "targets=" + std::to_string(supported.size()) +
            " applied=0 alreadyCorrect=0 skipped=" + std::to_string(skippedNotActive) +
            " unavailable=0 ambiguous=0 failed=0";
        log_.push_back("All supported WMI candidates are inactive or disconnected; stale result cleared.");
        return result;
    }

    auto probe = probeFactory_ ? probeFactory_() : nullptr;
    const bool initialized = probe && probe->Initialize();
    if (!initialized)
        log_.push_back("IGCL initialization failed; no target setter called.");

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
            if (item.kind == SupportedPanelKind::Claw7Internal)
                log_.push_back("Panel=" + label +
                    " action=SKIPPED_UNSAFE reason=IGCL_unavailable setter=0");
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
            " failureRecords=" + std::to_string(before.failureRecordCount) +
            " suppressedFailures=" + std::to_string(before.suppressedFailureCount) +
            " capabilityResult=" + ApiCode(before.capabilityResult) +
            " profileResult=" + ApiCode(before.profileResult));
        AppendProbeFailures(log_, label, before);

        std::optional<Tma2027Evidence> tmaEvidence;
        if (item.kind == SupportedPanelKind::Claw7Internal)
        {
            try
            {
                tmaEvidence = tmaEvidenceProvider_(item.panel, item.target,
                    HasHalfFloorCapability(before));
            }
            catch (...)
            {
                Tma2027Evidence unavailable;
                unavailable.target = TmaTargetIdentity(item.target);
                unavailable.edidStatus = Tma2027EdidStatus::Unavailable;
                unavailable.directRange.attempted = HasHalfFloorCapability(before);
                unavailable.directRange.failureDetail = "evidence provider threw";
                tmaEvidence = std::move(unavailable);
            }
            if (tmaEvidence)
                LogTmaEvidence(log_, label, *tmaEvidence);
        }

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
            if (item.kind == SupportedPanelKind::Claw7Internal)
                log_.push_back("Panel=" + label +
                    " action=SKIPPED_UNSAFE reason=exact_IGCL_state_unavailable setter=0");
            continue;
        }

        const auto decision = item.kind == SupportedPanelKind::ClawInternal
            ? EvaluateClawInternalProfile(before)
            : item.kind == SupportedPanelKind::Claw7Internal
                ? ToRunStatus(EvaluateTma2027Profile(TmaProfileState(before, item.target),
                    tmaEvidence ? &*tmaEvidence : nullptr))
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
        if (item.kind == SupportedPanelKind::Claw7Internal)
        {
            const char* action = status == IntelVrrRunStatus::AlreadyCorrect
                ? "NO_WRITE_ALREADY_CORRECT" :
                status == IntelVrrRunStatus::SkippedUserProfile
                    ? before.profile && before.profile->profile == kCustom
                        ? "PRESERVED_OEM_CUSTOM" : "PRESERVED_USER_PROFILE" :
                status == IntelVrrRunStatus::Applied ? "APPLY_EXCELLENT" : "SKIPPED_UNSAFE";
            log_.push_back("Panel=" + label + " action=" + action + " setter=" +
                std::string(status == IntelVrrRunStatus::Applied ? "pending" : "0"));
        }

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
            if (item.kind == SupportedPanelKind::Claw7Internal)
                log_.push_back("Panel=" + label +
                    " action=SKIPPED_UNSAFE reason=pre_write_target_revalidation_failed setter=0");
            continue;
        }

        if (item.kind == SupportedPanelKind::Claw7Internal)
        {
            try
            {
                tmaEvidence = tmaEvidenceProvider_(item.panel, item.target,
                    HasHalfFloorCapability(before));
            }
            catch (...)
            {
                tmaEvidence.reset();
            }
            const auto expectedTmaTarget = TmaTargetIdentity(item.target);
            const bool physicalEdidVerified = tmaEvidence &&
                IsTma2027VerifiedPhysicalEdidEvidence(*tmaEvidence, expectedTmaTarget);
            const bool directRangeVerified = !HasHalfFloorCapability(before) ||
                (tmaEvidence && IsTma2027VerifiedHalfFloorEvidence(*tmaEvidence,
                    expectedTmaTarget));
            if (!physicalEdidVerified || !directRangeVerified)
            {
                if (tmaEvidence)
                    LogTmaEvidence(log_, label, *tmaEvidence);
                panelResults.push_back(PanelResult(IntelVrrRunStatus::Unavailable,
                    item.panel, &before,
                    "TMA2027 evidence changed or is unavailable before EXCELLENT.", timestamp));
                log_.push_back("Panel=" + label +
                    " action=SKIPPED_UNSAFE reason=pre-write evidence revalidation failed setter=0");
                continue;
            }
            if (tmaEvidence)
                LogTmaEvidence(log_, label, *tmaEvidence);
        }

        const auto applied = probe->SetProfileForExactTarget(before, panelClass, kExcellent,
            tmaEvidence ? &*tmaEvidence : nullptr);
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
            if (item.kind == SupportedPanelKind::Claw7Internal)
                log_.push_back("Panel=" + label + " action=" +
                    std::string(applied.attempted ? "APPLY_FAILED" : "SKIPPED_UNSAFE") +
                    " setter=" +
                    std::string(applied.attempted ? "1" : "0") + " api=" +
                    ApiCode(applied.apiResult));
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
            if (item.kind == SupportedPanelKind::Claw7Internal)
                log_.push_back("Panel=" + label +
                    " action=VERIFY_FAILED reason=post_write_target_revalidation_failed setter=1 readback=0");
            continue;
        }

        const auto after = probe->Query(item.target.adapterLuid, item.target.targetId);
        if (item.kind == SupportedPanelKind::Claw7Internal)
        {
            log_.push_back("Panel=" + label + " IGCL_AFTER initialized=" +
                std::to_string(after.initialized) + " enumerationComplete=" +
                std::to_string(after.enumerationComplete) + " outputs=" +
                std::to_string(after.displayOutputCount) + " propertiesOk=" +
                std::to_string(after.displayPropertiesSuccessCount) + " targetMatches=" +
                std::to_string(after.targetMatchCount) + " mapping=" +
                (after.mappingStatus == IntelVrrTargetMappingStatus::Exact ? "Exact" :
                    after.mappingStatus == IntelVrrTargetMappingStatus::Ambiguous ? "Ambiguous" : "Unknown") +
                " capabilitySupported=" +
                (after.capability ? (after.capability->supported ? "1" : "0") : "unknown") +
                " capability=" + (after.capability
                    ? Range(after.capability->minimumHz, after.capability->maximumHz) : "unknown") +
                " profile=" + (after.profile ? std::to_string(after.profile->profile) : "unknown") +
                " range=" + (after.profile
                    ? Range(after.profile->minimumHz, after.profile->maximumHz) : "unknown") +
                " capabilityResult=" + ApiCode(after.capabilityResult) +
                " profileResult=" + ApiCode(after.profileResult));
            AppendProbeFailures(log_, label + "-AFTER", after);
        }
        std::optional<Tma2027Evidence> afterEvidence;
        if (item.kind == SupportedPanelKind::Claw7Internal)
        {
            try
            {
                afterEvidence = tmaEvidenceProvider_(item.panel, item.target,
                    HasHalfFloorCapability(after));
            }
            catch (...)
            {
                Tma2027Evidence unavailable;
                unavailable.target = TmaTargetIdentity(item.target);
                unavailable.edidStatus = Tma2027EdidStatus::Unavailable;
                unavailable.directRange.failureDetail = "post-write evidence provider threw";
                unavailable.directRange.attempted = HasHalfFloorCapability(after);
                afterEvidence = std::move(unavailable);
            }
            LogTmaEvidence(log_, label, *afterEvidence);
        }
        if (after.profile)
            log_.push_back("Panel=" + label + " rangeAfter=" +
                Range(after.profile->minimumHz, after.profile->maximumHz) +
                " profileAfter=" + std::to_string(after.profile->profile));
        if (Verified(after, item.kind, kExcellent, item.target,
                afterEvidence ? &*afterEvidence : nullptr))
        {
            auto appliedResult = PanelResult(IntelVrrRunStatus::Applied, item.panel,
                &before, item.kind == SupportedPanelKind::Claw7Internal
                    ? "TMA2027 EXCELLENT 48-120 Hz profile verified."
                    : "Full native VRR range verified.", timestamp);
            appliedResult.rangeAfter = Range(after.profile->minimumHz, after.profile->maximumHz);
            panelResults.push_back(std::move(appliedResult));
            if (item.kind == SupportedPanelKind::Claw7Internal)
                log_.push_back("Panel=" + label + " action=APPLY_EXCELLENT verified=1");
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
        if (item.kind == SupportedPanelKind::Claw7Internal)
            log_.push_back("Panel=" + label + " action=VERIFY_FAILED setter=1 readback=0");
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
        " unavailable=" + std::to_string(count(IntelVrrRunStatus::Unavailable)) +
        " ambiguous=" + std::to_string(count(IntelVrrRunStatus::AmbiguousDisplay)) +
        " unsupported=" + std::to_string(count(IntelVrrRunStatus::UnsupportedPanel)) +
        " failed=" + std::to_string(count(IntelVrrRunStatus::ApplyFailed) +
            count(IntelVrrRunStatus::VerificationFailed));
    log_.push_back(std::string("Panel summary targets=") +
        summary_ + " result=" +
        IntelVrrRunStatusName(result.status) + " message=" + result.message);
    return finish(std::move(result));
}
}
