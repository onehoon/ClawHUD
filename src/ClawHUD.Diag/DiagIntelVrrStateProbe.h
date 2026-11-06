#pragma once

#include "Tweaks/IntelVrr/Probe/IntelVrrTargetStateProbe.h"

using DiagIgclTargetMappingStatus = clawhud::IntelVrrTargetMappingStatus;
using DiagIgclTargetMatch = clawhud::IntelVrrTargetMatch;
using DiagIgclOutputIdentity = clawhud::IntelVrrOutputIdentity;
using DiagArcSyncCapability = clawhud::IntelVrrArcSyncCapability;
using DiagArcSyncProfile = clawhud::IntelVrrArcSyncProfile;
using DiagIgclProbeFailureStage = clawhud::IntelVrrProbeFailureStage;
using DiagIgclProbeResultDomain = clawhud::IntelVrrProbeResultDomain;
using DiagIgclProbeFailure = clawhud::IntelVrrProbeFailure;
using DiagIntelVrrState = clawhud::IntelVrrTargetState;
using DiagIgclProfileSetResult = clawhud::IntelVrrProfileSetResult;
using IntelVrrPanelClass = clawhud::IntelVrrPanelClass;

inline constexpr std::size_t kMaximumDiagIgclFailureRecords =
    clawhud::kMaximumIntelVrrFailureRecords;

inline DiagIgclTargetMatch ResolveDiagIgclTarget(
    const LUID& windowsTargetAdapterLuid, std::uint32_t windowsTargetId,
    std::span<const DiagIgclOutputIdentity> outputs,
    bool enumerationComplete = true) noexcept
{
    return clawhud::ResolveIntelVrrTarget(windowsTargetAdapterLuid,
        windowsTargetId, outputs, enumerationComplete);
}

using clawhud::IsIntelVrrProfileChangeAllowed;

// Source-compatible Diag facade. The legacy overload is intentionally LG-only.
class DiagIntelVrrStateProbe
{
public:
    bool Initialize() noexcept { return probe_.Initialize(); }

    DiagIntelVrrState Query(const LUID& windowsTargetAdapterLuid,
        std::uint32_t windowsTargetId) noexcept
    {
        return probe_.Query(windowsTargetAdapterLuid, windowsTargetId);
    }

    DiagIgclProfileSetResult SetProfileForExactTarget(
        const DiagIntelVrrState& expectedCurrentState,
        std::uint32_t requestedProfile) noexcept
    {
        return probe_.SetProfileForExactTarget(expectedCurrentState,
            IntelVrrPanelClass::Lg27gs65f, requestedProfile);
    }

    DiagIgclProfileSetResult SetProfileForExactTarget(
        const DiagIntelVrrState& expectedCurrentState,
        IntelVrrPanelClass panelClass, std::uint32_t requestedProfile,
        const clawhud::Tma2027Evidence* tmaEvidence = nullptr) noexcept
    {
        return probe_.SetProfileForExactTarget(expectedCurrentState, panelClass,
            requestedProfile, tmaEvidence);
    }

    void Shutdown() noexcept { probe_.Shutdown(); }

private:
    clawhud::IntelVrrTargetStateProbe probe_;
};
