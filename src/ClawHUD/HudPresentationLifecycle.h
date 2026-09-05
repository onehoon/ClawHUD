#pragma once

#include <windows.h>

namespace clawhud
{
struct HudPresentationRefreshPlan
{
    bool recreate{};
    bool restoreVisibility{};
};

constexpr HudPresentationRefreshPlan BuildHudPresentationRefreshPlan(
    bool displayChangePending,
    bool visible) noexcept
{
    return { displayChangePending, displayChangePending && visible };
}
}
