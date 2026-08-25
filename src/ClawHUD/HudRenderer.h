#pragma once

#include "HudModel.h"

#include <d2d1_1.h>
#include <dwrite.h>

#include <cstdint>
#include <string>
#include <vector>

namespace clawhud
{
struct HudRenderOptions
{
    HudLayoutOptions layout{};
    float fontPixelSize{20.0f};
    float unitFontPixelSize{11.0f};
    float barPixelHeight{30.0f};
    float horizontalPaddingPx{8.0f};
    float segmentGapPx{6.0f};
    float separatorGapPx{6.0f};
    float dpi{96.0f};
};

struct HudMeasureResult
{
    float contentWidth{};
    float contentHeight{};
    float textHeight{};
};

struct HudUnitRange
{
    std::uint32_t start{};
    std::uint32_t length{};
};

struct HudRenderGeometry
{
    D2D1_RECT_F background{};
    D2D1_POINT_2F textOrigin{};
};

float DipFromPhysicalPixels(float pixels, float dpi) noexcept;
std::vector<HudUnitRange> FindHudUnitRanges(const std::wstring& text);
float RightAlignedOffset(float reservedWidth, float actualWidth) noexcept;
HudRenderGeometry CalculateHudGeometry(
    const D2D1_RECT_F& viewport,
    const HudMeasureResult& measure,
    const HudRenderOptions& options) noexcept;

class HudRenderer
{
public:
    explicit HudRenderer(IDWriteFactory* factory) noexcept : factory_(factory) {}

    HRESULT Measure(
        const std::vector<HudTextRun>& runs,
        const HudRenderOptions& options,
        HudMeasureResult& result) const;

    HRESULT Draw(
        ID2D1DeviceContext* context,
        const std::vector<HudTextRun>& runs,
        const HudRenderOptions& options,
        const D2D1_RECT_F& viewport) const;

private:
    IDWriteFactory* factory_{};
};
}
