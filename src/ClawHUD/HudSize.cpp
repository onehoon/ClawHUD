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

HudRenderOptions BuildHudRenderOptionsForSize(
    int offset, const HudLayoutOptions& layout) noexcept
{
    constexpr float kBaseFontSize = 20.0f;
    constexpr float kBaseUnitFontSize = 11.0f;
    constexpr float kBaseBarHeight = 30.0f;
    constexpr float kBaseHorizontalPadding = 8.0f;
    constexpr float kBaseSegmentGap = 6.0f;
    constexpr float kBaseSeparatorGap = 10.0f;

    const float mainFont = kBaseFontSize +
        static_cast<float>(ClampHudSizeOffset(offset));
    const float scale = mainFont / kBaseFontSize;

    HudRenderOptions options{};
    options.layout = layout;
    options.fontPixelSize = mainFont;
    options.unitFontPixelSize = kBaseUnitFontSize * scale;
    options.barPixelHeight = static_cast<float>(std::round(
        static_cast<double>(kBaseBarHeight) * static_cast<double>(mainFont) /
        static_cast<double>(kBaseFontSize)));
    options.horizontalPaddingPx = kBaseHorizontalPadding * scale;
    options.segmentGapPx = kBaseSegmentGap * scale;
    options.separatorGapPx = kBaseSeparatorGap * scale;
    return options;
}
}
