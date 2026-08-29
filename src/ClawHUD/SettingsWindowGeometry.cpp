#include "SettingsWindowGeometry.h"

#include <algorithm>

RECT ClampSettingsWindowRectToWorkArea(
    RECT windowRect, RECT workArea,
    int minimumWidth, int minimumHeight) noexcept
{
    const int workWidth = std::max(0L, workArea.right - workArea.left);
    const int workHeight = std::max(0L, workArea.bottom - workArea.top);
    if (workWidth == 0 || workHeight == 0)
        return windowRect;

    const int currentWidth = std::max(0L, windowRect.right - windowRect.left);
    const int currentHeight = std::max(0L, windowRect.bottom - windowRect.top);
    const int width = std::clamp(currentWidth,
        std::min(std::max(0, minimumWidth), workWidth), workWidth);
    const int height = std::clamp(currentHeight,
        std::min(std::max(0, minimumHeight), workHeight), workHeight);
    const LONG x = std::clamp(windowRect.left,
        workArea.left, workArea.right - width);
    const LONG y = std::clamp(windowRect.top,
        workArea.top, workArea.bottom - height);
    return { x, y, x + width, y + height };
}
