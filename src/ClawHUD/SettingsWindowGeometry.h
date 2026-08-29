#pragma once

#include <windows.h>

RECT ClampSettingsWindowRectToWorkArea(
    RECT windowRect, RECT workArea,
    int minimumWidth, int minimumHeight) noexcept;
