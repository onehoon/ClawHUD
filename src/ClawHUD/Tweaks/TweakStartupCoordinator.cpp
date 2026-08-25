#include "TweakStartupCoordinator.h"
#include "IntelVrr\AffectedPanelDetector.h"
#include "IntelVrr\IntelArcSyncClient.h"
#include "IntelVrr\IntelVrrRangeTweak.h"
#include "IntelVrr\IntelVrrRunLogger.h"
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
    IntelVrrRunLogger::StartSession();
    const int delays[] = { 2, 5, 15 };
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        if (stopping_) return;
        IntelVrrRangeTweak tweak([] { return std::make_unique<IntelArcSyncClient>(); }, [] { return EnumeratePanelIdentities(); });
        const auto result = tweak.Run(enabled); IntelVrrRunLogger::AppendAttempt(attempt + 1, tweak.LastLog());
        if (result.status != IntelVrrRunStatus::Unavailable || !enabled) return;
        if (attempt < 3) for (int i = 0; i < delays[attempt] * 10 && !stopping_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    }
    catch (...)
    {
        // A startup tweak failure must never terminate the tray process.
    }
}
}
