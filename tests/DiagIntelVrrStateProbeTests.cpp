#ifdef NDEBUG
#undef NDEBUG
#endif

#include "DiagIntelVrrStateProbe.h"
#include "DiagIntelVrrStateProbeAbi.h"
#include "Lg27gs65fVrrProfileCommand.h"

#include <cassert>
#include <array>
#include <string>
#include <utility>

namespace
{
DiagIntelVrrState ProfileState(std::uint32_t profile, float minimum, float maximum)
{
    DiagIntelVrrState state;
    state.initialized = state.enumerationComplete = true;
    state.mappingStatus = DiagIgclTargetMappingStatus::Exact;
    state.targetMatchCount = state.displayOutputCount =
        state.displayPropertiesSuccessCount = 1;
    state.capability = DiagArcSyncCapability{ true, 48.0f, 180.0f, 0, 0 };
    state.profile = DiagArcSyncProfile{ profile, maximum, minimum, 0, 0 };
    state.capabilityResult = state.profileResult = 0;
    return state;
}

Lg27gs65fVrrDecision Decide(Lg27gs65fVrrAction action,
    const DiagIntelVrrState& state, bool confirmed = true,
    bool identity = true, std::uint32_t paths = 1,
    std::uint32_t monitors = 1)
{
    return EvaluateLg27gs65fAction(action, identity, paths, monitors,
        confirmed, state);
}
}

int main()
{
    diag_igcl_abi::DisplayProperties displayProperties{};
    diag_igcl_abi::InitializeDisplayProperties(displayProperties);
    assert(displayProperties.size == sizeof(displayProperties));
    assert(displayProperties.version == 0);
    assert(displayProperties.displayTiming.size == sizeof(displayProperties.displayTiming));
    assert(displayProperties.displayTiming.version == 0);

    const LUID intel{ 17, 2 };
    const LUID discrete{ 21, 3 };
    const std::array<DiagIgclOutputIdentity, 3> outputs{{
        { intel, 17 }, { discrete, 0 }, { intel, 8 } }};
    const auto exact = ResolveDiagIgclTarget(intel, 8, outputs);
    assert(exact.status == DiagIgclTargetMappingStatus::Exact);
    assert(exact.outputIndex == 2);

    // The numeric target ID can repeat on a different adapter. Its LUID must
    // also match the Windows display path before the output is considered.
    const auto wrongAdapter = ResolveDiagIgclTarget(intel, 0, outputs);
    assert(wrongAdapter.status == DiagIgclTargetMappingStatus::Unknown);
    assert(!wrongAdapter.outputIndex);

    const std::array<DiagIgclOutputIdentity, 3> duplicateOutputs{{
        { intel, 17 }, { discrete, 17 }, { intel, 17 } }};
    const auto ambiguous = ResolveDiagIgclTarget(intel, 17, duplicateOutputs);
    assert(ambiguous.status == DiagIgclTargetMappingStatus::Ambiguous);
    assert(!ambiguous.outputIndex);

    const auto missing = ResolveDiagIgclTarget(intel, 4, outputs);
    assert(missing.status == DiagIgclTargetMappingStatus::Unknown);
    assert(!missing.outputIndex);

    const auto incomplete = ResolveDiagIgclTarget(intel, 8, outputs, false);
    assert(incomplete.status == DiagIgclTargetMappingStatus::Unknown);
    assert(!incomplete.outputIndex);

    const auto incompleteDuplicate = ResolveDiagIgclTarget(intel, 17,
        duplicateOutputs, false);
    assert(incompleteDuplicate.status == DiagIgclTargetMappingStatus::Ambiguous);
    assert(!incompleteDuplicate.outputIndex);

    assert(IsLg27gs65fIdentity(L"GSM", L"LG 27GS65F"));
    assert(IsLg27gs65fIdentity(L"gsm", L"LG ULTRAGEAR"));
    assert(!IsLg27gs65fIdentity(L"DEL", L"LG 27GS65F"));
    assert(!IsLg27gs65fIdentity(L"GSM", L"Generic PnP Monitor"));
    assert(!IsLg27gs65fIdentity(L"", L"LG ULTRAGEAR"));
    assert(IsLg27gs65fConfirmationAccepted("y"));
    assert(IsLg27gs65fConfirmationAccepted("Y"));
    assert(!IsLg27gs65fConfirmationAccepted(""));
    assert(!IsLg27gs65fConfirmationAccepted("n"));
    assert(!IsLg27gs65fConfirmationAccepted("yes"));
    assert(ParseLg27gs65fMenuSelection("1") == 1);
    assert(ParseLg27gs65fMenuSelection("2") == 2);
    assert(ParseLg27gs65fMenuSelection("3") == 3);
    assert(ParseLg27gs65fMenuSelection("") == 0);
    assert(ParseLg27gs65fMenuSelection("1x") == 0);
    assert(ParseLg27gs65fMenuSelection("4") == 0);

    const auto recommended = ProfileState(1, 90.0f, 180.0f);
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, recommended, false) ==
        Lg27gs65fVrrDecision::ConfirmationRequired);
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, recommended) ==
        Lg27gs65fVrrDecision::Write);
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, recommended, true, false) ==
        Lg27gs65fVrrDecision::Skip);
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, recommended, true, true, 2) ==
        Lg27gs65fVrrDecision::Skip);
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, recommended, true, true, 1, 2) ==
        Lg27gs65fVrrDecision::Skip);

    const auto excellent = ProfileState(2, 48.0f, 180.0f);
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, excellent) ==
        Lg27gs65fVrrDecision::AlreadyAtTarget);
    assert(Decide(Lg27gs65fVrrAction::RestoreRecommended, excellent, false) ==
        Lg27gs65fVrrDecision::ConfirmationRequired);
    assert(Decide(Lg27gs65fVrrAction::RestoreRecommended, excellent) ==
        Lg27gs65fVrrDecision::Write);
    for (const auto range : { std::pair{ 90.0f, 180.0f }, std::pair{ 60.0f, 180.0f } })
    {
        const auto failedApply = ProfileState(2, range.first, range.second);
        assert(Decide(Lg27gs65fVrrAction::RestoreRecommended,
            failedApply, false) == Lg27gs65fVrrDecision::ConfirmationRequired);
        assert(Decide(Lg27gs65fVrrAction::RestoreRecommended,
            failedApply) == Lg27gs65fVrrDecision::Write);
    }
    assert(Decide(Lg27gs65fVrrAction::RestoreRecommended,
        ProfileState(2, 0.0f, 180.0f)) == Lg27gs65fVrrDecision::Skip);
    assert(Decide(Lg27gs65fVrrAction::RestoreRecommended,
        ProfileState(2, 60.0f, 181.0f)) == Lg27gs65fVrrDecision::Skip);
    assert(Decide(Lg27gs65fVrrAction::RestoreRecommended,
        ProfileState(2, 100.0f, 90.0f)) == Lg27gs65fVrrDecision::Skip);
    assert(VerifyLg27gs65fAction(Lg27gs65fVrrAction::ApplyExcellent,
        true, true, excellent));
    assert(!VerifyLg27gs65fAction(Lg27gs65fVrrAction::ApplyExcellent,
        false, true, excellent));
    assert(!VerifyLg27gs65fAction(Lg27gs65fVrrAction::ApplyExcellent,
        true, false, excellent));
    assert(!VerifyLg27gs65fAction(Lg27gs65fVrrAction::ApplyExcellent,
        true, true, recommended));

    const auto restored = ProfileState(1, 90.0f, 180.0f);
    assert(Decide(Lg27gs65fVrrAction::RestoreRecommended, restored) ==
        Lg27gs65fVrrDecision::AlreadyAtTarget);
    assert(VerifyLg27gs65fAction(Lg27gs65fVrrAction::RestoreRecommended,
        true, true, restored));
    assert(!VerifyLg27gs65fAction(Lg27gs65fVrrAction::RestoreRecommended,
        true, true, excellent));
    assert(!VerifyLg27gs65fAction(Lg27gs65fVrrAction::RestoreRecommended,
        false, true, restored));
    assert(!VerifyLg27gs65fAction(Lg27gs65fVrrAction::RestoreRecommended,
        true, false, restored));
    assert(!VerifyLg27gs65fAction(Lg27gs65fVrrAction::RestoreRecommended,
        true, true, DiagIntelVrrState{}));
    for (const auto profile : { 3u, 5u, 7u, 99u })
        assert(Decide(Lg27gs65fVrrAction::ApplyExcellent,
            ProfileState(profile, 90.0f, 180.0f)) == Lg27gs65fVrrDecision::Skip);
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent,
        ProfileState(1, 89.0f, 180.0f)) == Lg27gs65fVrrDecision::Skip);

    auto unsafe = recommended;
    unsafe.capability->supported = false;
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, unsafe) ==
        Lg27gs65fVrrDecision::Skip);
    unsafe = recommended;
    unsafe.capability->maximumHz = 165.0f;
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, unsafe) ==
        Lg27gs65fVrrDecision::Skip);
    unsafe = recommended;
    unsafe.capabilityResult = 0x40000006;
    unsafe.capability.reset();
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, unsafe) ==
        Lg27gs65fVrrDecision::Skip);
    unsafe = recommended;
    unsafe.mappingStatus = DiagIgclTargetMappingStatus::Unknown;
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, unsafe) ==
        Lg27gs65fVrrDecision::Skip);
    unsafe.mappingStatus = DiagIgclTargetMappingStatus::Ambiguous;
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, unsafe) ==
        Lg27gs65fVrrDecision::Skip);
    unsafe = recommended;
    unsafe.targetMatchCount = 2;
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, unsafe) ==
        Lg27gs65fVrrDecision::Skip);
    unsafe.enumerationComplete = false;
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, unsafe) ==
        Lg27gs65fVrrDecision::Skip);
    unsafe = recommended;
    unsafe.displayPropertiesSuccessCount = 0;
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, unsafe) ==
        Lg27gs65fVrrDecision::Skip);
    unsafe = recommended;
    unsafe.profileResult = 0x40000006;
    unsafe.profile.reset();
    assert(Decide(Lg27gs65fVrrAction::ApplyExcellent, unsafe) ==
        Lg27gs65fVrrDecision::Skip);

    auto clawNarrow = ProfileState(1, 48.0f, 60.0f);
    clawNarrow.capability->maximumHz = 120.0f;
    assert(IsIntelVrrProfileChangeAllowed(clawNarrow,
        IntelVrrPanelClass::ClawInternal, 2));
    auto clawFullRange = ProfileState(1, 48.0f, 120.0f);
    clawFullRange.capability->maximumHz = 120.0f;
    assert(!IsIntelVrrProfileChangeAllowed(clawFullRange,
        IntelVrrPanelClass::ClawInternal, 2));
    auto clawCustom = ProfileState(7, 48.0f, 60.0f);
    clawCustom.capability->maximumHz = 120.0f;
    assert(!IsIntelVrrProfileChangeAllowed(clawCustom,
        IntelVrrPanelClass::ClawInternal, 2));
    assert(!IsIntelVrrProfileChangeAllowed(clawNarrow,
        IntelVrrPanelClass::ClawInternal, 4));
    assert(IsIntelVrrProfileChangeAllowed(recommended,
        IntelVrrPanelClass::Lg27gs65f, 2));
    assert(IsIntelVrrProfileChangeAllowed(excellent,
        IntelVrrPanelClass::Lg27gs65f, 1));
    assert(!IsIntelVrrProfileChangeAllowed(excellent,
        IntelVrrPanelClass::Lg27gs65f, 2));
    auto wrongClawCapability = clawNarrow;
    wrongClawCapability.capability->maximumHz = 180.0f;
    assert(!IsIntelVrrProfileChangeAllowed(wrongClawCapability,
        IntelVrrPanelClass::ClawInternal, 2));

    auto claw7Native = ProfileState(1, 60.0f, 120.0f);
    claw7Native.capability = DiagArcSyncCapability{ true, 48.0f, 120.0f, 0, 0 };
    claw7Native.windowsTargetAdapterLuid = LUID{ 303, 3 };
    claw7Native.windowsTargetId = 33;
    assert(IsIntelVrrProfileChangeAllowed(claw7Native,
        IntelVrrPanelClass::Claw7Internal, 2));
    auto claw7HalfFloor = claw7Native;
    claw7HalfFloor.capability->minimumHz = 24.0f;
    clawhud::Tma2027Evidence tmaEvidence;
    tmaEvidence.target = { 3, 303, 3, 33, L"display\\tma2027\\5&3&uid3" };
    tmaEvidence.edidStatus = clawhud::Tma2027EdidStatus::Matched;
    tmaEvidence.edidSha256 = std::string(clawhud::kTma2027PhysicalEdidSha256);
    tmaEvidence.directRange = { true, true, true, true, true, 0,
        3, 303, 3, 33, 1, 48, 120 };
    assert(!IsIntelVrrProfileChangeAllowed(claw7HalfFloor,
        IntelVrrPanelClass::Claw7Internal, 2));
    assert(IsIntelVrrProfileChangeAllowed(claw7HalfFloor,
        IntelVrrPanelClass::Claw7Internal, 2, &tmaEvidence));
    auto foreignEvidence = tmaEvidence;
    foreignEvidence.target.targetId = 34;
    foreignEvidence.directRange.targetId = 34;
    assert(!IsIntelVrrProfileChangeAllowed(claw7HalfFloor,
        IntelVrrPanelClass::Claw7Internal, 2, &foreignEvidence));
    auto claw7Custom = claw7HalfFloor;
    claw7Custom.profile->profile = 7;
    claw7Custom.profile->minimumHz = 30.0f;
    assert(!IsIntelVrrProfileChangeAllowed(claw7Custom,
        IntelVrrPanelClass::Claw7Internal, 2, &tmaEvidence));
    assert(!IsIntelVrrProfileChangeAllowed(claw7HalfFloor,
        IntelVrrPanelClass::Claw7Internal, 4, &tmaEvidence));
}
