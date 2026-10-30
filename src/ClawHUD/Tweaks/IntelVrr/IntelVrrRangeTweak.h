#pragma once

#include "AffectedPanelDetector.h"
#include "IntelVrrRunResult.h"
#include "Lg27gs65fVrrRangeTweak.h"
#include "Probe/IntelVrrTargetStateProbe.h"
#include "Tma2027VrrEvidence.h"

#include <functional>
#include <memory>
#include <vector>

namespace clawhud
{
class IntelVrrRangeTweak
{
public:
    using ProbeFactory = std::function<std::unique_ptr<IIntelVrrTargetStateProbe>()>;
    using PanelProvider = std::function<std::vector<PanelIdentity>()>;
    using WindowsTargetProvider = std::function<IntelVrrWindowsTargets()>;
    using TmaEvidenceProvider = std::function<Tma2027Evidence(const PanelIdentity&,
        const IntelVrrWindowsTarget&, bool)>;

    IntelVrrRangeTweak(ProbeFactory probeFactory, PanelProvider panelProvider,
        WindowsTargetProvider windowsTargetProvider = {},
        TmaEvidenceProvider tmaEvidenceProvider = {});
    IntelVrrRunResult Run(bool enabled);
    const std::vector<std::string>& LastLog() const { return log_; }
    const std::string& LastSummary() const { return summary_; }

private:
    ProbeFactory probeFactory_;
    PanelProvider panelProvider_;
    WindowsTargetProvider windowsTargetProvider_;
    TmaEvidenceProvider tmaEvidenceProvider_;
    std::vector<std::string> log_;
    std::string summary_;
};

// Returns Applied to indicate the exact Claw target is eligible for EXCELLENT.
IntelVrrRunStatus EvaluateClawInternalProfile(const IntelVrrTargetState& state) noexcept;
}
