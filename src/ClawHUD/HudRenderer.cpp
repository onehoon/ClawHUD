#include "HudRenderer.h"

#include <algorithm>

#include <wrl/client.h>

namespace clawhud
{
namespace
{
using Microsoft::WRL::ComPtr;

constexpr float kMaxLayoutDimension = 100000.0f;
constexpr wchar_t kFontName[] = L"Segoe UI Variable";

float Padding(const HudRenderOptions& options) noexcept
{
    return DipFromPhysicalPixels(options.horizontalPaddingPx, options.dpi);
}

float SegmentGap(const HudRenderOptions& options) noexcept
{
    return DipFromPhysicalPixels(options.segmentGapPx, options.dpi);
}

float SeparatorGap(const HudRenderOptions& options) noexcept
{
    return DipFromPhysicalPixels(options.separatorGapPx, options.dpi);
}

HRESULT CreateTextFormat(IDWriteFactory* factory, const HudRenderOptions& options,
    ComPtr<IDWriteTextFormat>& format)
{
    if (!factory)
        return E_INVALIDARG;

    const float size = DipFromPhysicalPixels(options.fontPixelSize, options.dpi);
    HRESULT hr = factory->CreateTextFormat(kFontName, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, size, L"", &format);
    if (FAILED(hr))
        return hr;
    hr = format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (FAILED(hr))
        return hr;
    hr = format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (FAILED(hr))
        return hr;
    return format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

HRESULT CreateLayout(IDWriteFactory* factory, IDWriteTextFormat* format,
    const std::wstring& text, bool tabular, ComPtr<IDWriteTextLayout>& layout)
{
    if (!factory || !format)
        return E_INVALIDARG;

    HRESULT hr = factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
        format, kMaxLayoutDimension, kMaxLayoutDimension, &layout);
    if (FAILED(hr) || !tabular || text.empty())
        return hr;

    ComPtr<IDWriteTypography> typography;
    hr = factory->CreateTypography(&typography);
    if (FAILED(hr))
        return hr;
    const DWRITE_FONT_FEATURE feature{DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1};
    hr = typography->AddFontFeature(feature);
    if (FAILED(hr))
        return hr;
    const DWRITE_TEXT_RANGE range{0, static_cast<UINT32>(text.size())};
    return layout->SetTypography(typography.Get(), range);
}

float Width(IDWriteTextLayout* layout) noexcept
{
    DWRITE_TEXT_METRICS metrics{};
    return SUCCEEDED(layout->GetMetrics(&metrics)) ? metrics.widthIncludingTrailingWhitespace : 0.0f;
}

HRESULT RunWidth(IDWriteFactory* factory, IDWriteTextFormat* format,
    const HudTextRun& run, const HudRenderOptions& options, float& width)
{
    ComPtr<IDWriteTextLayout> label;
    ComPtr<IDWriteTextLayout> value;
    HRESULT hr = CreateLayout(factory, format, run.label, false, label);
    if (FAILED(hr))
        return hr;
    hr = CreateLayout(factory, format, run.value, true, value);
    if (FAILED(hr))
        return hr;
    width = Width(label.Get()) + SegmentGap(options) + Width(value.Get());
    return S_OK;
}

D2D1_COLOR_F LabelColor(HudSegmentKind kind) noexcept
{
    switch (kind)
    {
    case HudSegmentKind::Cpu: return D2D1::ColorF(0x2E97CB, 1.0f);
    case HudSegmentKind::Gpu: return D2D1::ColorF(0x2E9762, 1.0f);
    case HudSegmentKind::Battery: return D2D1::ColorF(0xFF9078, 1.0f);
    default: return D2D1::ColorF(D2D1::ColorF::White, 1.0f);
    }
}

struct TextAntialiasModeGuard
{
    ID2D1DeviceContext* context;
    D2D1_TEXT_ANTIALIAS_MODE previous;

    explicit TextAntialiasModeGuard(ID2D1DeviceContext* value)
        : context(value), previous(value->GetTextAntialiasMode())
    {
        context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    }

    ~TextAntialiasModeGuard()
    {
        context->SetTextAntialiasMode(previous);
    }
};
}

float DipFromPhysicalPixels(float pixels, float dpi) noexcept
{
    return dpi > 0.0f ? pixels * 96.0f / dpi : pixels;
}

HudRenderGeometry CalculateHudGeometry(const D2D1_RECT_F& viewport,
    const HudMeasureResult& measure, const HudRenderOptions& options) noexcept
{
    const float viewportWidth = std::max(0.0f, viewport.right - viewport.left);
    const float contentWidth = std::max(0.0f, measure.contentWidth);
    const float padding = Padding(options);
    float contentX = viewport.left;
    switch (options.layout.alignment)
    {
    case HudAlignment::Center:
        contentX = viewport.left + (viewportWidth - contentWidth) / 2.0f;
        break;
    case HudAlignment::Right:
        contentX = viewport.right - contentWidth;
        break;
    case HudAlignment::Left:
        break;
    }
    if (contentWidth > viewportWidth)
        contentX = viewport.left;
    else
        contentX = std::clamp(contentX, viewport.left, viewport.right - contentWidth);

    const float barHeight = std::max(0.0f, measure.contentHeight);
    const float textY = viewport.top + std::max(0.0f, (barHeight -
        DipFromPhysicalPixels(options.fontPixelSize, options.dpi)) / 2.0f);
    HudRenderGeometry geometry{
        D2D1::RectF(viewport.left, viewport.top, viewport.right,
            std::min(viewport.bottom, viewport.top + barHeight)),
        D2D1::Point2F(contentX + padding, textY)};
    if (options.layout.backgroundMode == HudBackgroundMode::ContentWidth)
        geometry.background = D2D1::RectF(contentX, viewport.top,
            contentX + contentWidth, std::min(viewport.bottom, viewport.top + barHeight));
    return geometry;
}

HRESULT HudRenderer::Measure(const std::vector<HudTextRun>& runs,
    const HudRenderOptions& options, HudMeasureResult& result) const
{
    result = {};
    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = CreateTextFormat(factory_, options, format);
    if (FAILED(hr))
        return hr;

    result.contentWidth = Padding(options) * 2.0f;
    result.contentHeight = DipFromPhysicalPixels(options.barPixelHeight, options.dpi);
    ComPtr<IDWriteTextLayout> separator;
    hr = CreateLayout(factory_, format.Get(), L"|", false, separator);
    if (FAILED(hr))
        return hr;
    for (size_t i = 0; i < runs.size(); ++i)
    {
        float width{};
        hr = RunWidth(factory_, format.Get(), runs[i], options, width);
        if (FAILED(hr))
            return hr;
        result.contentWidth += width;
        if (i + 1 < runs.size())
            result.contentWidth += SeparatorGap(options) * 2.0f + Width(separator.Get());
    }
    return S_OK;
}

HRESULT HudRenderer::Draw(ID2D1DeviceContext* context,
    const std::vector<HudTextRun>& runs, const HudRenderOptions& options,
    const D2D1_RECT_F& viewport) const
{
    if (!context || !factory_)
        return E_INVALIDARG;

    HudMeasureResult measure{};
    HRESULT hr = Measure(runs, options, measure);
    if (FAILED(hr))
        return hr;
    const HudRenderGeometry geometry = CalculateHudGeometry(viewport, measure, options);

    ComPtr<ID2D1SolidColorBrush> background;
    ComPtr<ID2D1SolidColorBrush> white;
    ComPtr<ID2D1SolidColorBrush> separatorBrush;
    hr = context->CreateSolidColorBrush(D2D1::ColorF(0x020202, 1.0f), &background);
    if (FAILED(hr)) return hr;
    hr = context->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &white);
    if (FAILED(hr)) return hr;
    hr = context->CreateSolidColorBrush(D2D1::ColorF(0xD0D0D0, 1.0f), &separatorBrush);
    if (FAILED(hr)) return hr;
    background->SetOpacity(std::clamp(options.layout.backgroundOpacity, 0.0f, 1.0f));

    ComPtr<IDWriteTextFormat> format;
    hr = CreateTextFormat(factory_, options, format);
    if (FAILED(hr)) return hr;
    ComPtr<IDWriteTextLayout> separator;
    hr = CreateLayout(factory_, format.Get(), L"|", false, separator);
    if (FAILED(hr)) return hr;

    TextAntialiasModeGuard antialiasMode(context);
    context->FillRectangle(geometry.background, background.Get());
    float x = geometry.textOrigin.x;
    for (size_t i = 0; i < runs.size(); ++i)
    {
        ComPtr<IDWriteTextLayout> label;
        ComPtr<IDWriteTextLayout> value;
        hr = CreateLayout(factory_, format.Get(), runs[i].label, false, label);
        if (FAILED(hr)) return hr;
        hr = CreateLayout(factory_, format.Get(), runs[i].value, true, value);
        if (FAILED(hr)) return hr;

        ComPtr<ID2D1SolidColorBrush> labelBrush;
        hr = context->CreateSolidColorBrush(LabelColor(runs[i].kind), &labelBrush);
        if (FAILED(hr)) return hr;
        context->DrawTextLayout(D2D1::Point2F(x, geometry.textOrigin.y), label.Get(), labelBrush.Get());
        x += Width(label.Get()) + SegmentGap(options);
        context->DrawTextLayout(D2D1::Point2F(x, geometry.textOrigin.y), value.Get(), white.Get());
        x += Width(value.Get());
        if (i + 1 < runs.size())
        {
            x += SeparatorGap(options);
            context->DrawTextLayout(D2D1::Point2F(x, geometry.textOrigin.y), separator.Get(), separatorBrush.Get());
            x += Width(separator.Get()) + SeparatorGap(options);
        }
    }
    return S_OK;
}
}
