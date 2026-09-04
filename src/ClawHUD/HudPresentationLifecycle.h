#pragma once

#include <windows.h>

namespace clawhud
{
// One-shot per-process HUD presentation warm-up trigger. Field behaviour: the
// first Presentation API / DirectComposition HUD instance after process launch
// can end up visually covered by Edge/Steam even though the HUD HWND stays
// visible and topmost, and a full ClawHUD restart clears it. The bounded
// workaround recreates the presentation exactly once, deferred to a later
// message-pump turn, after the first genuinely visible non-empty HUD frame is
// presented. S_FALSE (no presentation buffer available) and an empty frame both
// fail the trigger; only an S_OK present of real HUD content qualifies.
constexpr bool ShouldScheduleFirstVisibleHudWarmup(bool attempted, bool visible,
    bool hasRenderableContent, HRESULT renderResult) noexcept
{
    return !attempted && visible && hasRenderableContent && renderResult == S_OK;
}

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
