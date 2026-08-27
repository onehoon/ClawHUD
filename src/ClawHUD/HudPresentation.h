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

using HudPresentCallback = HRESULT (*)(void* context) noexcept;
using HudRenderObserver = void (*)(float backgroundOpacity, HRESULT presentResult, void* context) noexcept;

inline HRESULT CommitHudRenderFrame(
    float backgroundOpacity,
    HudPresentCallback present,
    void* presentContext,
    HudRenderObserver observer = nullptr,
    void* observerContext = nullptr) noexcept
{
    const HRESULT result = present(presentContext);
    if (observer != nullptr)
        observer(backgroundOpacity, result, observerContext);
    return result;
}

class HudPresentation
{
public:
    ~HudPresentation();

    HRESULT Initialize(HINSTANCE instance, const HudRenderOptions& options = {});
    HRESULT Render(
        const HudTelemetrySnapshot& snapshot,
        const HudRenderOptions& options,
        HudRenderObserver observer = nullptr,
        void* observerContext = nullptr);
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
    static HRESULT PresentFrame(void* context) noexcept;
    HRESULT CreateWindowHost(HINSTANCE instance);
    HRESULT CreateGraphics();
    HRESULT CreatePresentationSurface();
    HRESULT CreateBitmapTargets();
    HRESULT TryAcquireAvailableBuffer(HudFrameBuffer*& selected) noexcept;
    HRESULT RefreshDisplayIfNeeded();
    HRESULT CommitVisibility(bool visible);

    HINSTANCE instance_{};
    HWND window_{};
    HANDLE surfaceHandle_{ INVALID_HANDLE_VALUE };
    int xPx_{};
    int yPx_{};
    UINT widthPx_{};
    UINT heightPx_{};
    float dpi_{ 96.0f };
    float barPixelHeight_{ 30.0f };
    bool visible_{};
    bool initialized_{};
    bool displayChangePending_{};
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
