#include "HudSize.h"

#include <cmath>
#include <iostream>

using namespace clawhud;

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
    const HudLayoutOptions layout{};
    const auto check = [&](int offset, float main, float unit, float bar,
        float padding, float segmentGap, float separatorGap)
    {
        const auto options = BuildHudRenderOptionsForSize(offset, layout);
        ok &= Check(Near(options.fontPixelSize, main), "main font size");
        ok &= Check(Near(options.unitFontPixelSize, unit), "unit font size");
        ok &= Check(Near(options.barPixelHeight, bar), "bar height");
        ok &= Check(Near(options.horizontalPaddingPx, padding), "horizontal padding");
        ok &= Check(Near(options.segmentGapPx, segmentGap), "segment gap");
        ok &= Check(Near(options.separatorGapPx, separatorGap), "separator gap");
    };

    check(-2, 18.0f, 9.9f, 30.0f, 5.0f, 8.0f, 8.0f);
    check(-1, 19.0f, 10.45f, 30.0f, 5.0f, 8.0f, 8.0f);
    check(0, 20.0f, 11.0f, 30.0f, 5.0f, 8.0f, 8.0f);
    check(1, 21.0f, 11.55f, 30.0f, 5.0f, 8.0f, 8.0f);
    check(2, 22.0f, 12.1f, 30.0f, 5.0f, 8.0f, 8.0f);

    ok &= Check(ClampHudSizeOffset(-100) == -2, "minimum clamp");
    ok &= Check(ClampHudSizeOffset(100) == 2, "maximum clamp");
    ok &= Check(ParseHudSizeOffset(L"") == 0, "missing size defaults");
    ok &= Check(ParseHudSizeOffset(L"invalid") == 0, "invalid size defaults");
    ok &= Check(ParseHudSizeOffset(L"-100") == -2, "persisted minimum clamp");
    ok &= Check(ParseHudSizeOffset(L"100") == 2, "persisted maximum clamp");
    ok &= Check(ParseHudSizeOffset(L"2") == 2, "persisted size restore");
    ok &= Check(ParseHudFont(L"") == HudFont::Unispace,
        "missing font defaults to Unispace");
    ok &= Check(ParseHudFont(L"Unispace") == HudFont::Unispace,
        "Unispace font parses");
    ok &= Check(ParseHudFont(L"SegoeUIVariable") == HudFont::SegoeUiVariable,
        "Segoe UI Variable font parses");
    ok &= Check(ParseHudFont(L"unknown") == HudFont::Unispace,
        "unknown font falls back to Unispace");
    ok &= Check(std::wstring(HudFontIniToken(HudFont::Unispace)) == L"Unispace" &&
        std::wstring(HudFontIniToken(HudFont::SegoeUiVariable)) == L"SegoeUIVariable",
        "font INI tokens");
    ok &= Check(CommitHudSizeOffsetAfterRecreation(0, 2, false) == 0,
        "failed recreation rolls back size");
    const int retryOffset = CommitHudSizeOffsetAfterRecreation(0, 2, true);
    ok &= Check(retryOffset == 2, "successful retry commits size");
    ok &= Check(CommitHudAlignmentAfterRecreation(
        HudAlignment::Left, HudAlignment::Right, false) == HudAlignment::Left,
        "failed recreation rolls back alignment");
    ok &= Check(CommitHudAlignmentAfterRecreation(
        HudAlignment::Left, HudAlignment::Right, true) == HudAlignment::Right,
        "successful recreation commits alignment");
    ok &= Check(CommitHudBackgroundModeAfterRecreation(
        HudBackgroundMode::FullWidth, HudBackgroundMode::ContentWidth, false) ==
        HudBackgroundMode::FullWidth,
        "failed recreation rolls back background mode");
    ok &= Check(CommitHudBackgroundModeAfterRecreation(
        HudBackgroundMode::FullWidth, HudBackgroundMode::ContentWidth, true) ==
        HudBackgroundMode::ContentWidth,
        "successful recreation commits background mode");
    ok &= Check(ShouldRestoreHudVisibility(true),
        "visible HUD remains visible after rollback retry");
    ok &= Check(!ShouldRestoreHudVisibility(false),
        "hidden HUD remains hidden after rollback retry");

    HudLayoutOptions customLayout{};
    customLayout.visibilityMode = HudVisibilityMode::Always;
    customLayout.alignment = HudAlignment::Right;
    customLayout.backgroundMode = HudBackgroundMode::ContentWidth;
    customLayout.backgroundOpacity = 0.35f;
    const auto customOptions = BuildHudRenderOptionsForSize(1, customLayout);
    ok &= Check(customOptions.layout.visibilityMode == HudVisibilityMode::Always &&
        customOptions.layout.alignment == HudAlignment::Right &&
        customOptions.layout.backgroundMode == HudBackgroundMode::ContentWidth &&
        Near(customOptions.layout.backgroundOpacity, 0.35f),
        "HUD layout passes through unchanged");
    ok &= Check(customOptions.font == HudFont::Unispace,
        "HUD render options default to Unispace");
    const auto segoeOptions = BuildHudRenderOptionsForSize(
        0, customLayout, HudFont::SegoeUiVariable);
    ok &= Check(segoeOptions.font == HudFont::SegoeUiVariable,
        "HUD render options pass through Segoe UI Variable");
    return ok ? 0 : 1;
}
