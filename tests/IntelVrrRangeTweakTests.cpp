#include "Tweaks/IntelVrr/IntelVrrRangeTweak.h"
#include "Tweaks/IntelVrr/IntelVrrResultStore.h"
#include "Tweaks/TweakStartupCoordinator.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace clawhud;
namespace
{
bool Check(bool value, const char* message)
{
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}

PanelIdentity Claw(bool active = true, const char* instance = "DISPLAY\\CSW0801\\5&1&UID1_0")
{
    return { "CSW", "0801", "PN8007QB1-2", active, instance };
}

PanelIdentity Lg(bool active = true, const char* instance = "DISPLAY\\GSM5C8C\\5&2&UID2_0")
{
    return { "GSM", "5C8C", "LG ULTRAGEAR", active, instance };
}

PanelIdentity Claw7(bool active = true,
    const char* instance = "DISPLAY\\TMA2027\\5&3&UID3_0")
{
    return { "TMA", "2027", "TL070FVXS02-0", active, instance };
}

IntelVrrWindowsTarget ClawTarget(UINT32 targetId = 11)
{
    return { L"\\\\.\\DISPLAY1",
        L"\\\\?\\DISPLAY#CSW0801#5&1&UID1#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}",
        L"PN8007QB1-2", LUID{ 101, 1 }, targetId, 1, true };
}

IntelVrrWindowsTarget LgTarget(UINT32 targetId = 22)
{
    return { L"\\\\.\\DISPLAY2",
        L"\\\\?\\DISPLAY#GSM5C8C#5&2&UID2#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}",
        L"LG ULTRAGEAR", LUID{ 202, 2 }, targetId, 2, true };
}

IntelVrrWindowsTarget Claw7Target(UINT32 targetId = 33)
{
    return { L"\\\\.\\DISPLAY3",
        L"\\\\?\\DISPLAY#TMA2027#5&3&UID3#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}",
        L"TL070FVXS02-0", LUID{ 303, 3 }, targetId, 3, true };
}

Tma2027Evidence TmaEvidenceFor(const IntelVrrWindowsTarget& target,
    bool queryDirectRange)
{
    Tma2027Evidence evidence;
    evidence.target = { target.adapterLuid.HighPart, target.adapterLuid.LowPart,
        target.sourceId, target.targetId,
        NormalizeMonitorInstancePath(target.monitorDevicePath) };
    evidence.edidStatus = Tma2027EdidStatus::Matched;
    evidence.edidSha256 = std::string(kTma2027PhysicalEdidSha256);
    if (queryDirectRange)
    {
        evidence.directRange = { true, true, true, true, true, 0,
            target.adapterLuid.HighPart, target.adapterLuid.LowPart,
            target.sourceId, target.targetId, 1, 48, 120 };
    }
    return evidence;
}

IntelVrrTargetState State(SupportedPanelKind kind, std::uint32_t profile,
    float rangeMinimum, float rangeMaximum)
{
    const float capabilityMaximum = kind == SupportedPanelKind::Lg27gs65f ? 180.0f : 120.0f;
    IntelVrrTargetState state;
    state.initialized = true;
    state.enumerationComplete = true;
    state.mappingStatus = IntelVrrTargetMappingStatus::Exact;
    state.targetMatchCount = 1;
    state.displayOutputCount = 2;
    state.displayPropertiesSuccessCount = 2;
    state.capabilityResult = 0;
    state.profileResult = 0;
    state.capability = IntelVrrArcSyncCapability{ true, 48.0f, capabilityMaximum, 0, 0 };
    state.profile = IntelVrrArcSyncProfile{ profile, rangeMaximum, rangeMinimum, 0, 0 };
    return state;
}

struct FakeProbeState
{
    bool initialize{ true };
    std::map<UINT32, IntelVrrTargetState> states;
    std::map<UINT32, IntelVrrTargetState> originalStates;
    std::vector<UINT32> setters;
    std::vector<IntelVrrPanelClass> setterPolicies;
    UINT32 failSetTarget{};
    UINT32 badReadbackTarget{};
    std::vector<Tma2027Evidence> setterEvidence;
};

class FakeProbe final : public IIntelVrrTargetStateProbe
{
public:
    explicit FakeProbe(std::shared_ptr<FakeProbeState> state) : state_(std::move(state)) {}
    bool Initialize() noexcept override { return state_->initialize; }
    IntelVrrTargetState Query(const LUID& adapter, std::uint32_t target) noexcept override
    {
        auto found = state_->states.find(target);
        if (found == state_->states.end()) return {};
        auto result = found->second;
        result.windowsTargetAdapterLuid = adapter;
        result.windowsTargetId = target;
        return result;
    }
    IntelVrrProfileSetResult SetProfileForExactTarget(const IntelVrrTargetState& current,
        IntelVrrPanelClass kind, std::uint32_t profile,
        const Tma2027Evidence* tmaEvidence) noexcept override
    {
        const UINT32 target = current.windowsTargetId;
        state_->setters.push_back(target);
        state_->setterPolicies.push_back(kind);
        if (tmaEvidence)
            state_->setterEvidence.push_back(*tmaEvidence);
        if (target == state_->failSetTarget)
            return { true, false, 5, "fake setter rejected" };
        auto& value = state_->states[target];
        if (profile == 2)
        {
            value.profile->profile = 2;
            value.profile->minimumHz = 48;
            value.profile->maximumHz = target == state_->badReadbackTarget
                ? 165.0f : value.capability->maximumHz;
        }
        else if (profile != 2)
        {
            const auto original = state_->originalStates.find(target);
            if (original != state_->originalStates.end())
                value = original->second;
        }
        return { true, true, 0, {} };
    }
    void Shutdown() noexcept override {}

private:
    std::shared_ptr<FakeProbeState> state_;
};

IntelVrrRunResult Run(const std::vector<PanelIdentity>& panels,
    const std::vector<IntelVrrWindowsTarget>& targets,
    std::shared_ptr<FakeProbeState> state, bool complete = true,
    IntelVrrRangeTweak::TmaEvidenceProvider evidenceProvider = {})
{
    IntelVrrRangeTweak tweak(
        [state] { return std::make_unique<FakeProbe>(state); },
        [panels] { return panels; },
        [targets, complete] { return IntelVrrWindowsTargets{ targets, complete, 0 }; },
        std::move(evidenceProvider));
    return tweak.Run(true);
}

std::shared_ptr<FakeProbeState> TmaProbeState(std::uint32_t profile,
    float profileMinimum, float profileMaximum, float capabilityMinimum = 48.0f)
{
    auto fake = std::make_shared<FakeProbeState>();
    auto state = State(SupportedPanelKind::Claw7Internal, profile,
        profileMinimum, profileMaximum);
    state.capability->minimumHz = capabilityMinimum;
    fake->states[33] = state;
    fake->originalStates = fake->states;
    return fake;
}
}

int main()
{
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    const std::wstring resultDirectory = std::wstring(tempPath) + L"ClawHUD-Vrr-MultiPanel-Test";
    IntelVrrResultStore::SetDataDirectoryOverrideForTests(resultDirectory);
    bool ok = true;

    ok &= Check(IsAffectedPanel(Claw()), "Claw panel model matches");
    ok &= Check(IsAffectedPanel(Claw(false)), "WMI Active does not gate model discovery");
    ok &= Check(IsAffectedPanel(Claw7(false)), "TMA2027 is an affected panel independent of WMI Active");
    ok &= Check(ClassifySupportedPanel(Claw7()) == SupportedPanelKind::Claw7Internal,
        "exact TMA manufacturer/product/panel-name triple is classified separately");
    ok &= Check(ClassifySupportedPanel(PanelIdentity{ "tma", "2027", "tl070fvxs02-0", true,
        "DISPLAY\\TMA2027\\5&3&UID3_0" }) == SupportedPanelKind::Claw7Internal,
        "TMA identity comparison is case insensitive");
    ok &= Check(!IsAffectedPanel(PanelIdentity{ "TMA", "2027", "TL070FVXS02-1", true, "x" }) &&
        !IsAffectedPanel(PanelIdentity{ "TMA", "2028", "TL070FVXS02-0", true, "x" }) &&
        !IsAffectedPanel(PanelIdentity{ "ABC", "2027", "TL070FVXS02-0", true, "x" }),
        "TMA classification rejects a wrong manufacturer, product or panel name");
    ok &= Check(IsLg27gs65fPanel(Lg(false)), "WMI Active does not gate LG discovery");
    ok &= Check(!IsAffectedPanel(PanelIdentity{ "ABC", "0801", "PN8007QB1-2", true, "x" }),
        "manufacturer mismatch is unsupported");
    ok &= Check(!IsLg27gs65fPanel(PanelIdentity{ "GSM", "0000", "LG ULTRAGEAR", true, "x" }),
        "LG product mismatch is unsupported");
    ok &= Check(IsLg27gs65fWindowsIdentity(
        L"\\\\?\\DISPLAY#GSM5C8C#5&2&UID2#{guid}", L"LG ULTRAGEAR"),
        "exact LG hardware ID and friendly name match");
    ok &= Check(!IsLg27gs65fWindowsIdentity(
        L"\\\\?\\DISPLAY#GSM0000#5&2&UID2#{guid}", L"LG ULTRAGEAR"),
        "other LG model path is rejected");

    IntelVrrWindowsTargets targets{ { ClawTarget(), LgTarget() }, true, 0 };
    ok &= Check(ResolvePanelWindowsTarget(Claw(), targets).status == PanelTargetMappingStatus::Exact,
        "WMI Claw instance maps to exact active Windows target");
    ok &= Check(ResolvePanelWindowsTarget(Lg(), targets).status == PanelTargetMappingStatus::Exact,
        "WMI LG instance maps to exact active Windows target");
    ok &= Check(ResolvePanelWindowsTarget(Claw7(),
        IntelVrrWindowsTargets{ { Claw7Target() }, true, 0 }).status == PanelTargetMappingStatus::Exact,
        "TMA WMI instance maps only to its exact active DISPLAY\\TMA2027 target");
    auto misleadingTmaPath = Claw7Target();
    misleadingTmaPath.monitorDevicePath =
        L"\\\\?\\DISPLAY#TMA2028#5&3&UID3#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}";
    ok &= Check(ResolvePanelWindowsTarget(Claw7(),
        IntelVrrWindowsTargets{ { misleadingTmaPath }, true, 0 }).status ==
        PanelTargetMappingStatus::NotActive,
        "TMA policy rejects a non-TMA2027 active Windows device path");
    ok &= Check(ResolvePanelWindowsTarget(Claw(), IntelVrrWindowsTargets{ {}, true, 0 }).status ==
        PanelTargetMappingStatus::NotActive, "WMI-only stale panel has no active target");
    auto differentInstance = ClawTarget();
    differentInstance.monitorDevicePath =
        L"\\\\?\\DISPLAY#CSW0801#5&1&UID999#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}";
    ok &= Check(ResolvePanelWindowsTarget(Claw(),
        IntelVrrWindowsTargets{ { differentInstance }, true, 0 }).status ==
        PanelTargetMappingStatus::NotActive,
        "same model with a different PnP instance is not accepted by substring");
    ok &= Check(ResolvePanelWindowsTarget(Claw(), IntelVrrWindowsTargets{ { ClawTarget(), ClawTarget() }, true, 0 }).status ==
        PanelTargetMappingStatus::Ambiguous, "duplicate Windows target mapping is rejected");
    ok &= Check(ResolvePanelWindowsTarget(Claw(), IntelVrrWindowsTargets{ { ClawTarget() }, false, 5 }).status ==
        PanelTargetMappingStatus::Unavailable, "incomplete Windows enumeration is not trusted");
    auto unavailable = ClawTarget(); unavailable.available = false;
    ok &= Check(ResolvePanelWindowsTarget(Claw(), IntelVrrWindowsTargets{ { unavailable }, true, 0 }).status ==
        PanelTargetMappingStatus::NotActive, "unavailable target is never writable");

    const IntelVrrRangeTweak::TmaEvidenceProvider matchingTmaEvidence =
        [](const PanelIdentity&, const IntelVrrWindowsTarget& target, bool direct)
        { return TmaEvidenceFor(target, direct); };
    {
        auto fake = TmaProbeState(1, 60, 120);
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::Applied &&
            fake->setters == std::vector<UINT32>{ 33 } &&
            fake->setterPolicies == std::vector<IntelVrrPanelClass>{ IntelVrrPanelClass::Claw7Internal },
            "native 48-120 TMA capability applies EXCELLENT to its exact output");
        ok &= Check(fake->setterEvidence.size() == 1 &&
            !fake->setterEvidence[0].directRange.attempted,
            "native TMA setter does not require the half-floor direct-range exception");
    }
    {
        auto fake = TmaProbeState(1, 60, 120);
        int evidenceReads{};
        auto unavailableBeforeWrite = [&](const PanelIdentity&,
            const IntelVrrWindowsTarget& target, bool direct)
        {
            if (++evidenceReads == 2)
                throw std::runtime_error("native TMA path does not need half-floor evidence");
            return TmaEvidenceFor(target, direct);
        };
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            unavailableBeforeWrite);
        ok &= Check(result.status == IntelVrrRunStatus::Applied &&
            evidenceReads == 3 && fake->setters == std::vector<UINT32>{ 33 },
            "native 48-120 TMA policy does not depend on half-floor evidence availability");
    }
    {
        auto fake = TmaProbeState(4, 48, 120);
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::AlreadyCorrect && fake->setters.empty(),
            "any recognized standard TMA profile already at 48-120 is left unchanged");
    }
    {
        auto fake = TmaProbeState(1, 60, 120, 24);
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::Applied &&
            fake->setters == std::vector<UINT32>{ 33 } &&
            fake->setterEvidence.size() == 1 &&
            fake->setterEvidence[0].directRange.minimumHz == 48,
            "24-120 TMA telemetry applies only with exact EDID and direct 48-120 evidence");
    }
    {
        auto fake = TmaProbeState(1, 60, 120, 24);
        auto missingEvidence = [](const PanelIdentity&, const IntelVrrWindowsTarget& target, bool)
        {
            auto evidence = TmaEvidenceFor(target, false);
            evidence.edidStatus = Tma2027EdidStatus::Unavailable;
            return evidence;
        };
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            missingEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::Unavailable && fake->setters.empty(),
            "missing TMA EDID or direct range evidence produces no write");
    }
    {
        auto fake = TmaProbeState(7, 30, 120, 24);
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::SkippedUserProfile && fake->setters.empty(),
            "TMA OEM CUSTOM 30-120 profile is preserved without a setter");
    }
    {
        auto fake = TmaProbeState(5, 48, 120);
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::SkippedUserProfile && fake->setters.empty(),
            "TMA OFF profile is preserved");
    }
    {
        auto fake = TmaProbeState(7, 48, 120);
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::SkippedUserProfile && fake->setters.empty(),
            "TMA CUSTOM is preserved even when its reported range looks native");
    }
    {
        auto fake = TmaProbeState(1, 60, 120, 15);
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::UnsupportedPanel && fake->setters.empty(),
            "unsupported 15-120 telemetry never enters the TMA exception");
    }
    {
        auto fake = TmaProbeState(1, 60, 120, 24);
        auto wrongTargetEvidence = [](const PanelIdentity&, const IntelVrrWindowsTarget& target, bool direct)
        {
            auto evidence = TmaEvidenceFor(target, direct);
            evidence.directRange.targetId++;
            return evidence;
        };
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            wrongTargetEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::Unavailable && fake->setters.empty(),
            "TMA direct evidence for a different target is rejected");
    }
    {
        auto fake = TmaProbeState(1, 60, 120, 24);
        fake->failSetTarget = 33;
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::ApplyFailed &&
            fake->setters == std::vector<UINT32>{ 33 },
            "TMA setter rejection is reported once without fallback writes");
    }
    {
        auto fake = TmaProbeState(1, 60, 120, 24);
        int evidenceReads{};
        auto changingEvidence = [&](const PanelIdentity&,
            const IntelVrrWindowsTarget& target, bool direct)
        {
            auto evidence = TmaEvidenceFor(target, direct);
            if (++evidenceReads == 2)
                evidence.edidStatus = Tma2027EdidStatus::OverridePresent;
            return evidence;
        };
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            changingEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::Unavailable &&
            evidenceReads == 2 && fake->setters.empty(),
            "TMA evidence is revalidated before write and a changed override blocks it");
    }
    {
        auto fake = TmaProbeState(1, 60, 120);
        fake->badReadbackTarget = 33;
        const auto result = Run({ Claw7() }, { Claw7Target() }, fake, true,
            matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::VerificationFailed &&
            fake->setters == std::vector<UINT32>{ 33 },
            "TMA setter success with wrong readback is not reported as applied");
    }
    {
        auto fake = TmaProbeState(1, 60, 120, 24);
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 1, 90, 180);
        const auto result = Run({ Claw7(), Lg() }, { Claw7Target(), LgTarget() }, fake,
            true, matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::Applied &&
            fake->setters == std::vector<UINT32>({ 33, 22 }) &&
            fake->setterPolicies == std::vector<IntelVrrPanelClass>(
                { IntelVrrPanelClass::Claw7Internal, IntelVrrPanelClass::Lg27gs65f }),
            "TMA and LG outputs are independently evaluated with distinct policies");
    }
    {
        auto secondPanel = Claw7(true, "DISPLAY\\TMA2027\\5&4&UID4_0");
        auto secondTarget = Claw7Target(34);
        secondTarget.sourceDeviceName = L"\\\\.\\DISPLAY4";
        secondTarget.monitorDevicePath =
            L"\\\\?\\DISPLAY#TMA2027#5&4&UID4#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}";
        secondTarget.sourceId = 4;
        auto fake = TmaProbeState(1, 60, 120);
        fake->states[34] = State(SupportedPanelKind::Claw7Internal, 1, 60, 120);
        fake->originalStates = fake->states;
        const auto result = Run({ Claw7(), secondPanel },
            { Claw7Target(), secondTarget }, fake, true, matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::Applied &&
            fake->setters == std::vector<UINT32>({ 33, 34 }),
            "two exact TMA2027 physical instances receive independent per-target writes");
    }
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 2, 48, 180);
        fake->originalStates = fake->states;
        auto duplicateTarget = Claw7Target();
        auto result = Run({ Claw7(), Lg() },
            { Claw7Target(), duplicateTarget, LgTarget() }, fake, true,
            matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::AmbiguousDisplay &&
            fake->setters.empty(),
            "ambiguous active TMA mapping remains in the aggregate while LG is isolated");
        const auto saved = IntelVrrResultStore::Load();
        ok &= Check(saved && saved->status == IntelVrrRunStatus::AmbiguousDisplay,
            "active supported-panel mapping failure is persisted in the aggregate result");
    }
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 2, 48, 180);
        fake->originalStates = fake->states;
        const auto result = Run({ Claw7(), Lg() }, { Claw7Target(), LgTarget() }, fake,
            true, matchingTmaEvidence);
        ok &= Check(result.status == IntelVrrRunStatus::Unavailable && fake->setters.empty(),
            "active TMA IGCL failure is retained without suppressing a healthy LG sibling");
        const auto saved = IntelVrrResultStore::Load();
        ok &= Check(saved && saved->status == IntelVrrRunStatus::Unavailable,
            "active IGCL failure remains visible in the persisted aggregate result");
    }

    const LUID exactAdapter{ 101, 1 };
    const std::vector<DiagIgclOutputIdentity> igclOutputs{
        { exactAdapter, 11 }, { LUID{ 999, 9 }, 11 } };
    ok &= Check(ResolveDiagIgclTarget(exactAdapter, 11, igclOutputs).status ==
        IntelVrrTargetMappingStatus::Exact,
        "IGCL target matching requires the exact adapter LUID and target ID");
    ok &= Check(ResolveDiagIgclTarget(exactAdapter, 12, igclOutputs).status ==
        IntelVrrTargetMappingStatus::Unknown,
        "same LUID with a different target ID does not map");
    ok &= Check(ResolveDiagIgclTarget(LUID{ 555, 5 }, 11, igclOutputs).status ==
        IntelVrrTargetMappingStatus::Unknown,
        "same target ID on a different adapter does not map");
    auto duplicateOutputs = igclOutputs;
    duplicateOutputs.push_back({ exactAdapter, 11 });
    ok &= Check(ResolveDiagIgclTarget(exactAdapter, 11, duplicateOutputs).status ==
        IntelVrrTargetMappingStatus::Ambiguous,
        "duplicate exact IGCL identity is ambiguous");
    ok &= Check(ResolveDiagIgclTarget(exactAdapter, 11, igclOutputs, false).status ==
        IntelVrrTargetMappingStatus::Unknown,
        "incomplete IGCL enumeration cannot confirm an exact target");

    auto clawCustom = State(SupportedPanelKind::ClawInternal, 7, 48, 60);
    ok &= Check(EvaluateClawInternalProfile(clawCustom) == IntelVrrRunStatus::SkippedUserProfile,
        "Claw custom profile remains protected");
    auto clawNative = State(SupportedPanelKind::ClawInternal, 1, 48, 120);
    ok &= Check(EvaluateClawInternalProfile(clawNative) == IntelVrrRunStatus::AlreadyCorrect,
        "Claw full native range is already correct");
    auto clawNarrow = State(SupportedPanelKind::ClawInternal, 1, 48, 60);
    ok &= Check(EvaluateClawInternalProfile(clawNarrow) == IntelVrrRunStatus::Applied,
        "Claw recommended narrow profile is eligible for fix");
    auto wrongCapability = State(SupportedPanelKind::ClawInternal, 1, 48, 60);
    wrongCapability.capability->maximumHz = 180;
    ok &= Check(EvaluateClawInternalProfile(wrongCapability) == IntelVrrRunStatus::UnsupportedPanel,
        "same-range external panel cannot use Claw policy");
    ok &= Check(IsIntelVrrProfileChangeAllowed(clawNarrow,
        IntelVrrPanelClass::ClawInternal, 2),
        "exact Claw target may apply EXCELLENT under 48-120 policy");
    ok &= Check(!IsIntelVrrProfileChangeAllowed(wrongCapability,
        IntelVrrPanelClass::ClawInternal, 2),
        "Claw setter policy rejects an external capability range");
    auto lgRecommended = State(SupportedPanelKind::Lg27gs65f, 4, 90, 180);
    ok &= Check(IsIntelVrrProfileChangeAllowed(lgRecommended,
        IntelVrrPanelClass::Lg27gs65f, 2),
        "exact LG 90-180 COMPATIBLE target may apply EXCELLENT");
    ok &= Check(!IsIntelVrrProfileChangeAllowed(lgRecommended,
        IntelVrrPanelClass::ClawInternal, 2),
        "LG profile state cannot pass the Claw setter policy");
    auto lgRollback = State(SupportedPanelKind::Lg27gs65f, 2, 48, 180);
    ok &= Check(IsIntelVrrProfileChangeAllowed(lgRollback,
        IntelVrrPanelClass::Lg27gs65f, 4),
        "verified LG target can restore its original COMPATIBLE profile");
    lgRollback.profile->maximumHz = 165.0f;
    ok &= Check(IsIntelVrrProfileChangeAllowed(lgRollback,
        IntelVrrPanelClass::Lg27gs65f, 4),
        "LG rollback remains allowed after EXCELLENT readback has a mismatched range");

    // LG active by itself: a stale WMI Claw record must not steal its IGCL output.
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 1, 90, 180);
        fake->originalStates = fake->states;
        auto result = Run({ Claw(true), Lg(true) }, { LgTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::Applied &&
            fake->setters == std::vector<UINT32>{ 22 },
            "LG-only Windows topology applies only to the exact LG target");
        ok &= Check(fake->setterPolicies == std::vector<IntelVrrPanelClass>{ IntelVrrPanelClass::Lg27gs65f },
            "LG target uses LG-specific capability policy");
    }

    // Claw-only active: an inactive LG candidate does not affect the internal policy.
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[11] = State(SupportedPanelKind::ClawInternal, 1, 48, 60);
        fake->originalStates = fake->states;
        auto result = Run({ Claw(true), Lg(false) }, { ClawTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::Applied &&
            fake->setters == std::vector<UINT32>{ 11 },
            "Claw-only topology applies only to the exact internal target");
        ok &= Check(fake->setterPolicies == std::vector<IntelVrrPanelClass>{ IntelVrrPanelClass::ClawInternal },
            "internal target retains 48-120 policy");
    }

    // Both displays are independent and each keeps its own capability policy.
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[11] = State(SupportedPanelKind::ClawInternal, 1, 48, 60);
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 4, 90, 180);
        fake->originalStates = fake->states;
        auto result = Run({ Claw(), Lg() }, { ClawTarget(), LgTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::Applied && fake->setters.size() == 2,
            "both active panels are processed independently");
        ok &= Check(fake->setterPolicies[0] == IntelVrrPanelClass::ClawInternal &&
            fake->setterPolicies[1] == IntelVrrPanelClass::Lg27gs65f,
            "each panel receives its model-specific setter policy");
        ok &= Check(result.panelName == "Multiple displays",
            "multiple panel results use a deterministic aggregate summary");
        const auto saved = IntelVrrResultStore::Load();
        ok &= Check(saved && saved->status == IntelVrrRunStatus::Applied &&
            saved->panelName == "Multiple displays", "one aggregate result is saved for IPC");
    }

    // The diagnostic stream keeps enough identity and IGCL state to explain a decision.
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 4, 90, 180);
        IntelVrrRangeTweak tweak(
            [fake] { return std::make_unique<FakeProbe>(fake); },
            [] { return std::vector<PanelIdentity>{ Lg() }; },
            [] { return IntelVrrWindowsTargets{ { LgTarget() }, true, 0 }; });
        tweak.Run(true);
        const auto& log = tweak.LastLog();
        const auto contains = [&](const char* text)
        {
            return std::any_of(log.begin(), log.end(), [text](const auto& line)
                { return line.find(text) != std::string::npos; });
        };
        ok &= Check(contains("monitorDevicePath=\\\\?\\DISPLAY#GSM5C8C#") &&
            contains("mappingEvidence=normalized-WMI-PnP-instance"),
            "Intel diagnostics include the exact Windows device path and WMI mapping evidence");
        ok &= Check(contains("capabilitySupported=1 capability=48-180 Hz") &&
            contains("requestedProfile=EXCELLENT(2) setApi=0x0"),
            "Intel diagnostics include capability support and the requested profile result");
    }

    // Active=false remains diagnostic metadata when an exact live target exists.
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 1, 90, 180);
        fake->originalStates = fake->states;
        const auto result = Run({ Lg(false) }, { LgTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::Applied && fake->setters.size() == 1,
            "WMI Active=false does not block an exact live target");
    }

    // Ambiguous, incomplete, and adapter/target-mismatched IGCL inventories never write.
    {
        auto fake = std::make_shared<FakeProbeState>();
        auto state = State(SupportedPanelKind::Lg27gs65f, 1, 90, 180);
        state.mappingStatus = IntelVrrTargetMappingStatus::Ambiguous;
        state.targetMatchCount = 2;
        fake->states[22] = state;
        const auto result = Run({ Lg() }, { LgTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::AmbiguousDisplay && fake->setters.empty(),
            "ambiguous IGCL exact-target match performs no setter call");
    }
    {
        auto fake = std::make_shared<FakeProbeState>();
        auto state = State(SupportedPanelKind::Lg27gs65f, 1, 90, 180);
        state.enumerationComplete = false;
        fake->states[22] = state;
        const auto result = Run({ Lg() }, { LgTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::Unavailable && fake->setters.empty(),
            "incomplete IGCL enumeration performs no setter call");
    }
    {
        auto fake = std::make_shared<FakeProbeState>();
        auto state = State(SupportedPanelKind::Lg27gs65f, 1, 90, 180);
        state.displayPropertiesSuccessCount = 1;
        fake->states[22] = state;
        const auto result = Run({ Lg() }, { LgTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::Unavailable && fake->setters.empty(),
            "partial IGCL display-property enumeration performs no setter call");
    }
    {
        auto fake = std::make_shared<FakeProbeState>();
        auto state = State(SupportedPanelKind::Lg27gs65f, 1, 90, 180);
        state.displayOutputCount = 0;
        state.displayPropertiesSuccessCount = 0;
        fake->states[22] = state;
        const auto result = Run({ Lg() }, { LgTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::Unavailable && fake->setters.empty(),
            "empty IGCL output enumeration performs no setter call");
    }

    // Stale WMI-only entries, wrong IDs, and duplicates do not write or retain stale IPC results.
    {
        auto fake = std::make_shared<FakeProbeState>();
        IntelVrrRunResult stale; stale.status = IntelVrrRunStatus::Applied;
        IntelVrrResultStore::Save(stale);
        const auto result = Run({ Claw(), Lg() }, {}, fake);
        ok &= Check(result.status == IntelVrrRunStatus::UnsupportedPanel && fake->setters.empty(),
            "WMI-only inactive candidates do not invoke setters");
        ok &= Check(!IntelVrrResultStore::Load(), "no exact active panel clears stale result");
    }
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 1, 90, 180);
        auto wrong = Lg(); wrong.productCode = "0000";
        const auto result = Run({ wrong, Lg(), Lg() }, { LgTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::Applied &&
            fake->setters == std::vector<UINT32>{ 22 },
            "wrong product is ignored and duplicate WMI identity is processed once");
    }

    // One unavailable target does not suppress a successful sibling panel.
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[11] = State(SupportedPanelKind::ClawInternal, 1, 48, 60);
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 1, 90, 180);
        fake->states[22].profileResult = 0x40000007;
        const auto result = Run({ Claw(), Lg() }, { ClawTarget(), LgTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::Unavailable &&
            fake->setters == std::vector<UINT32>{ 11 },
            "one unavailable panel does not block its successful sibling");
        const auto retry = Run({ Claw(), Lg() }, { ClawTarget(), LgTarget() }, fake);
        ok &= Check(retry.status == IntelVrrRunStatus::Unavailable &&
            fake->setters == std::vector<UINT32>{ 11 },
            "retry does not write again to a panel already confirmed correct");
    }
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 1, 90, 180);
        int targetReads{};
        auto changedTarget = LgTarget();
        changedTarget.targetId = 23;
        IntelVrrRangeTweak tweak(
            [fake] { return std::make_unique<FakeProbe>(fake); },
            [] { return std::vector<PanelIdentity>{ Lg() }; },
            [&]
            {
                ++targetReads;
                return IntelVrrWindowsTargets{
                    { targetReads == 1 ? LgTarget() : changedTarget }, true, 0 };
            });
        const auto result = tweak.Run(true);
        ok &= Check(result.status == IntelVrrRunStatus::AmbiguousDisplay &&
            fake->setters.empty(),
            "changed Windows target identity is rejected before the IGCL setter");
    }
    {
        auto fake = TmaProbeState(1, 60, 120);
        int targetReads{};
        auto changedTarget = Claw7Target();
        changedTarget.targetId = 34;
        IntelVrrRangeTweak tweak(
            [fake] { return std::make_unique<FakeProbe>(fake); },
            [] { return std::vector<PanelIdentity>{ Claw7() }; },
            [&]
            {
                ++targetReads;
                return IntelVrrWindowsTargets{
                    { targetReads <= 2 ? Claw7Target() : changedTarget }, true, 0 };
            },
            matchingTmaEvidence);
        const auto result = tweak.Run(true);
        ok &= Check(result.status == IntelVrrRunStatus::VerificationFailed &&
            fake->setters == std::vector<UINT32>{33},
            "TMA post-write target change is a verification failure, never false success");
    }
    {
        auto claw = std::make_shared<FakeProbeState>();
        claw->states[11] = State(SupportedPanelKind::ClawInternal, 1, 60, 120);
        claw->states[11].capability->minimumHz = 24.0f;
        auto result = Run({ Claw() }, { ClawTarget() }, claw);
        ok &= Check(result.status == IntelVrrRunStatus::UnsupportedPanel && claw->setters.empty(),
            "24-120 telemetry remains unsupported for CSW0801");

        auto lg = std::make_shared<FakeProbeState>();
        lg->states[22] = State(SupportedPanelKind::Lg27gs65f, 1, 90, 180);
        lg->states[22].capability->minimumHz = 24.0f;
        result = Run({ Lg() }, { LgTarget() }, lg);
        ok &= Check(result.status == IntelVrrRunStatus::UnsupportedPanel && lg->setters.empty(),
            "24-120 telemetry remains unsupported for GSM5C8C");
    }

    // A failed apply and a bad LG readback are reported without cross-target writes.
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[11] = State(SupportedPanelKind::ClawInternal, 1, 48, 60);
        fake->failSetTarget = 11;
        const auto result = Run({ Claw() }, { ClawTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::ApplyFailed &&
            fake->setters == std::vector<UINT32>{ 11 }, "setter failure is reported cleanly");
    }
    {
        auto fake = std::make_shared<FakeProbeState>();
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 4, 90, 180);
        fake->originalStates = fake->states;
        fake->badReadbackTarget = 22;
        const auto result = Run({ Lg() }, { LgTarget() }, fake);
        ok &= Check(result.status == IntelVrrRunStatus::VerificationFailed &&
            fake->setters == std::vector<UINT32>{ 22, 22 } &&
            fake->states[22].profile->profile == 4 &&
            fake->states[22].profile->minimumHz == 90.0f &&
            fake->states[22].profile->maximumHz == 180.0f,
            "LG failed readback restores and verifies the original COMPATIBLE profile");
    }

    // Existing 2/5/15 second bounded retry contract remains unchanged.
    {
        int attempts{};
        std::vector<std::chrono::seconds> delays;
        const int count = TweakStartupCoordinator::RunRetrySequence(
            [&](int) { ++attempts; return IntelVrrRunResult{
                attempts == 4 ? IntelVrrRunStatus::Applied : IntelVrrRunStatus::Unavailable }; },
            [&](auto delay) { delays.push_back(delay); return true; });
        ok &= Check(count == 4 && delays == std::vector<std::chrono::seconds>{
            std::chrono::seconds{2}, std::chrono::seconds{5}, std::chrono::seconds{15}},
            "Intel unavailable retry schedule remains bounded at 2/5/15 seconds");
    }
    {
        auto fake = TmaProbeState(1, 60, 120, 24);
        fake->states[22] = State(SupportedPanelKind::Lg27gs65f, 2, 48, 180);
        fake->originalStates = fake->states;
        int activeAttempt{};
        std::vector<std::chrono::seconds> delays;
        const auto transientEvidence = [&](const PanelIdentity&,
            const IntelVrrWindowsTarget& target, bool direct)
        {
            auto evidence = TmaEvidenceFor(target, direct);
            if (activeAttempt < 3)
                evidence.edidStatus = Tma2027EdidStatus::Unavailable;
            return evidence;
        };
        const auto attempts = TweakStartupCoordinator::RunRetrySequence(
            [&](int attempt)
            {
                activeAttempt = attempt;
                return Run({ Claw7(), Lg() }, { Claw7Target(), LgTarget() },
                    fake, true, transientEvidence);
            },
            [&](auto delay) { delays.push_back(delay); return true; });
        ok &= Check(attempts == 3 && delays == std::vector<std::chrono::seconds>{
                std::chrono::seconds{2}, std::chrono::seconds{5}} &&
            fake->setters == std::vector<UINT32>{33},
            "transient active TMA evidence retries while already-correct LG remains write-free");
    }

    IntelVrrResultStore::Clear();
    std::error_code cleanupError;
    std::filesystem::remove_all(resultDirectory, cleanupError);
    return ok ? 0 : 1;
}
