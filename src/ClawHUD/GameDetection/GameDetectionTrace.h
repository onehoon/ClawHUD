#pragma once

#include "GameDetectionCoordinator.h"

#include <string_view>

namespace clawhud
{
std::wstring_view GameDetectionStateName(GameDetectionState state) noexcept;
std::wstring_view GameDetectionTriggerName(GameDetectionTrigger trigger) noexcept;
std::wstring_view GameDetectionTransitionName(
    GameDetectionTransition transition) noexcept;
}
