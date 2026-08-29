#pragma once

#include <windows.h>

constexpr bool ShouldNormalizeSettingsWindow(
    bool minimized, bool maximized) noexcept
{
    return !minimized && !maximized;
}

RECT ClampSettingsWindowRectToWorkArea(
    RECT windowRect, RECT workArea,
    int minimumWidth, int minimumHeight) noexcept;
