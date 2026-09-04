#include "HudPresentation.h"
#include "HudPresentationLifecycle.h"

#include <iostream>

int main()
{
    bool ok = true;
    const auto expect = [&](bool condition, const char* name)
    {
        if (!condition) std::cerr << "FAILED: " << name << '\n';
        ok &= condition;
    };
    const auto hidden = clawhud::BuildHudPresentationRefreshPlan(false, true);
    expect(!hidden.recreate && !hidden.restoreVisibility, "no pending change");
    const auto visible = clawhud::BuildHudPresentationRefreshPlan(true, true);
    expect(visible.recreate && visible.restoreVisibility, "visible recreation restores visibility");
    const auto wasHidden = clawhud::BuildHudPresentationRefreshPlan(true, false);
    expect(wasHidden.recreate && !wasHidden.restoreVisibility, "hidden recreation stays hidden");

    clawhud::HudRenderOptions initialized{};
    initialized.barPixelHeight = 30.0f;
    initialized.layout.backgroundOpacity = 0.5f;
    clawhud::HudRenderOptions requested = initialized;
    requested.layout.backgroundOpacity = 0.0f;
    const auto transparent = clawhud::BuildEffectiveHudRenderOptions(requested, initialized, 144.0f);
    expect(transparent.layout.backgroundOpacity == 0.0f,
        "runtime opacity remains requested at render boundary");
    expect(transparent.barPixelHeight == 30.0f && transparent.dpi == 144.0f,
        "initialized geometry and runtime dpi are preserved");
    requested.layout.backgroundOpacity = 1.0f;
    const auto opaque = clawhud::BuildEffectiveHudRenderOptions(requested, initialized, 144.0f);
    expect(opaque.layout.backgroundOpacity == 1.0f,
        "runtime opacity propagates to opaque render boundary");

    // --- first-visible presentation warm-up one-shot -----------------------
    using clawhud::ShouldScheduleFirstVisibleHudWarmup;
    expect(ShouldScheduleFirstVisibleHudWarmup(false, true, true, S_OK),
        "visible non-empty S_OK first frame schedules warm-up");
    expect(!ShouldScheduleFirstVisibleHudWarmup(false, true, false, S_OK),
        "empty HUD frame does not schedule warm-up");
    expect(!ShouldScheduleFirstVisibleHudWarmup(false, true, true, S_FALSE),
        "S_FALSE (no presentation buffer) does not schedule warm-up");
    expect(!ShouldScheduleFirstVisibleHudWarmup(false, false, true, S_OK),
        "a hidden render does not schedule warm-up");
    expect(!ShouldScheduleFirstVisibleHudWarmup(true, true, true, S_OK),
        "the one-shot never re-arms once attempted");
    expect(!ShouldScheduleFirstVisibleHudWarmup(false, true, true, E_FAIL),
        "a failed render does not schedule warm-up");

    return ok ? 0 : 1;
}
