#include "HudWindowGeometry.h"

#include <algorithm>

namespace clawhud
{
HudWindowGeometry CalculateHudWindowGeometry(
    const RECT& monitor,
    HudBackgroundMode mode,
    HudAlignment alignment,
    UINT reservedWidth) noexcept
{
    const LONG monitorWidth = std::max<LONG>(0, monitor.right - monitor.left);
    if (mode == HudBackgroundMode::FullWidth)
        return { monitor.left, monitor.top, static_cast<UINT>(monitorWidth) };

    const UINT width = std::min(reservedWidth, static_cast<UINT>(monitorWidth));
    LONG x = monitor.left;
    switch (alignment)
    {
    case HudAlignment::Center:
        x += (monitorWidth - static_cast<LONG>(width)) / 2;
        break;
    case HudAlignment::Right:
        x = monitor.right - static_cast<LONG>(width);
        break;
    case HudAlignment::Left:
        break;
    }
    return { x, monitor.top, width };
}
}
