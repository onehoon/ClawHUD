#pragma once

#include "GameDetectionCoordinator.h"

namespace clawhud
{
class SteamRunningAppTrigger
{
public:
    explicit SteamRunningAppTrigger(GameDetectionCoordinator& coordinator) noexcept
        : coordinator_(coordinator)
    {
    }

    GameDetectionTransitionResult Initialize(std::uint32_t currentAppId) noexcept;
    GameDetectionTransitionResult ObserveChange(
        std::uint32_t previousAppId, std::uint32_t currentAppId) noexcept;

private:
    GameDetectionTransitionResult NoTransition() const noexcept;

    GameDetectionCoordinator& coordinator_;
};
}
