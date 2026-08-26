#pragma once

#include "HudModel.h"

#include <windows.h>

namespace clawhud
{
struct HudWindowGeometry
{
    LONG xPx{};
    LONG yPx{};
    UINT widthPx{};
};

HudWindowGeometry CalculateHudWindowGeometry(
    const RECT& monitor,
    HudBackgroundMode mode,
    HudAlignment alignment,
    UINT reservedWidth) noexcept;
}
