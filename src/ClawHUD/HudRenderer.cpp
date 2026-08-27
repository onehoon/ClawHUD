#include "HudRenderer.h"

#include "RuntimeLogger.h"

#include <algorithm>
#include <sstream>

#include <wrl/client.h>
#include <dwrite_3.h>

namespace clawhud
{
namespace
{
using Microsoft::WRL::ComPtr;

constexpr float kMaxLayoutDimension = 100000.0f;
constexpr wchar_t kFontName[] = L"Unispace";

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

HRESULT CreateTextFormat(IDWriteFactory* factory, IDWriteFontCollection* collection,
    const HudRenderOptions& options,
    ComPtr<IDWriteTextFormat>& format, bool unit = false)
{
    if (!factory)
        return E_INVALIDARG;

    const float size = DipFromPhysicalPixels(
        unit ? options.unitFontPixelSize : options.fontPixelSize, options.dpi);
    HRESULT hr = factory->CreateTextFormat(kFontName, collection,
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

bool IsNumericAttachedUnit(const std::wstring& text, std::size_t start,
    std::size_t length) noexcept
{
    if (start == 0)
        return false;
    const wchar_t previous = text[start - 1];
    const bool numericBefore = (previous >= L'0' && previous <= L'9') || previous == L'.';
    if (!numericBefore)
        return false;

    const std::size_t end = start + length;
    const auto isWord = [](wchar_t value)
    {
        return value >= L'A' && value <= L'Z' || value >= L'a' && value <= L'z' ||
            value >= L'0' && value <= L'9' || value == L'_';
    };
    return end >= text.size() || !isWord(text[end]);
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
            if (!wordBoundary || IsWordBoundary(text, start, length) ||
                IsNumericAttachedUnit(text, start, length))
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

HRESULT CreatePrivateFontCollection(IDWriteFactory* factory, const std::wstring& path,
    ComPtr<IDWriteFontCollection>& collection)
{
    if (!factory || path.empty()) return E_INVALIDARG;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        const DWORD error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_FILE_NOT_FOUND;
        return HRESULT_FROM_WIN32(error ? error : ERROR_FILE_NOT_FOUND);
    }
    ComPtr<IDWriteFactory3> factory3;
    HRESULT hr = factory->QueryInterface(IID_PPV_ARGS(&factory3));
    if (FAILED(hr)) return hr;
    ComPtr<IDWriteFontFile> fontFile;
    if (FAILED(hr = factory->CreateFontFileReference(path.c_str(), nullptr, &fontFile))) return hr;
    BOOL supported{};
    DWRITE_FONT_FILE_TYPE fileType{};
    DWRITE_FONT_FACE_TYPE faceType{};
    UINT32 faceCount{};
    if (FAILED(hr = fontFile->Analyze(&supported, &fileType, &faceType, &faceCount))) return hr;
    if (!supported || faceCount == 0) return DWRITE_E_FILEFORMAT;
    ComPtr<IDWriteFontSetBuilder> builder;
    ComPtr<IDWriteFactory5> factory5;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5))))
    {
        ComPtr<IDWriteFontSetBuilder1> builder1;
        if (FAILED(hr = factory5->CreateFontSetBuilder(&builder1))) return hr;
        if (FAILED(hr = builder1->AddFontFile(fontFile.Get()))) return hr;
        builder = builder1;
    }
    else
    {
        if (FAILED(hr = factory3->CreateFontSetBuilder(&builder))) return hr;
        for (UINT32 faceIndex = 0; faceIndex < faceCount; ++faceIndex)
        {
            ComPtr<IDWriteFontFaceReference> face;
            if (FAILED(hr = factory3->CreateFontFaceReference(fontFile.Get(), faceIndex,
                DWRITE_FONT_SIMULATIONS_NONE, &face))) return hr;
            if (FAILED(hr = builder->AddFontFaceReference(face.Get(), nullptr, 0))) return hr;
        }
    }
    ComPtr<IDWriteFontSet> set;
    if (FAILED(hr = builder->CreateFontSet(&set))) return hr;
    ComPtr<IDWriteFontCollection1> collection1;
    if (FAILED(hr = factory3->CreateFontCollectionFromFontSet(set.Get(), &collection1))) return hr;
    UINT32 familyIndex{};
    BOOL exists{};
    if (FAILED(hr = collection1->FindFamilyName(kFontName, &familyIndex, &exists)) || !exists)
        return FAILED(hr) ? hr : DWRITE_E_NOFONT;
    collection = collection1;
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

const wchar_t* LabelExemplar(HudSegmentKind kind) noexcept
{
    switch (kind)
    {
    case HudSegmentKind::Graphics: return L"Vulkan";
    case HudSegmentKind::Cpu: return L"CPU";
    case HudSegmentKind::Gpu: return L"GPU";
    case HudSegmentKind::Vram: return L"VRAM";
    case HudSegmentKind::Tdp: return L"TDP";
    case HudSegmentKind::SystemPower: return L"SYS";
    case HudSegmentKind::Fan: return L"FAN";
    case HudSegmentKind::Battery: return L"BAT";
    }
    return L"";
}

const wchar_t* ValueExemplar(HudSegmentKind kind) noexcept
{
    switch (kind)
    {
    case HudSegmentKind::Graphics: return L"999FPS";
    case HudSegmentKind::Cpu: return L"100% 100\u00B0C";
    case HudSegmentKind::Gpu: return L"100%";
    case HudSegmentKind::Vram: return L"99.9GB";
    case HudSegmentKind::Tdp:
    case HudSegmentKind::SystemPower: return L"99.9W";
    case HudSegmentKind::Fan: return L"9999RPM";
    case HudSegmentKind::Battery: return L"100% 9.9h";
    }
    return L"";
}

HRESULT MeasureSegmentMetrics(IDWriteFactory* factory, IDWriteTextFormat* format,
    const HudRenderOptions& options, HudSegmentKind kind, HudSegmentMetrics& metrics)
{
    ComPtr<IDWriteTextLayout> label;
    ComPtr<IDWriteTextLayout> exemplar;
    HRESULT hr = CreateLayout(factory, format, LabelExemplar(kind), options, false, false, label);
    if (FAILED(hr))
        return hr;
    hr = CreateLayout(factory, format, ValueExemplar(kind), options, true, true, exemplar);
    if (FAILED(hr))
        return hr;
    metrics.labelSlotWidth = Width(label.Get());
    metrics.valueSlotWidth = Width(exemplar.Get());
    metrics.segmentWidth = metrics.labelSlotWidth + SegmentGap(options) + metrics.valueSlotWidth;
    return S_OK;
}

void DrawOutlinedLayout(ID2D1DeviceContext* context, IDWriteTextLayout* layout,
    float x, float y, ID2D1Brush* textBrush, ID2D1Brush* outlineBrush,
    const HudRenderOptions& options);

HRESULT DrawValue(ID2D1DeviceContext* context, IDWriteFactory* factory,
    IDWriteFontCollection* collection,
    IDWriteTextFormat* mainFormat,
    const std::wstring& text, const HudRenderOptions& options,
    float x, float y, ID2D1Brush* brush, ID2D1Brush* outlineBrush)
{
    ComPtr<IDWriteTextFormat> unitFormat;
    HRESULT hr = CreateTextFormat(factory, collection, options, unitFormat, true);
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
            DrawOutlinedLayout(context, layout.Get(), x, y, brush, outlineBrush, options);
            x += Width(layout.Get());
        }
        ComPtr<IDWriteTextLayout> unit;
        const std::wstring unitText = text.substr(range.start, range.length);
        hr = CreateLayout(factory, unitFormat.Get(), unitText, options, true, false, unit);
        if (FAILED(hr)) return hr;
        DrawOutlinedLayout(context, unit.Get(), x, y, brush, outlineBrush, options);
        x += Width(unit.Get());
        cursor = range.start + range.length;
    }
    if (cursor < text.size())
    {
        ComPtr<IDWriteTextLayout> layout;
        hr = CreateLayout(factory, mainFormat, text.substr(cursor), options, true, false, layout);
        if (FAILED(hr)) return hr;
        DrawOutlinedLayout(context, layout.Get(), x, y, brush, outlineBrush, options);
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
    HudSegmentMetrics metrics{};
    hr = MeasureSegmentMetrics(factory, format, options, run.kind, metrics);
    if (FAILED(hr))
        return hr;
    width = std::max(metrics.labelSlotWidth, Width(label.Get())) + SegmentGap(options) +
        std::max(metrics.valueSlotWidth, Width(value.Get()));
    height = std::max(Height(label.Get()), Height(value.Get()));
    return S_OK;
}

D2D1_COLOR_F LabelColor(HudSegmentKind kind) noexcept
{
    switch (kind)
    {
    case HudSegmentKind::Cpu: return D2D1::ColorF(kHudCpuColor, 1.0f);
    case HudSegmentKind::Gpu: return D2D1::ColorF(kHudGpuColor, 1.0f);
    case HudSegmentKind::Vram: return D2D1::ColorF(kHudVramColor, 1.0f);
    case HudSegmentKind::Graphics: return D2D1::ColorF(kHudGraphicsColor, 1.0f);
    case HudSegmentKind::SystemPower:
    case HudSegmentKind::Battery: return D2D1::ColorF(kHudSystemColor, 1.0f);
    case HudSegmentKind::Tdp: return D2D1::ColorF(kHudCpuColor, 1.0f);
    case HudSegmentKind::Fan: return D2D1::ColorF(kHudGraphicsColor, 1.0f);
    default: return D2D1::ColorF(D2D1::ColorF::White, 1.0f);
    }
}

float SeparatorWidth(const HudRenderOptions& options) noexcept
{
    return DipFromPhysicalPixels(kHudSeparatorOuterPx, options.dpi);
}

float SeparatorHeight(const HudRenderOptions& options) noexcept
{
    return DipFromPhysicalPixels(options.fontPixelSize * 0.85f, options.dpi);
}

void DrawOutlinedLayout(ID2D1DeviceContext* context, IDWriteTextLayout* layout,
    float x, float y, ID2D1Brush* textBrush, ID2D1Brush* outlineBrush,
    const HudRenderOptions& options)
{
    const float offset = DipFromPhysicalPixels(kHudTextOutlinePx, options.dpi);
    for (const auto& point : { D2D1::Point2F(x - offset, y), D2D1::Point2F(x + offset, y),
        D2D1::Point2F(x, y - offset), D2D1::Point2F(x, y + offset) })
        context->DrawTextLayout(point, layout, outlineBrush);
    context->DrawTextLayout(D2D1::Point2F(x, y), layout, textBrush);
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

HudRenderer::HudRenderer(IDWriteFactory* factory, const std::wstring& fontFilePath) noexcept
    : factory_(factory)
{
    if (!factory_ || fontFilePath.empty()) return;
    const HRESULT hr = CreatePrivateFontCollection(factory_, fontFilePath, fontCollection_);
    if (SUCCEEDED(hr))
    {
        privateFontLoaded_ = true;
        std::wostringstream message;
        message << L"HUD font loaded: Unispace (private), path=" << fontFilePath;
        RuntimeLogger::Log(RuntimeLogLevel::Info, message.str());
    }
    else
    {
        std::wostringstream message;
        message << L"HUD font load failed: " << fontFilePath << L", hr=0x" << std::hex
            << static_cast<unsigned long>(hr);
        RuntimeLogger::Log(RuntimeLogLevel::Error, message.str());
        RuntimeLogger::Log(RuntimeLogLevel::Warn, L"HUD font fallback active");
    }
}

HRESULT HudRenderer::MeasureMainTextHeight(const HudRenderOptions& options, float& heightPx) const
{
    heightPx = 0.0f;
    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = CreateTextFormat(factory_, fontCollection_.Get(), options, format);
    if (FAILED(hr)) return hr;
    ComPtr<IDWriteTextLayout> layout;
    hr = CreateLayout(factory_, format.Get(), L"Ag", options, false, false, layout);
    if (FAILED(hr)) return hr;
    heightPx = Height(layout.Get()) * options.dpi / 96.0f;
    return S_OK;
}

float DipFromPhysicalPixels(float pixels, float dpi) noexcept
{
    return dpi > 0.0f ? pixels * 96.0f / dpi : pixels;
}

std::vector<HudUnitRange> FindHudUnitRanges(const std::wstring& text)
{
    return FindHudUnitRangesImpl(text);
}

HudSegmentLayout CalculateHudSegmentLayout(float segmentStart,
    const HudSegmentMetrics& metrics, float actualLabelWidth, float actualValueWidth,
    float segmentGap) noexcept
{
    const float labelSlotWidth = std::max(metrics.labelSlotWidth, actualLabelWidth);
    const float valueSlotWidth = std::max(metrics.valueSlotWidth, actualValueWidth);
    const float valueX = segmentStart + actualLabelWidth + segmentGap;
    return HudSegmentLayout{
        segmentStart,
        valueX,
        labelSlotWidth + segmentGap + valueSlotWidth};
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
    const bool contentBackground =
        options.layout.backgroundMode == HudBackgroundMode::ContentWidth;
    const float backgroundLeft = contentBackground ? contentX : viewport.left;
    const float backgroundRight = contentBackground
        ? std::min(viewport.right, contentX + contentWidth) : viewport.right;
    HudRenderGeometry geometry{
        D2D1::RectF(backgroundLeft, viewport.top, backgroundRight,
            std::min(viewport.bottom, viewport.top + barHeight)),
        D2D1::Point2F(contentX + padding, textY)};
    return geometry;
}

HRESULT HudRenderer::Measure(const std::vector<HudTextRun>& runs,
    const HudRenderOptions& options, HudMeasureResult& result) const
{
    result = {};
    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = CreateTextFormat(factory_, fontCollection_.Get(), options, format);
    if (FAILED(hr))
        return hr;

    result.contentWidth = Padding(options) * 2.0f;
    result.contentHeight = DipFromPhysicalPixels(options.barPixelHeight, options.dpi);
    result.textHeight = 0.0f;
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
            result.contentWidth += SeparatorGap(options) * 2.0f + SeparatorWidth(options);
    }
    return S_OK;
}

HRESULT HudRenderer::MeasureReservedHudWidth(
    const HudRenderOptions& options, float& width) const
{
    const std::vector<HudTextRun> reservedRuns{
        { HudSegmentKind::Graphics, L"Vulkan", L"999FPS" },
        { HudSegmentKind::Cpu, L"CPU", L"100% 100\u00B0C" },
        { HudSegmentKind::Gpu, L"GPU", L"100%" },
        { HudSegmentKind::Vram, L"VRAM", L"99.9GB" },
        { HudSegmentKind::Tdp, L"TDP", L"99.9W" },
        { HudSegmentKind::SystemPower, L"SYS", L"99.9W" },
        { HudSegmentKind::Fan, L"FAN", L"9999RPM" },
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
    const float backgroundAlpha = std::clamp(options.layout.backgroundOpacity, 0.0f, 1.0f);
    hr = context->CreateSolidColorBrush(D2D1::ColorF(kHudBackgroundColor, backgroundAlpha), &background);
    if (FAILED(hr)) return hr;
    hr = context->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &white);
    if (FAILED(hr)) return hr;
    hr = context->CreateSolidColorBrush(D2D1::ColorF(kHudSeparatorColor, 1.0f), &separatorBrush);
    if (FAILED(hr)) return hr;

    ComPtr<IDWriteTextFormat> format;
    hr = CreateTextFormat(factory_, fontCollection_.Get(), options, format);
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
        HudSegmentMetrics metrics{};
        hr = MeasureSegmentMetrics(factory_, format.Get(), options, runs[i].kind, metrics);
        if (FAILED(hr)) return hr;

        const auto layout = CalculateHudSegmentLayout(x, metrics, Width(label.Get()),
            Width(value.Get()), SegmentGap(options));

        ComPtr<ID2D1SolidColorBrush> labelBrush;
        hr = context->CreateSolidColorBrush(LabelColor(runs[i].kind), &labelBrush);
        if (FAILED(hr)) return hr;
        ComPtr<ID2D1SolidColorBrush> outlineBrush;
        hr = context->CreateSolidColorBrush(D2D1::ColorF(0x000000, 1.0f), &outlineBrush);
        if (FAILED(hr)) return hr;
        DrawOutlinedLayout(context, label.Get(), layout.labelX, geometry.textOrigin.y,
            labelBrush.Get(), outlineBrush.Get(), options);
        hr = DrawValue(context, factory_, fontCollection_.Get(), format.Get(), runs[i].value, options,
            layout.valueX, geometry.textOrigin.y, white.Get(), outlineBrush.Get());
        if (FAILED(hr)) return hr;
        x += layout.segmentWidth;
        if (i + 1 < runs.size())
        {
            x += SeparatorGap(options);
            const float separatorHeight = SeparatorHeight(options);
            const float separatorY = viewport.top +
                std::max(0.0f, (geometry.background.bottom - geometry.background.top - separatorHeight) / 2.0f);
            const float centerX = x + SeparatorWidth(options) / 2.0f;
            context->DrawLine(D2D1::Point2F(centerX, separatorY),
                D2D1::Point2F(centerX, separatorY + separatorHeight),
                outlineBrush.Get(), DipFromPhysicalPixels(kHudSeparatorOuterPx, options.dpi));
            context->DrawLine(D2D1::Point2F(centerX, separatorY),
                D2D1::Point2F(centerX, separatorY + separatorHeight),
                separatorBrush.Get(), DipFromPhysicalPixels(kHudSeparatorCorePx, options.dpi));
            x += SeparatorWidth(options) + SeparatorGap(options);
        }
    }
    return S_OK;
}
}
