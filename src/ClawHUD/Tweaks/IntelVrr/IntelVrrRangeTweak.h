#pragma once

#include "AffectedPanelDetector.h"
#include "DiagIntelVrrStateProbe.h"
#include "IntelVrrRunResult.h"
#include "Lg27gs65fVrrRangeTweak.h"

#include <functional>
#include <memory>
#include <vector>

namespace clawhud
{
using IntelVrrTargetState = DiagIntelVrrState;
using IntelVrrTargetMappingStatus = DiagIgclTargetMappingStatus;
using IntelVrrArcSyncCapability = DiagArcSyncCapability;
using IntelVrrArcSyncProfile = DiagArcSyncProfile;
using IntelVrrProfileSetResult = DiagIgclProfileSetResult;

class IIntelVrrTargetStateProbe
{
public:
    virtual ~IIntelVrrTargetStateProbe() = default;
    virtual bool Initialize() noexcept = 0;
    virtual IntelVrrTargetState Query(const LUID& targetAdapterLuid,
        std::uint32_t targetId) noexcept = 0;
    virtual IntelVrrProfileSetResult SetProfileForExactTarget(
        const IntelVrrTargetState& expectedCurrentState,
        IntelVrrPanelClass panelClass, std::uint32_t requestedProfile) noexcept = 0;
    virtual void Shutdown() noexcept = 0;
};

class IntelVrrRangeTweak
{
public:
    using ProbeFactory = std::function<std::unique_ptr<IIntelVrrTargetStateProbe>()>;
    using PanelProvider = std::function<std::vector<PanelIdentity>()>;
    using WindowsTargetProvider = std::function<IntelVrrWindowsTargets()>;

    IntelVrrRangeTweak(ProbeFactory probeFactory, PanelProvider panelProvider,
        WindowsTargetProvider windowsTargetProvider = {});
    IntelVrrRunResult Run(bool enabled);
    const std::vector<std::string>& LastLog() const { return log_; }
    const std::string& LastSummary() const { return summary_; }

private:
    ProbeFactory probeFactory_;
    PanelProvider panelProvider_;
    WindowsTargetProvider windowsTargetProvider_;
    std::vector<std::string> log_;
    std::string summary_;
};

// Returns Applied to indicate the exact Claw target is eligible for EXCELLENT.
IntelVrrRunStatus EvaluateClawInternalProfile(const IntelVrrTargetState& state) noexcept;
}
