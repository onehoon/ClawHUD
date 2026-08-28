#pragma once

#include "HudPresentationContract.h"
#include "HudRenderer.h"
#include "HudWindowGeometry.h"

#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <presentation.h>
#include <wrl/client.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>

namespace clawhud
{
inline HudRenderOptions BuildEffectiveHudRenderOptions(
    const HudRenderOptions& requested,
    const HudRenderOptions& initialized,
    float dpi) noexcept
{
    HudRenderOptions effective = requested;
    effective.dpi = dpi;
    effective.barPixelHeight = initialized.barPixelHeight;
    return effective;
}

inline UINT CalculateHudContentWidthPixels(
    float contentWidthDip, float dpi, UINT monitorWidth) noexcept
{
    const double scale = dpi > 0.0f ? static_cast<double>(dpi) / 96.0 : 1.0;
    const double nonNegativeWidth = contentWidthDip > 0.0f ? contentWidthDip : 0.0f;
    const double pixels = std::ceil(nonNegativeWidth * scale);
    return static_cast<UINT>(std::clamp(
        pixels, 1.0, static_cast<double>(monitorWidth)));
}

inline D3D11_BOX HudAlphaSampleSourceBox(UINT x, UINT y) noexcept
{
    return D3D11_BOX{ x, y, 0, x + 1, y + 1, 1 };
}

class HudPresentation
{
public:
    ~HudPresentation();

    HRESULT Initialize(HINSTANCE instance, const HudRenderOptions& options = {});
    HRESULT Render(const HudTelemetrySnapshot& snapshot, const HudRenderOptions& options);
    HRESULT Show();
    HRESULT Hide();
    bool Visible() const noexcept { return visible_; }
    bool Initialized() const noexcept { return initialized_; }
    void Shutdown() noexcept;

private:
    static constexpr auto kHudPresentationContract = ProductionHudPresentationContract();
    struct HudFrameBuffer
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<IPresentationBuffer> presentationBuffer;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmapTarget;
    };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    HRESULT CreateWindowHost(HINSTANCE instance);
    HRESULT CreateGraphics();
    HRESULT CreatePresentationSurface();
    HRESULT CreateBitmapTargets();
    HRESULT ResizeContentWidth(UINT widthPx, HudAlignment alignment);
#ifdef _DEBUG
    HRESULT ValidatePresentedAlpha(
        ID3D11Texture2D* texture, UINT sampleX, UINT sampleY,
        BYTE expectedAlpha);
#endif
    HRESULT TryAcquireAvailableBuffer(HudFrameBuffer*& selected) noexcept;
    HRESULT RefreshDisplayIfNeeded();
    HRESULT CommitVisibility(bool visible);

    HINSTANCE instance_{};
    HWND window_{};
    HANDLE surfaceHandle_{ INVALID_HANDLE_VALUE };
    int xPx_{};
    int yPx_{};
    RECT monitorRect_{};
    UINT widthPx_{};
    UINT surfaceWidthPx_{};
    UINT heightPx_{};
    float dpi_{ 96.0f };
    float barPixelHeight_{ 30.0f };
    bool visible_{};
    bool initialized_{};
    bool displayChangePending_{};
#ifdef _DEBUG
    int debugLastValidatedAlpha_{ -1 };
#endif
    HudRenderOptions initializationOptions_{};

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> compositionDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> compositionTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> compositionSurface_;
    Microsoft::WRL::ComPtr<IPresentationFactory> presentationFactory_;
    Microsoft::WRL::ComPtr<IPresentationManager> presentationManager_;
    Microsoft::WRL::ComPtr<IPresentationSurface> presentationSurface_;
    std::array<HudFrameBuffer, kHudPresentationContract.bufferCount> buffers_{};
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    std::unique_ptr<HudRenderer> renderer_;
};
}
