#pragma once

#include <windows.h>

namespace clawhud
{
struct HudPresentationRefreshPlan
{
    bool recreate{};
    bool restoreVisibility{};
    bool enablePresentStatisticsDiagnostics{};
};

constexpr HudPresentationRefreshPlan BuildHudPresentationRefreshPlan(
    bool displayChangePending,
    bool visible,
    bool enablePresentStatisticsDiagnostics = false) noexcept
{
    return {
        displayChangePending,
        displayChangePending && visible,
        enablePresentStatisticsDiagnostics
    };
}
}
