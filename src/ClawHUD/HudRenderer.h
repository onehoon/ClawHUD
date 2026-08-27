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
inline constexpr std::uint32_t kHudBackgroundColor = 0x020202;
inline constexpr std::uint32_t kHudCpuColor = 0x2E97CB;
inline constexpr std::uint32_t kHudGpuColor = 0x2E9762;
inline constexpr std::uint32_t kHudVramColor = 0xAD64C1;
inline constexpr std::uint32_t kHudGraphicsColor = 0xEB5B5B;
inline constexpr std::uint32_t kHudSystemColor = 0xFF9078;
inline constexpr std::uint32_t kHudSeparatorColor = 0xAD64C1;
inline constexpr float kHudTextOutlinePx = 1.5f;
inline constexpr float kHudSeparatorCorePx = 2.0f;
inline constexpr float kHudSeparatorOuterPx = 3.0f;

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

    bool PrivateFontLoaded() const noexcept { return privateFontLoaded_; }

private:
    IDWriteFactory* factory_{};
    Microsoft::WRL::ComPtr<IDWriteFontCollection> fontCollection_;
    bool privateFontLoaded_{};
};
}
