#pragma once

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
