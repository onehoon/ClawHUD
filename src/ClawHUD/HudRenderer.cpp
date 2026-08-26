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
    ComPtr<IDWriteTextFormat>& format, bool unit = false)
{
    if (!factory)
        return E_INVALIDARG;

    const float size = DipFromPhysicalPixels(
        unit ? options.unitFontPixelSize : options.fontPixelSize, options.dpi);
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

bool IsWordBoundary(const std::wstring& text, std::size_t start, std::size_t length) noexcept
{
    const auto isWord = [](wchar_t value) { return value >= L'A' && value <= L'Z' ||
        value >= L'a' && value <= L'z' || value >= L'0' && value <= L'9' || value == L'_'; };
    return (start == 0 || !isWord(text[start - 1])) &&
        (start + length >= text.size() || !isWord(text[start + length]));
}

std::vector<HudUnitRange> FindHudUnitRangesImpl(const std::wstring& text)
{
    std::vector<HudUnitRange> ranges;
    const auto addToken = [&](const wchar_t* token, bool wordBoundary)
    {
        const std::size_t length = wcslen(token);
        for (std::size_t start = text.find(token); start != std::wstring::npos;
            start = text.find(token, start + length))
        {
            if (!wordBoundary || IsWordBoundary(text, start, length))
                ranges.push_back({static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(length)});
        }
    };
    addToken(L"FPS", true);
    addToken(L"RPM", true);
    addToken(L"%", false);
    addToken(L"\u00B0C", false);
    addToken(L"W", true);
    addToken(L"GB", true);
    for (std::size_t i = 1; i < text.size(); ++i)
    {
        if ((text[i] == L'h' || text[i] == L'm') &&
            (text[i - 1] >= L'0' && text[i - 1] <= L'9' || text[i - 1] == L'.') &&
            (i + 1 == text.size() || !((text[i + 1] >= L'a' && text[i + 1] <= L'z') ||
                (text[i + 1] >= L'A' && text[i + 1] <= L'Z'))))
            ranges.push_back({static_cast<std::uint32_t>(i), 1});
    }
    std::sort(ranges.begin(), ranges.end(), [](const auto& left, const auto& right)
        { return left.start < right.start; });
    return ranges;
}

HRESULT ApplyUnitTypography(IDWriteTextLayout* layout, const std::wstring& text,
    const HudRenderOptions& options)
{
    if (!layout || text.empty()) return S_OK;
    const auto ranges = FindHudUnitRangesImpl(text);
    const float size = DipFromPhysicalPixels(options.unitFontPixelSize, options.dpi);
    for (const auto& unit : ranges)
    {
        const DWRITE_TEXT_RANGE range{unit.start, unit.length};
        HRESULT hr = layout->SetFontSize(size, range);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

HRESULT CreateLayout(IDWriteFactory* factory, IDWriteTextFormat* format,
    const std::wstring& text, const HudRenderOptions& options, bool tabular,
    bool styleUnits, ComPtr<IDWriteTextLayout>& layout)
{
    if (!factory || !format)
        return E_INVALIDARG;

    HRESULT hr = factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
        format, kMaxLayoutDimension, kMaxLayoutDimension, &layout);
    if (FAILED(hr) || text.empty())
        return hr;

    if (tabular)
    {
        ComPtr<IDWriteTypography> typography;
        hr = factory->CreateTypography(&typography);
        if (FAILED(hr)) return hr;
        const DWRITE_FONT_FEATURE feature{DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1};
        hr = typography->AddFontFeature(feature);
        if (FAILED(hr)) return hr;
        const DWRITE_TEXT_RANGE range{0, static_cast<UINT32>(text.size())};
        hr = layout->SetTypography(typography.Get(), range);
        if (FAILED(hr)) return hr;
    }
    return styleUnits ? ApplyUnitTypography(layout.Get(), text, options) : S_OK;
}

float Width(IDWriteTextLayout* layout) noexcept
{
    DWRITE_TEXT_METRICS metrics{};
    return SUCCEEDED(layout->GetMetrics(&metrics)) ? metrics.widthIncludingTrailingWhitespace : 0.0f;
}

float Height(IDWriteTextLayout* layout) noexcept
{
    DWRITE_TEXT_METRICS metrics{};
    return SUCCEEDED(layout->GetMetrics(&metrics)) ? metrics.height : 0.0f;
}

const wchar_t* ValueExemplar(HudSegmentKind kind) noexcept
{
    switch (kind)
    {
    case HudSegmentKind::Graphics: return L"999 FPS";
    case HudSegmentKind::Cpu: return L"100% 100\u00B0C";
    case HudSegmentKind::Gpu: return L"100% VRAM 99.9 GB";
    case HudSegmentKind::Tdp:
    case HudSegmentKind::SystemPower: return L"99.9 W";
    case HudSegmentKind::Fan: return L"9999 RPM";
    case HudSegmentKind::Battery: return L"100% 9.9h";
    }
    return L"";
}

HRESULT ReservedValueWidth(IDWriteFactory* factory, IDWriteTextFormat* format,
    const HudRenderOptions& options, HudSegmentKind kind, float& width)
{
    ComPtr<IDWriteTextLayout> exemplar;
    HRESULT hr = CreateLayout(factory, format, ValueExemplar(kind), options, true, true, exemplar);
    if (FAILED(hr))
        return hr;
    width = Width(exemplar.Get());
    return S_OK;
}

HRESULT ValueExtent(IDWriteFactory* factory, IDWriteTextFormat* format,
    const HudRenderOptions& options, HudSegmentKind kind, IDWriteTextLayout* actual, float& width)
{
    HRESULT hr = ReservedValueWidth(factory, format, options, kind, width);
    if (FAILED(hr))
        return hr;
    width = std::max(width, Width(actual));
    return S_OK;
}

HRESULT DrawValue(ID2D1DeviceContext* context, IDWriteFactory* factory,
    IDWriteTextFormat* mainFormat,
    const std::wstring& text, const HudRenderOptions& options,
    float x, float y, ID2D1Brush* brush)
{
    ComPtr<IDWriteTextFormat> unitFormat;
    HRESULT hr = CreateTextFormat(factory, options, unitFormat, true);
    if (FAILED(hr)) return hr;
    std::size_t cursor = 0;
    for (const auto& range : FindHudUnitRangesImpl(text))
    {
        if (range.start > cursor)
        {
            ComPtr<IDWriteTextLayout> layout;
            hr = CreateLayout(factory, mainFormat, text.substr(cursor, range.start - cursor),
                options, true, false, layout);
            if (FAILED(hr)) return hr;
            context->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush);
            x += Width(layout.Get());
        }
        ComPtr<IDWriteTextLayout> unit;
        const std::wstring unitText = text.substr(range.start, range.length);
        hr = CreateLayout(factory, unitFormat.Get(), unitText, options, true, false, unit);
        if (FAILED(hr)) return hr;
        context->DrawTextLayout(D2D1::Point2F(x, y), unit.Get(), brush);
        x += Width(unit.Get());
        cursor = range.start + range.length;
    }
    if (cursor < text.size())
    {
        ComPtr<IDWriteTextLayout> layout;
        hr = CreateLayout(factory, mainFormat, text.substr(cursor), options, true, false, layout);
        if (FAILED(hr)) return hr;
        context->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush);
    }
    return S_OK;
}

HRESULT RunWidth(IDWriteFactory* factory, IDWriteTextFormat* format,
    const HudTextRun& run, const HudRenderOptions& options, float& width, float& height)
{
    ComPtr<IDWriteTextLayout> label;
    ComPtr<IDWriteTextLayout> value;
    HRESULT hr = CreateLayout(factory, format, run.label, options, false, false, label);
    if (FAILED(hr))
        return hr;
    hr = CreateLayout(factory, format, run.value, options, true, true, value);
    if (FAILED(hr))
        return hr;
    float valueWidth{};
    hr = ValueExtent(factory, format, options, run.kind, value.Get(), valueWidth);
    if (FAILED(hr))
        return hr;
    width = Width(label.Get()) + SegmentGap(options) + valueWidth;
    height = std::max(Height(label.Get()), Height(value.Get()));
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

std::vector<HudUnitRange> FindHudUnitRanges(const std::wstring& text)
{
    return FindHudUnitRangesImpl(text);
}

float RightAlignedOffset(float reservedWidth, float actualWidth) noexcept
{
    return actualWidth < reservedWidth ? reservedWidth - actualWidth : 0.0f;
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
    const float textHeight = measure.textHeight > 0.0f
        ? measure.textHeight : DipFromPhysicalPixels(options.fontPixelSize, options.dpi);
    const float textY = viewport.top + std::max(0.0f, (barHeight - textHeight) / 2.0f);
    HudRenderGeometry geometry{
        D2D1::RectF(viewport.left, viewport.top, viewport.right,
            std::min(viewport.bottom, viewport.top + barHeight)),
        D2D1::Point2F(contentX + padding, textY)};
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
    hr = CreateLayout(factory_, format.Get(), L"|", options, false, false, separator);
    if (FAILED(hr))
        return hr;
    result.textHeight = Height(separator.Get());
    for (size_t i = 0; i < runs.size(); ++i)
    {
        float width{};
        float height{};
        hr = RunWidth(factory_, format.Get(), runs[i], options, width, height);
        if (FAILED(hr))
            return hr;
        result.textHeight = std::max(result.textHeight, height);
        result.contentWidth += width;
        if (i + 1 < runs.size())
            result.contentWidth += SeparatorGap(options) * 2.0f + Width(separator.Get());
    }
    return S_OK;
}

HRESULT HudRenderer::MeasureReservedHudWidth(
    const HudRenderOptions& options, float& width) const
{
    const std::vector<HudTextRun> reservedRuns{
        { HudSegmentKind::Graphics, L"Vulkan", L"999 FPS" },
        { HudSegmentKind::Cpu, L"CPU", L"100% 100\u00B0C" },
        { HudSegmentKind::Gpu, L"GPU", L"100% VRAM 99.9 GB" },
        { HudSegmentKind::Tdp, L"TDP", L"99.9 W" },
        { HudSegmentKind::SystemPower, L"SYS", L"99.9 W" },
        { HudSegmentKind::Fan, L"FAN", L"9999 RPM" },
        { HudSegmentKind::Battery, L"BAT", L"100% 9.9h" },
    };
    HudMeasureResult result{};
    const HRESULT hr = Measure(reservedRuns, options, result);
    if (SUCCEEDED(hr))
        width = result.contentWidth;
    return hr;
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
    hr = CreateLayout(factory_, format.Get(), L"|", options, false, false, separator);
    if (FAILED(hr)) return hr;

    TextAntialiasModeGuard antialiasMode(context);
    context->FillRectangle(geometry.background, background.Get());
    float x = geometry.textOrigin.x;
    for (size_t i = 0; i < runs.size(); ++i)
    {
        ComPtr<IDWriteTextLayout> label;
        ComPtr<IDWriteTextLayout> value;
        hr = CreateLayout(factory_, format.Get(), runs[i].label, options, false, false, label);
        if (FAILED(hr)) return hr;
        hr = CreateLayout(factory_, format.Get(), runs[i].value, options, true, true, value);
        if (FAILED(hr)) return hr;
        float valueWidth{};
        hr = ValueExtent(factory_, format.Get(), options, runs[i].kind, value.Get(), valueWidth);
        if (FAILED(hr)) return hr;

        ComPtr<ID2D1SolidColorBrush> labelBrush;
        hr = context->CreateSolidColorBrush(LabelColor(runs[i].kind), &labelBrush);
        if (FAILED(hr)) return hr;
        context->DrawTextLayout(D2D1::Point2F(x, geometry.textOrigin.y), label.Get(), labelBrush.Get());
        x += Width(label.Get()) + SegmentGap(options);
        const float actualValueWidth = Width(value.Get());
        const float valueX = x + RightAlignedOffset(valueWidth, actualValueWidth);
        hr = DrawValue(context, factory_, format.Get(), runs[i].value, options,
            valueX, geometry.textOrigin.y, white.Get());
        if (FAILED(hr)) return hr;
        x += valueWidth;
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
