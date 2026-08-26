#include "HudRenderer.h"
#include "HudWindowGeometry.h"

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
    ok &= Check(Near(geometry.background.left, 0.0f) && Near(geometry.background.right, 1000.0f),
        "content background fills viewport");
    ok &= Check(Near(geometry.textOrigin.x, 308.0f), "center text origin");

    options.layout.alignment = HudAlignment::Left;
    geometry = CalculateHudGeometry(viewport, measure, options);
    ok &= Check(Near(geometry.background.left, 0.0f) &&
        Near(geometry.background.right, 1000.0f) && Near(geometry.textOrigin.x, 8.0f),
        "left content geometry");
    options.layout.alignment = HudAlignment::Right;
    geometry = CalculateHudGeometry(viewport, measure, options);
    ok &= Check(Near(geometry.background.left, 0.0f) &&
        Near(geometry.background.right, 1000.0f) && Near(geometry.textOrigin.x, 608.0f),
        "right content geometry");

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
    ok &= Check(Near(geometry.background.right, 1000.0f), "144 DPI content viewport");
    ok &= Check(Near(geometry.textOrigin.x, 5.3333f), "144 DPI physical padding");

    const HudRenderOptions geometryOptions{};
    geometry = CalculateHudGeometry(viewport, HudMeasureResult{400.0f, 42.0f, 32.0f}, geometryOptions);
    ok &= Check(Near(geometry.textOrigin.y, 5.0f), "vertical centering uses measured text height");
    ok &= Check(Near(RightAlignedOffset(100.0f, 80.0f), 20.0f) &&
        Near(RightAlignedOffset(100.0f, 100.0f), 0.0f) &&
        Near(RightAlignedOffset(100.0f, 120.0f), 0.0f), "right aligned value offset");

    const RECT monitor{ -1920, -100, 0, 980 };
    const auto fullWindow = CalculateHudWindowGeometry(
        monitor, HudBackgroundMode::FullWidth, HudAlignment::Center, 500);
    ok &= Check(fullWindow.xPx == -1920 && fullWindow.yPx == -100 &&
        fullWindow.widthPx == 1920, "full width window geometry");
    const auto leftWindow = CalculateHudWindowGeometry(
        monitor, HudBackgroundMode::ContentWidth, HudAlignment::Left, 500);
    const auto centerWindow = CalculateHudWindowGeometry(
        monitor, HudBackgroundMode::ContentWidth, HudAlignment::Center, 500);
    const auto rightWindow = CalculateHudWindowGeometry(
        monitor, HudBackgroundMode::ContentWidth, HudAlignment::Right, 500);
    ok &= Check(leftWindow.xPx == -1920 && leftWindow.widthPx == 500,
        "content left window geometry");
    ok &= Check(centerWindow.xPx == -1210 && centerWindow.widthPx == 500,
        "content center window geometry");
    ok &= Check(rightWindow.xPx == -500 && rightWindow.widthPx == 500,
        "content right window geometry");
    const auto clampedWindow = CalculateHudWindowGeometry(
        monitor, HudBackgroundMode::ContentWidth, HudAlignment::Right, 3000);
    ok &= Check(clampedWindow.xPx == -1920 && clampedWindow.widthPx == 1920,
        "content window width clamps to monitor");

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
    checkUnits(L"87% VRAM 3.4 GB", {{2, 1}, {13, 2}},
        "percentage and VRAM unit ranges");
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
        ok &= Check(width(HudSegmentKind::Gpu, L"0%") <
            width(HudSegmentKind::Gpu, L"47% VRAM 3.4 GB"),
            "GPU without VRAM does not reserve VRAM width");
        ok &= Check(Near(width(HudSegmentKind::Tdp, L"9.8 W"),
            width(HudSegmentKind::Tdp, L"10.1 W")), "stable power slot");
        ok &= Check(Near(width(HudSegmentKind::Fan, L"999 RPM"),
            width(HudSegmentKind::Fan, L"1000 RPM")), "stable fan slot");
        ok &= Check(Near(width(HudSegmentKind::Gpu, L"99% VRAM 3.4 GB"),
            width(HudSegmentKind::Gpu, L"100% VRAM 99.9 GB")),
            "stable GPU VRAM slot");
        float reservedWidth{};
        ok &= Check(SUCCEEDED(renderer.MeasureReservedHudWidth(stableOptions, reservedWidth)) &&
            reservedWidth > 0.0f, "measure reserved HUD envelope");
        HudRenderOptions alternateOptions = stableOptions;
        alternateOptions.layout.alignment = HudAlignment::Right;
        alternateOptions.layout.backgroundMode = HudBackgroundMode::ContentWidth;
        float alternateReservedWidth{};
        ok &= Check(SUCCEEDED(renderer.MeasureReservedHudWidth(alternateOptions,
            alternateReservedWidth)) && Near(reservedWidth, alternateReservedWidth),
            "reserved HUD envelope is layout-stable");
        ok &= Check(width(HudSegmentKind::Fan, L"10000 RPM") > width(HudSegmentKind::Fan, L"999 RPM"),
            "fan overflow expands");

        const std::vector<HudTextRun> gpu{{HudSegmentKind::Gpu, L"GPU", L"99%"}};
        const std::vector<HudTextRun> gpuAndCpu{
            {HudSegmentKind::Gpu, L"GPU", L"99%"}, {HudSegmentKind::Cpu, L"CPU", L"36%"}};
        ok &= Check(MeasureWidth(renderer, gpuAndCpu, stableOptions) >
            MeasureWidth(renderer, gpu, stableOptions), "missing run compacts");

        const auto gpuOnlyMeasure = [&]()
        {
            HudMeasureResult measured{};
            renderer.Measure({{HudSegmentKind::Gpu, L"GPU", L"0%"}},
                stableOptions, measured);
            return measured;
        }();
        const auto expectedContentMeasure = [&]()
        {
            HudMeasureResult measured{};
            renderer.Measure({{HudSegmentKind::Cpu, L"CPU", L"16% 43°C"},
                {HudSegmentKind::Gpu, L"GPU", L"47%"},
                {HudSegmentKind::Tdp, L"TDP", L"5 W"},
                {HudSegmentKind::Fan, L"FAN", L"4507 RPM"},
                {HudSegmentKind::Battery, L"BAT", L"78%"}},
                stableOptions, measured);
            return measured;
        }();
        ok &= Check(gpuOnlyMeasure.contentWidth > 0.0f &&
            expectedContentMeasure.contentWidth > gpuOnlyMeasure.contentWidth,
            "ContentWidth measures only current runs");
    }
    return ok ? 0 : 1;
}
