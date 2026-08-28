#include "HudSize.h"

#include <cmath>
#include <cwchar>
#include <string>

namespace clawhud
{
int ClampHudSizeOffset(int offset) noexcept
{
    return offset < kMinHudSizeOffset ? kMinHudSizeOffset :
        offset > kMaxHudSizeOffset ? kMaxHudSizeOffset : offset;
}

int ParseHudSizeOffset(std::wstring_view value)
{
    if (value.empty())
        return 0;

    const std::wstring text(value);
    wchar_t* end{};
    const long parsed = std::wcstol(text.c_str(), &end, 10);
    if (end == text.c_str() || !end || *end != L'\0')
        return 0;
    return ClampHudSizeOffset(static_cast<int>(parsed));
}

HudFont ParseHudFont(std::wstring_view value)
{
    return _wcsicmp(std::wstring(value).c_str(), L"SegoeUIVariable") == 0
        ? HudFont::SegoeUiVariable : HudFont::Unispace;
}

const wchar_t* HudFontIniToken(HudFont font) noexcept
{
    return font == HudFont::SegoeUiVariable ? L"SegoeUIVariable" : L"Unispace";
}

int CommitHudSizeOffsetAfterRecreation(
    int previous, int requested, bool recreationSucceeded) noexcept
{
    return recreationSucceeded ? ClampHudSizeOffset(requested) : previous;
}

HudAlignment CommitHudAlignmentAfterRecreation(
    HudAlignment previous, HudAlignment requested, bool recreationSucceeded) noexcept
{
    return recreationSucceeded ? requested : previous;
}

HudBackgroundMode CommitHudBackgroundModeAfterRecreation(
    HudBackgroundMode previous, HudBackgroundMode requested, bool recreationSucceeded) noexcept
{
    return recreationSucceeded ? requested : previous;
}

bool ShouldRestoreHudVisibility(bool wasVisible) noexcept
{
    return wasVisible;
}

HudRenderOptions BuildHudRenderOptionsForSize(
    int offset, const HudLayoutOptions& layout, HudFont font) noexcept
{
    constexpr float kBaseFontSize = 20.0f;
    constexpr float kUnitScale = 0.55f;
    constexpr float kBaseBarHeight = 30.0f;
    constexpr float kPhysicalPadding = 11.0f;
    constexpr float kSegmentGap = 10.0f;
    constexpr float kMetricGap = 8.0f;
    constexpr float kSeparatorGap = 16.0f;

    const float mainFont = kBaseFontSize +
        static_cast<float>(ClampHudSizeOffset(offset));

    HudRenderOptions options{};
    options.layout = layout;
    options.font = font;
    options.fontPixelSize = mainFont;
    options.unitFontPixelSize = mainFont * kUnitScale;
    options.barPixelHeight = kBaseBarHeight;
    options.horizontalPaddingPx = kPhysicalPadding;
    options.segmentGapPx = kSegmentGap;
    options.metricGapPx = kMetricGap;
    options.separatorGapPx = kSeparatorGap;
    return options;
}
}
