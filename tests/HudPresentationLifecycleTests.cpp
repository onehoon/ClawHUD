#include "HudPresentation.h"
#include "HudPresentationLifecycle.h"

#include <iostream>
#include <vector>

namespace
{
struct RenderProbe
{
    int presentCount{};
    std::vector<float> observedOpacity;
};

HRESULT Present(void* context) noexcept
{
    auto* probe = static_cast<RenderProbe*>(context);
    ++probe->presentCount;
    return S_OK;
}

void Observe(float opacity, HRESULT result, void* context) noexcept
{
    auto* probe = static_cast<RenderProbe*>(context);
    if (SUCCEEDED(result))
        probe->observedOpacity.push_back(opacity);
}
}

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

    RenderProbe probe;
    expect(clawhud::CommitHudRenderFrame(transparent.layout.backgroundOpacity,
        &Present, &probe, &Observe, &probe) == S_OK,
        "transparent frame reaches presentation boundary");
    expect(clawhud::CommitHudRenderFrame(opaque.layout.backgroundOpacity,
        &Present, &probe, &Observe, &probe) == S_OK,
        "opaque frame reaches presentation boundary");
    expect(probe.presentCount == 2 && probe.observedOpacity.size() == 2 &&
        probe.observedOpacity[0] == 0.0f && probe.observedOpacity[1] == 1.0f,
        "same initialized presentation boundary observes both runtime opacities");
    return ok ? 0 : 1;
}
