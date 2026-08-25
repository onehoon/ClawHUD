#include "HudRenderer.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cmath>
#include <iostream>

using namespace clawhud;
using Microsoft::WRL::ComPtr;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

bool Near(float actual, float expected)
{
    return std::abs(actual - expected) < 0.01f;
}
}

int main()
{
    bool ok = true;
    ok &= Check(Near(DipFromPhysicalPixels(14.0f, 144.0f), 9.3333f), "physical pixels to DIP");

    HudRenderOptions options{};
    options.layout.alignment = HudAlignment::Center;
    options.layout.backgroundMode = HudBackgroundMode::ContentWidth;
    const D2D1_RECT_F viewport = D2D1::RectF(0, 0, 1000, 300);
    const HudMeasureResult measure{400.0f, 30.0f};
    auto geometry = CalculateHudGeometry(viewport, measure, options);
    ok &= Check(Near(geometry.background.left, 300.0f) && Near(geometry.background.right, 700.0f),
        "center content background");
    ok &= Check(Near(geometry.textOrigin.x, 308.0f), "center text origin");

    options.layout.alignment = HudAlignment::Left;
    geometry = CalculateHudGeometry(viewport, measure, options);
    ok &= Check(Near(geometry.background.left, 0.0f) && Near(geometry.textOrigin.x, 8.0f),
        "left content background");
    options.layout.alignment = HudAlignment::Right;
    geometry = CalculateHudGeometry(viewport, measure, options);
    ok &= Check(Near(geometry.background.left, 600.0f) && Near(geometry.textOrigin.x, 608.0f),
        "right content background");

    options.layout.backgroundMode = HudBackgroundMode::FullWidth;
    geometry = CalculateHudGeometry(viewport, measure, options);
    ok &= Check(Near(geometry.background.left, 0.0f) && Near(geometry.background.right, 1000.0f),
        "full width background");

    options.layout.backgroundMode = HudBackgroundMode::ContentWidth;
    geometry = CalculateHudGeometry(viewport, HudMeasureResult{1200.0f, 30.0f}, options);
    ok &= Check(Near(geometry.background.left, 0.0f), "wide content clamps to viewport");

    ComPtr<IDWriteFactory> factory;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), &factory);
    ok &= Check(SUCCEEDED(hr), "create DirectWrite factory");
    if (SUCCEEDED(hr))
    {
        HudRenderer renderer(factory.Get());
        HudMeasureResult measured{};
        const auto runs = FormatHud(MakeGameDcSample());
        hr = renderer.Measure(runs, options, measured);
        ok &= Check(SUCCEEDED(hr) && measured.contentWidth > 0 && measured.contentHeight > 0,
            "measure sample runs");
        ok &= Check(renderer.Measure({}, options, measured) == S_OK && measured.contentWidth > 0,
            "measure empty runs");
    }
    return ok ? 0 : 1;
}
