#include "HudRenderer.h"
#include "HudWindowGeometry.h"
#include "HudPresentation.h"

#include <dwrite.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
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

bool ReadBackgroundAlpha(HudRenderer& renderer, HudRenderOptions options, BYTE& backgroundAlpha,
    BYTE& transparentAreaAlpha, bool& foregroundVisible)
{
    constexpr UINT width = 512;
    constexpr UINT height = 64;
    D3D_FEATURE_LEVEL featureLevel{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceContext;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    const HRESULT deviceHr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP,
        nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
        D3D11_SDK_VERSION, &device, &featureLevel, &deviceContext);
    if (FAILED(deviceHr)) { std::cerr << "D3D11CreateDevice failed: " << std::hex << deviceHr << '\n'; return false; }
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device.As(&dxgiDevice))) return false;
    ComPtr<ID2D1Factory1> d2dFactory;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1), reinterpret_cast<void**>(d2dFactory.ReleaseAndGetAddressOf())))) return false;
    ComPtr<ID2D1Device> d2dDevice;
    if (FAILED(d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice))) return false;
    ComPtr<ID2D1DeviceContext> d2dContext;
    if (FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext))) return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture))) return false;
    ComPtr<IDXGISurface> surface;
    if (FAILED(texture.As(&surface))) return false;
    ComPtr<ID2D1Bitmap1> bitmap;
    const auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    const HRESULT bitmapHr = d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &props, &bitmap);
    if (FAILED(bitmapHr)) { std::cerr << "CreateBitmapFromDxgiSurface failed: " << std::hex << bitmapHr << '\n'; return false; }
    d2dContext->SetTarget(bitmap.Get());
    d2dContext->BeginDraw();
    d2dContext->Clear(D2D1::ColorF(0, 0.0f));
    const std::vector<HudTextRun> runs{{HudSegmentKind::Gpu, L"GPU", L"99%"},
        {HudSegmentKind::Tdp, L"TDP", L"18W"}};
    const HRESULT drawHr = renderer.Draw(d2dContext.Get(), runs, options,
        D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)));
    if (FAILED(drawHr)) { std::cerr << "HudRenderer::Draw failed: " << std::hex << drawHr << '\n'; return false; }
    const HRESULT endHr = d2dContext->EndDraw();
    if (FAILED(endHr)) { std::cerr << "EndDraw failed: " << std::hex << endHr << '\n'; return false; }
    d2dContext->SetTarget(nullptr);
    d2dContext->Flush();

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0; stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging))) return false;
    deviceContext->CopyResource(staging.Get(), texture.Get());
    deviceContext->Flush();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(deviceContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const auto* bytes = static_cast<const BYTE*>(mapped.pData);
    backgroundAlpha = bytes[1 * mapped.RowPitch + (width / 2) * 4 + 3];
    transparentAreaAlpha = bytes[1 * mapped.RowPitch + 1 * 4 + 3];
    foregroundVisible = false;
    for (UINT y = 0; y < height && !foregroundVisible; ++y)
        for (UINT x = 0; x < width; ++x)
            if (bytes[y * mapped.RowPitch + x * 4 + 3] >= 200 &&
                (x > 5 || y > 5)) { foregroundVisible = true; break; }
    deviceContext->Unmap(staging.Get(), 0);
    return true;
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
    ok &= Check(CalculateHudContentWidthPixels(1000.0f, 96.0f, 1920) == 1000 &&
        CalculateHudContentWidthPixels(1000.0f, 144.0f, 1920) == 1500 &&
        CalculateHudContentWidthPixels(3000.0f, 96.0f, 1920) == 1920,
        "production ContentWidth converts measured DIP width to monitor pixels");
    ok &= Check(kHudBackgroundColor == 0x020202 && kHudCpuColor == 0x2E97CB &&
        kHudGpuColor == 0x2E9762 && kHudVramColor == 0xAD64C1 &&
        kHudGraphicsColor == 0xEB5B5B && kHudSystemColor == 0xFF9078 &&
        kHudSeparatorColor == 0xAD64C1, "MangoHud semantic colors");
    ok &= Check(Near(kHudTextOutlinePx, 1.5f) && Near(kHudSeparatorCorePx, 2.0f) &&
        Near(kHudSeparatorOuterPx, 3.0f), "MangoHud separator and outline widths");

    HudRenderOptions options{};
    ok &= Check(Near(options.segmentGapPx, 8.0f) && Near(options.metricGapPx, 6.0f) &&
        Near(options.separatorGapPx, 14.0f),
        "horizontal spacing defaults");
    ok &= Check(Near(options.fontPixelSize, 20.0f) &&
        Near(options.unitFontPixelSize, 11.0f) &&
        Near(options.barPixelHeight, 30.0f), "HUD typography defaults");
    options.layout.alignment = HudAlignment::Center;
    options.layout.backgroundMode = HudBackgroundMode::ContentWidth;
    const D2D1_RECT_F viewport = D2D1::RectF(0, 0, 1000, 300);
    const HudMeasureResult measure{400.0f, 30.0f};
    auto geometry = CalculateHudGeometry(viewport, measure, options);
    ok &= Check(Near(geometry.background.left, 300.0f) &&
        Near(geometry.background.right, 700.0f),
        "center ContentWidth background uses measured content");
    ok &= Check(Near(geometry.textOrigin.x, 305.0f), "center text origin");

    options.layout.alignment = HudAlignment::Left;
    geometry = CalculateHudGeometry(viewport, measure, options);
    ok &= Check(Near(geometry.background.left, 0.0f) &&
        Near(geometry.background.right, 400.0f) && Near(geometry.textOrigin.x, 5.0f),
        "left ContentWidth geometry");
    options.layout.alignment = HudAlignment::Right;
    geometry = CalculateHudGeometry(viewport, measure, options);
    ok &= Check(Near(geometry.background.left, 600.0f) &&
        Near(geometry.background.right, 1000.0f) && Near(geometry.textOrigin.x, 605.0f),
        "right ContentWidth geometry");

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
    ok &= Check(Near(geometry.textOrigin.x, 3.3333f), "144 DPI physical padding");

    const HudRenderOptions geometryOptions{};
    geometry = CalculateHudGeometry(viewport, HudMeasureResult{400.0f, 42.0f, 32.0f}, geometryOptions);
    ok &= Check(Near(geometry.textOrigin.y, 5.0f), "vertical centering uses measured text height");

    HudRenderOptions typographyOptions{};
    ok &= Check(Near(MainTextYOffset(typographyOptions), 0.0f) &&
        Near(UnitTextYOffset(typographyOptions), 2.0f),
        "Unispace text offsets");
    typographyOptions.font = HudFont::SegoeUiVariable;
    ok &= Check(Near(MainTextYOffset(typographyOptions), -2.0f) &&
        Near(UnitTextYOffset(typographyOptions), 2.0f),
        "Segoe UI Variable text offsets");
    typographyOptions.dpi = 144.0f;
    ok &= Check(Near(MainTextYOffset(typographyOptions), -1.3333f) &&
        Near(UnitTextYOffset(typographyOptions), 1.3333f),
        "text offsets preserve physical pixels at high DPI");

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
    checkUnits(L"100FPS", {{3, 3}}, "FPS unit range");
    checkUnits(L"36% 67\u00B0C", {{2, 1}, {6, 2}}, "percentage and temperature unit ranges");
    checkUnits(L"10.1W", {{4, 1}}, "power unit range");
    checkUnits(L"87% VRAM 3.4GB", {{2, 1}, {12, 2}},
        "percentage and VRAM unit ranges");
    checkUnits(L"1000RPM", {{4, 3}}, "fan unit range");
    checkUnits(L"10Windows", {}, "power substring without trailing boundary");
    checkUnits(L"3GBps", {}, "VRAM substring without trailing boundary");
    checkUnits(L"60FPSCounter", {}, "FPS substring without trailing boundary");
    checkUnits(L"1000RPMValue", {}, "fan substring without trailing boundary");
    checkUnits(L"72% 2.5h", {{2, 1}, {7, 1}}, "battery hours unit ranges");
    checkUnits(L"72% 45m", {{2, 1}, {6, 1}}, "battery minutes unit ranges");

    ComPtr<IDWriteFactory> factory;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), &factory);
    ok &= Check(SUCCEEDED(hr), "create DirectWrite factory");
    if (SUCCEEDED(hr))
    {
        HudRenderer renderer(factory.Get());
        HudRenderer privateRenderer(factory.Get(), CLAWHUD_TEST_UNISPACE_PATH);
        ok &= Check(privateRenderer.PrivateFontLoaded(),
            "bundled Unispace private font loads and is present in collection");
        HudRenderer missingRenderer(factory.Get(), L"C:\\does-not-exist\\Unispace.otf");
        ok &= Check(!missingRenderer.PrivateFontLoaded(), "missing font uses fallback without crashing");
        HudMeasureResult measured{};
        const auto runs = FormatHud(MakeGameDcSample());
        hr = renderer.Measure(runs, options, measured);
        ok &= Check(SUCCEEDED(hr) && measured.contentWidth > 0 && measured.contentHeight > 0,
            "measure sample runs");
        HudRenderOptions unispaceOptions = options;
        unispaceOptions.font = HudFont::Unispace;
        ok &= Check(SUCCEEDED(privateRenderer.Measure(runs, unispaceOptions, measured)) &&
            measured.contentWidth > 0, "measure with private Unispace collection");
        HudRenderOptions segoeOptions = options;
        segoeOptions.font = HudFont::SegoeUiVariable;
        ok &= Check(SUCCEEDED(renderer.Measure(runs, segoeOptions, measured)) &&
            measured.contentWidth > 0, "measure with Segoe UI Variable system font");
        ok &= Check(renderer.Measure({}, options, measured) == S_OK && measured.contentWidth > 0,
            "measure empty runs");

        const HudRenderOptions stableOptions{};
        const auto width = [&](HudSegmentKind kind, const wchar_t* label, const wchar_t* value)
        {
            return MeasureWidth(renderer, {{kind, label, value}}, stableOptions);
        };
        const auto sameWidth = [&](HudSegmentKind kind, const wchar_t* label,
            std::initializer_list<const wchar_t*> values, const char* message)
        {
            const auto first = width(kind, label, *values.begin());
            for (const auto* value : values)
                ok &= Check(Near(width(kind, label, value), first), message);
        };
        sameWidth(HudSegmentKind::Graphics, L"FPS",
            {L"9FPS", L"99FPS", L"999FPS"}, "stable Graphics slot");
        sameWidth(HudSegmentKind::Cpu, L"CPU",
            {L"1% 40\u00B0C", L"36% 67\u00B0C", L"100% 100\u00B0C"},
            "stable CPU slot");
        sameWidth(HudSegmentKind::Cpu, L"CPU",
            {L"40\u00B0C", L"67\u00B0C", L"100\u00B0C"},
            "stable temperature-only CPU slot");
        sameWidth(HudSegmentKind::Gpu, L"GPU",
            {L"1%", L"47%", L"100%"},
            "stable GPU slot");
        sameWidth(HudSegmentKind::Vram, L"VRAM",
            {L"0.1GB", L"3.4GB", L"99.9GB"}, "stable VRAM slot");
        sameWidth(HudSegmentKind::Ram, L"RAM",
            {L"0.1GB", L"15.2GB", L"31.9GB", L"99.9GB"}, "stable RAM slot");
        sameWidth(HudSegmentKind::Tdp, L"TDP",
            {L"5W", L"18W", L"35W"}, "stable TDP slot uses 35W exemplar");
        sameWidth(HudSegmentKind::SystemPower, L"SYS",
            {L"8W", L"24.5W", L"99.9W"}, "stable SystemPower slot");
        sameWidth(HudSegmentKind::Fan, L"FAN",
            {L"800RPM", L"4500RPM", L"9999RPM"}, "stable Fan slot");
        sameWidth(HudSegmentKind::Battery, L"BAT",
            {L"9%", L"72%", L"100%"}, "stable Battery percent slot");
        ok &= Check(width(HudSegmentKind::Graphics, L"Vulkan", L"60FPS") >
            width(HudSegmentKind::Graphics, L"DX12", L"60FPS"),
            "Graphics API labels use actual width");
        ok &= Check(Near(width(HudSegmentKind::Graphics, L"DX12", L"9FPS"),
            width(HudSegmentKind::Graphics, L"DX12", L"999FPS")),
            "Graphics FPS slot stays fixed");
        ok &= Check(width(HudSegmentKind::Battery, L"BAT", L"80% 2.5h") >
            width(HudSegmentKind::Battery, L"BAT", L"80%"),
            "battery time slot is added only when present");
        const float singleGpuWidth = width(HudSegmentKind::Gpu, L"GPU", L"47%");
        const float singleCpuWidth = width(HudSegmentKind::Cpu, L"CPU", L"36%");
        const float expectedPairWidth = singleGpuWidth + singleCpuWidth -
            2.0f * stableOptions.horizontalPaddingPx +
            2.0f * stableOptions.separatorGapPx +
            DipFromPhysicalPixels(kHudSeparatorOuterPx, stableOptions.dpi);
        ok &= Check(Near(MeasureWidth(renderer, {
                {HudSegmentKind::Gpu, L"GPU", L"47%"},
                {HudSegmentKind::Cpu, L"CPU", L"36%"}}, stableOptions), expectedPairWidth),
            "separator uses two physical 14px gaps");
        float reservedWidth{};
        ok &= Check(SUCCEEDED(renderer.MeasureReservedHudWidth(stableOptions, reservedWidth)) &&
            reservedWidth > 0.0f, "measure reserved HUD envelope");
        HudMeasureResult withVram{};
        HudMeasureResult withoutVram{};
        renderer.Measure({{HudSegmentKind::Gpu, L"GPU", L"74%"},
            {HudSegmentKind::Vram, L"VRAM", L"3.4GB"},
            {HudSegmentKind::Tdp, L"TDP", L"18W"}}, stableOptions, withVram);
        renderer.Measure({{HudSegmentKind::Gpu, L"GPU", L"74%"},
            {HudSegmentKind::Tdp, L"TDP", L"18W"}}, stableOptions, withoutVram);
        ok &= Check(withVram.contentWidth > withoutVram.contentWidth,
            "visible ContentWidth omits missing VRAM slot");
        ok &= Check(reservedWidth > withVram.contentWidth,
            "reserved envelope remains maximum and includes VRAM");
        HudRenderOptions alternateOptions = stableOptions;
        alternateOptions.layout.alignment = HudAlignment::Right;
        alternateOptions.layout.backgroundMode = HudBackgroundMode::ContentWidth;
        float alternateReservedWidth{};
        ok &= Check(SUCCEEDED(renderer.MeasureReservedHudWidth(alternateOptions,
            alternateReservedWidth)) && Near(reservedWidth, alternateReservedWidth),
            "reserved HUD envelope is layout-stable");
        ok &= Check(width(HudSegmentKind::Fan, L"FAN", L"10000RPM") >
            width(HudSegmentKind::Fan, L"FAN", L"999RPM"),
            "fan overflow expands");
        const auto segoeWidth = [&](HudSegmentKind kind, const wchar_t* label,
            const wchar_t* value)
        {
            return MeasureWidth(renderer, {{kind, label, value}}, segoeOptions);
        };
        ok &= Check(Near(segoeWidth(HudSegmentKind::Gpu, L"GPU", L"9%"),
                segoeWidth(HudSegmentKind::Gpu, L"GPU", L"99%")) &&
            Near(segoeWidth(HudSegmentKind::Tdp, L"TDP", L"7W"),
                segoeWidth(HudSegmentKind::Tdp, L"TDP", L"35W")) &&
            Near(segoeWidth(HudSegmentKind::Fan, L"FAN", L"999RPM"),
                segoeWidth(HudSegmentKind::Fan, L"FAN", L"9999RPM")),
            "Segoe UI Variable segment widths stay reserved");

        const std::vector<HudTextRun> gpu{{HudSegmentKind::Gpu, L"GPU", L"99%"}};
        const std::vector<HudTextRun> gpuAndCpu{
            {HudSegmentKind::Gpu, L"GPU", L"99%"}, {HudSegmentKind::Cpu, L"CPU", L"36%"}};
        ok &= Check(MeasureWidth(renderer, gpuAndCpu, stableOptions) >
            MeasureWidth(renderer, gpu, stableOptions), "missing run compacts");
        const auto cpuAndGpu = [&](const wchar_t* cpuValue)
        {
            return MeasureWidth(renderer,
                {{HudSegmentKind::Cpu, L"CPU", cpuValue},
                 {HudSegmentKind::Gpu, L"GPU", L"47%"}}, stableOptions);
        };
        ok &= Check(Near(cpuAndGpu(L"1% 40\u00B0C"), cpuAndGpu(L"100% 100\u00B0C")),
            "next segment position is stable when CPU value changes");

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
                {HudSegmentKind::Tdp, L"TDP", L"5W"},
                {HudSegmentKind::Fan, L"FAN", L"4507RPM"},
                {HudSegmentKind::Battery, L"BAT", L"78%"}},
                stableOptions, measured);
            return measured;
        }();
        ok &= Check(gpuOnlyMeasure.contentWidth > 0.0f &&
            expectedContentMeasure.contentWidth > gpuOnlyMeasure.contentWidth,
            "ContentWidth measures only current runs");

        HudRenderOptions pixelOptions{};
        pixelOptions.layout.backgroundMode = HudBackgroundMode::ContentWidth;
        pixelOptions.layout.alignment = HudAlignment::Center;
        const auto checkAlpha = [&](float opacity, BYTE expected, const char* message)
        {
            pixelOptions.layout.backgroundOpacity = opacity;
            BYTE actual{};
            BYTE transparent{};
            bool foregroundVisible{};
            const bool rendered = ReadBackgroundAlpha(privateRenderer, pixelOptions,
                actual, transparent, foregroundVisible);
            ok &= Check(rendered && std::abs(static_cast<int>(actual) - static_cast<int>(expected)) <= 2,
                message);
            ok &= Check(rendered && transparent == 0,
                "transparent cleared area stays transparent");
            return foregroundVisible;
        };
        checkAlpha(0.0f, 0, "background alpha at 0 percent");
        checkAlpha(0.25f, 64, "background alpha at 25 percent");
        checkAlpha(0.50f, 128, "background alpha at 50 percent");
        checkAlpha(0.75f, 191, "background alpha at 75 percent");
        checkAlpha(1.0f, 255, "background alpha at 100 percent");
        pixelOptions.layout.backgroundOpacity = 0.0f;
        BYTE transparentAlpha{};
        BYTE clearedAreaAlpha{};
        bool foregroundVisible{};
        ok &= Check(ReadBackgroundAlpha(privateRenderer, pixelOptions,
            transparentAlpha, clearedAreaAlpha, foregroundVisible) &&
            transparentAlpha == 0 && clearedAreaAlpha == 0 && foregroundVisible,
            "foreground remains visible when background is transparent");
    }
    return ok ? 0 : 1;
}
