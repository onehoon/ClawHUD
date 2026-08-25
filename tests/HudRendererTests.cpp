#include "HudRenderer.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <cmath>
#include <initializer_list>
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

float MeasureWidth(HudRenderer& renderer, const std::vector<HudTextRun>& runs,
    const HudRenderOptions& options)
{
    HudMeasureResult result{};
    return SUCCEEDED(renderer.Measure(runs, options, result)) ? result.contentWidth : -1.0f;
}
}

int main()
{
    bool ok = true;
    ok &= Check(Near(DipFromPhysicalPixels(14.0f, 144.0f), 9.3333f), "physical pixels to DIP");

    HudRenderOptions options{};
    ok &= Check(Near(options.fontPixelSize, 20.0f) &&
        Near(options.unitFontPixelSize, 11.0f) &&
        Near(options.barPixelHeight, 30.0f), "HUD typography defaults");
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

    options.dpi = 144.0f;
    options.layout.alignment = HudAlignment::Left;
    const auto scaledMeasure = HudMeasureResult{
        DipFromPhysicalPixels(400.0f, options.dpi),
        DipFromPhysicalPixels(30.0f, options.dpi)};
    geometry = CalculateHudGeometry(viewport, scaledMeasure, options);
    ok &= Check(Near(geometry.background.right, 266.6667f), "144 DPI content width");
    ok &= Check(Near(geometry.textOrigin.x, 5.3333f), "144 DPI physical padding");

    const HudRenderOptions geometryOptions{};
    geometry = CalculateHudGeometry(viewport, HudMeasureResult{400.0f, 42.0f, 32.0f}, geometryOptions);
    ok &= Check(Near(geometry.textOrigin.y, 5.0f), "vertical centering uses measured text height");
    ok &= Check(Near(RightAlignedOffset(100.0f, 80.0f), 20.0f) &&
        Near(RightAlignedOffset(100.0f, 100.0f), 0.0f) &&
        Near(RightAlignedOffset(100.0f, 120.0f), 0.0f), "right aligned value offset");

    const auto checkUnits = [&](const wchar_t* text, std::initializer_list<HudUnitRange> expected,
        const char* message)
    {
        const auto actual = FindHudUnitRanges(text);
        const std::vector<HudUnitRange> wanted(expected);
        bool matches = actual.size() == wanted.size();
        for (std::size_t i = 0; matches && i < actual.size(); ++i)
            matches = actual[i].start == wanted[i].start && actual[i].length == wanted[i].length;
        ok &= Check(matches, message);
    };
    checkUnits(L"100 FPS", {{4, 3}}, "FPS unit range");
    checkUnits(L"36% 67\u00B0C", {{2, 1}, {6, 2}}, "percentage and temperature unit ranges");
    checkUnits(L"10.1 W", {{5, 1}}, "power unit range");
    checkUnits(L"1000 RPM", {{5, 3}}, "fan unit range");
    checkUnits(L"72% 2.5h", {{2, 1}, {7, 1}}, "battery hours unit ranges");
    checkUnits(L"72% 45m", {{2, 1}, {6, 1}}, "battery minutes unit ranges");

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

        const HudRenderOptions stableOptions{};
        const auto width = [&](HudSegmentKind kind, const wchar_t* value)
        {
            const wchar_t* label = kind == HudSegmentKind::Fan ? L"FAN" :
                kind == HudSegmentKind::Graphics ? L"DX11" : L"GPU";
            return MeasureWidth(renderer, {{kind, label, value}}, stableOptions);
        };
        ok &= Check(Near(width(HudSegmentKind::Graphics, L"99 FPS"),
            width(HudSegmentKind::Graphics, L"100 FPS")), "stable FPS slot");
        ok &= Check(Near(width(HudSegmentKind::Gpu, L"99%"),
            width(HudSegmentKind::Gpu, L"100%")), "stable percentage slot");
        ok &= Check(Near(width(HudSegmentKind::Tdp, L"9.8 W"),
            width(HudSegmentKind::Tdp, L"10.1 W")), "stable power slot");
        ok &= Check(Near(width(HudSegmentKind::Fan, L"999 RPM"),
            width(HudSegmentKind::Fan, L"1000 RPM")), "stable fan slot");
        ok &= Check(width(HudSegmentKind::Fan, L"10000 RPM") > width(HudSegmentKind::Fan, L"999 RPM"),
            "fan overflow expands");

        const std::vector<HudTextRun> gpu{{HudSegmentKind::Gpu, L"GPU", L"99%"}};
        const std::vector<HudTextRun> gpuAndCpu{
            {HudSegmentKind::Gpu, L"GPU", L"99%"}, {HudSegmentKind::Cpu, L"CPU", L"36%"}};
        ok &= Check(MeasureWidth(renderer, gpuAndCpu, stableOptions) >
            MeasureWidth(renderer, gpu, stableOptions), "missing run compacts");
    }
    return ok ? 0 : 1;
}
