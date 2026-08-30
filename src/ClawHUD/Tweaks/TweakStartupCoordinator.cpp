#include "TweakStartupCoordinator.h"
#include "IntelVrr\AffectedPanelDetector.h"
#include "IntelVrr\IntelArcSyncClient.h"
#include "IntelVrr\IntelVrrRangeTweak.h"
#include <algorithm>
#include <chrono>

namespace clawhud
{
TweakStartupCoordinator::~TweakStartupCoordinator() { Stop(); }
void TweakStartupCoordinator::Start(bool enabled)
{
    Stop(); stopping_ = false; worker_ = std::thread(&TweakStartupCoordinator::Run, this, enabled);
}
void TweakStartupCoordinator::Stop()
{
    stopping_ = true; if (worker_.joinable()) worker_.join();
}
void TweakStartupCoordinator::Run(bool enabled)
{
    try
    {
    RunRetrySequence(
        [&](int)
        {
            IntelVrrRangeTweak tweak([] { return std::make_unique<IntelArcSyncClient>(); }, [] { return EnumeratePanelIdentities(); });
            return tweak.Run(enabled);
        },
        [&](std::chrono::seconds delay)
        {
            for (auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(delay);
                remaining.count() > 0 && !stopping_; remaining -= std::chrono::milliseconds(100))
            {
                std::this_thread::sleep_for(std::min(remaining, std::chrono::milliseconds(100)));
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
