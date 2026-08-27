#pragma once

#include "HudModel.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

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
    float horizontalPaddingPx{5.0f};
    float segmentGapPx{8.0f};
    float separatorGapPx{8.0f};
    float dpi{96.0f};
    std::wstring fontFilePath;
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

struct HudSegmentMetrics
{
    float labelSlotWidth{};
    float valueSlotWidth{};
    float segmentWidth{};
};

struct HudSegmentLayout
{
    float labelX{};
    float valueX{};
    float segmentWidth{};
};

float DipFromPhysicalPixels(float pixels, float dpi) noexcept;
std::vector<HudUnitRange> FindHudUnitRanges(const std::wstring& text);
float RightAlignedOffset(float reservedWidth, float actualWidth) noexcept;
HudSegmentLayout CalculateHudSegmentLayout(
    float segmentStart,
    const HudSegmentMetrics& metrics,
    float actualLabelWidth,
    float actualValueWidth,
    float segmentGap) noexcept;
HudRenderGeometry CalculateHudGeometry(
    const D2D1_RECT_F& viewport,
    const HudMeasureResult& measure,
    const HudRenderOptions& options) noexcept;

class HudRenderer
{
public:
    explicit HudRenderer(IDWriteFactory* factory, const std::wstring& fontFilePath = {}) noexcept;

    HRESULT MeasureMainTextHeight(const HudRenderOptions& options, float& heightPx) const;

    HRESULT Measure(
        const std::vector<HudTextRun>& runs,
        const HudRenderOptions& options,
        HudMeasureResult& result) const;

    HRESULT MeasureReservedHudWidth(
        const HudRenderOptions& options,
        float& width) const;

    HRESULT Draw(
        ID2D1DeviceContext* context,
        const std::vector<HudTextRun>& runs,
        const HudRenderOptions& options,
        const D2D1_RECT_F& viewport) const;

private:
    IDWriteFactory* factory_{};
    Microsoft::WRL::ComPtr<IDWriteFontCollection> fontCollection_;
};
}
