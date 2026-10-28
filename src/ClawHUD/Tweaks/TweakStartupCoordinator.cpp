#include "TweakStartupCoordinator.h"

#include "IntelVrr/AffectedPanelDetector.h"
#include "IntelVrr/IntelVrrRangeTweak.h"
#include "IntelVrr/Probe/IntelVrrTargetStateProbe.h"
#include "VrrDiagnosticLogger.h"
#include "WindowsDrr/WindowsDrrDisable.h"
#include "Version.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace clawhud
{
namespace
{
const char* DrrStatusText(DrrDisableStatus status) noexcept
{
    switch (status)
    {
    case DrrDisableStatus::AlreadyOff: return "AlreadyOff";
    case DrrDisableStatus::Applied: return "Applied";
    case DrrDisableStatus::Unsupported: return "Unsupported";
    case DrrDisableStatus::Unavailable: return "Unavailable";
    case DrrDisableStatus::ValidateFailed: return "ValidateFailed";
    case DrrDisableStatus::ApplyFailed: return "ApplyFailed";
    case DrrDisableStatus::VerifyFailed: return "VerifyFailed";
    default: return "Unknown";
    }
}
}

TweakStartupCoordinator::~TweakStartupCoordinator() { Stop(); }

void TweakStartupCoordinator::Start(bool enabled)
{
    Stop();
    stopping_ = false;
    worker_ = std::thread(&TweakStartupCoordinator::Run, this, enabled);
}

void TweakStartupCoordinator::Stop()
{
    stopping_ = true;
    if (worker_.joinable()) worker_.join();
}

void TweakStartupCoordinator::Run(bool enabled)
{
    std::vector<PanelIdentity> initialPanels;
    bool initialPanelEnumerationSucceeded = !enabled;
    bool initialPanelEnumerationFailed = false;
    if (enabled)
    {
        try
        {
            initialPanels = EnumeratePanelIdentities();
            initialPanelEnumerationSucceeded = true;
        }
        catch (...)
        {
            initialPanelEnumerationFailed = true;
        }
    }

    const std::string candidateCount = enabled
        ? initialPanelEnumerationSucceeded ? std::to_string(initialPanels.size()) : "unavailable"
        : "0";
    VrrDiagnosticLogger::BeginRun(enabled, CLAWHUD_VERSION_UTF8, candidateCount);
    if (initialPanelEnumerationFailed)
        VrrDiagnosticLogger::Write("IGCL", "initial WMI discovery failed");
    std::string drrResult = enabled ? "Unavailable" : "Disabled";
    std::string intelSummary = "targets=0 applied=0 alreadyCorrect=0 skipped=0 unavailable=0 failed=0";
    IntelVrrRunResult lastIntelResult{};
    int attempts{};

    try
    {
        TweakStartupCoordinator::RunStartupSequence(enabled,
            [&]
            {
                try
                {
                    const auto result = DisableWindowsDrrForActivePaths();
                    drrResult = DrrStatusText(result.status);
                    VrrDiagnosticLogger::Write("DRR", "startup result=" + drrResult +
                        " activePathCount=" + std::to_string(result.activePathCount) +
                        " changedPathCount=" + std::to_string(result.changedPathCount) +
                        " win32=" + std::to_string(result.win32Error));
                }
                catch (...)
                {
                    drrResult = "Unavailable";
                    VrrDiagnosticLogger::Write("DRR",
                        "startup attempt threw; continuing Intel VRR stage");
                }
            },
            [&](int attemptNumber)
            {
                attempts = attemptNumber;
                auto panelProvider = [&, attemptNumber]
                {
                    if (attemptNumber == 1 && initialPanelEnumerationSucceeded)
                        return initialPanels;
                    return EnumeratePanelIdentities();
                };
                IntelVrrRangeTweak tweak(
                    [] { return std::make_unique<IntelVrrTargetStateProbe>(); },
                    std::move(panelProvider));
                lastIntelResult = tweak.Run(enabled);
                intelSummary = tweak.LastSummary();
                for (const auto& line : tweak.LastLog())
                    VrrDiagnosticLogger::Write("IGCL",
                        "attempt=" + std::to_string(attemptNumber) + " " + line);
                return lastIntelResult;
            },
            [&](std::chrono::seconds delay)
            {
                for (auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(delay);
                    remaining.count() > 0 && !stopping_;
                    remaining -= std::chrono::milliseconds(100))
                {
                    std::this_thread::sleep_for(
                        std::min(remaining, std::chrono::milliseconds(100)));
                }
                return !stopping_;
            });
        VrrDiagnosticLogger::EndRun("DRR=" + drrResult + " Intel=" +
            IntelVrrRunStatusName(lastIntelResult.status) + " attempts=" +
            std::to_string(attempts) + " " + intelSummary);
    }
    catch (...)
    {
        VrrDiagnosticLogger::EndRun("DRR=" + drrResult +
            " Intel=Unavailable attempts=" + std::to_string(attempts) +
            " " + intelSummary + " failure=unexpected-exception");
    }
}
}
