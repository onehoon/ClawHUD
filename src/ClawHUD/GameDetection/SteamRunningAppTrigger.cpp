#include "SteamRunningAppTrigger.h"

namespace clawhud
{
GameDetectionTransitionResult SteamRunningAppTrigger::NoTransition() const noexcept
{
    const auto& context = coordinator_.Context();
    return {GameDetectionTransition::None, context.generation,
        context.candidateProcessId};
}

GameDetectionTransitionResult SteamRunningAppTrigger::Initialize(
    std::uint32_t currentAppId) noexcept
{
    if (currentAppId == 0)
        return NoTransition();
    return coordinator_.ObserveWake({
        GameDetectionTrigger::SteamRunningAppId, 0, nullptr, currentAppId, false});
}

GameDetectionTransitionResult SteamRunningAppTrigger::ObserveChange(
    std::uint32_t previousAppId, std::uint32_t currentAppId) noexcept
{
    if (previousAppId == currentAppId)
        return NoTransition();

    if (previousAppId != 0)
        coordinator_.ClearSteamSession(previousAppId);
    if (currentAppId == 0)
        return NoTransition();

    const auto& context = coordinator_.Context();
    if (context.state != GameDetectionState::Idle &&
        context.state != GameDetectionState::Armed)
        return NoTransition();
    return coordinator_.ObserveWake({
        GameDetectionTrigger::SteamRunningAppId, 0, nullptr, currentAppId, false});
}
}
