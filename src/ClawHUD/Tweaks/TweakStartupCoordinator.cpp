#include "TweakStartupCoordinator.h"

#include "IntelVrr/AffectedPanelDetector.h"
#include "IntelVrr/IntelVrrRangeTweak.h"
#include "IntelVrr/Probe/IntelVrrTargetStateProbe.h"
#include "RuntimeLogger.h"
#include "WindowsDrr/WindowsDrrDisable.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

namespace clawhud
{
namespace
{
void LogDrrResult(const DrrDisableResult& result)
{
    const auto level = result.status == DrrDisableStatus::Applied ||
        result.status == DrrDisableStatus::AlreadyOff
        ? RuntimeLogLevel::Info
        : result.status == DrrDisableStatus::Unsupported ||
            result.status == DrrDisableStatus::Unavailable
            ? RuntimeLogLevel::Warn : RuntimeLogLevel::Error;
    RuntimeLogger::Log(level,
        std::wstring(L"Windows DRR startup result=") + DrrDisableStatusName(result.status) +
        L" activePathCount=" + std::to_wstring(result.activePathCount) +
        L" changedPathCount=" + std::to_wstring(result.changedPathCount) +
        L" win32=" + std::to_wstring(result.win32Error));
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
    try
    {
        RunStartupSequence(enabled,
            []
            {
                try
                {
                    LogDrrResult(DisableWindowsDrrForActivePaths());
                }
                catch (...)
                {
                    RuntimeLogger::Log(RuntimeLogLevel::Error,
                        L"Windows DRR startup attempt failed unexpectedly; continuing Intel VRR stage");
                }
            },
            [&](int)
            {
                IntelVrrRangeTweak tweak(
                    [] { return std::make_unique<IntelVrrTargetStateProbe>(); },
                    [] { return EnumeratePanelIdentities(); });
                return tweak.Run(enabled);
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
    }
    catch (...)
    {
        // A startup tweak failure must never terminate the tray process.
    }
}
}