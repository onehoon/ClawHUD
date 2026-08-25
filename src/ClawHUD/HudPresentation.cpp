#include "HudPresentation.h"

#include <cmath>

using Microsoft::WRL::ComPtr;

namespace clawhud
{
namespace
{
constexpr wchar_t kWindowClass[] = L"ClawHUD.MockHudSurface";

HRESULT LastErrorResult() noexcept
{
    return HRESULT_FROM_WIN32(GetLastError());
}
}

HudPresentation::~HudPresentation()
{
    Shutdown();
}

HRESULT HudPresentation::Initialize(HINSTANCE instance, const HudRenderOptions& options)
{
    if (initialized_)
        return S_OK;
    if (!instance || options.barPixelHeight <= 0.0f)
        return E_INVALIDARG;

    instance_ = instance;
    MONITORINFO monitor{ sizeof(monitor) };
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &monitor))
        return LastErrorResult();
    xPx_ = monitor.rcMonitor.left;
    yPx_ = monitor.rcMonitor.top;
    widthPx_ = static_cast<UINT>(monitor.rcMonitor.right - monitor.rcMonitor.left);
    heightPx_ = static_cast<UINT>(std::lround(options.barPixelHeight));
    if (!widthPx_ || !heightPx_)
        return E_INVALIDARG;

    HRESULT hr = CreateWindowHost(instance);
    if (FAILED(hr)) { Shutdown(); return hr; }
    dpi_ = static_cast<float>(GetDpiForWindow(window_));
    if (dpi_ <= 0.0f) dpi_ = 96.0f;
    if (FAILED(hr = CreateGraphics())) { Shutdown(); return hr; }
    if (FAILED(hr = CreatePresentationSurface())) { Shutdown(); return hr; }
    if (FAILED(hr = CreateBitmapTargets())) { Shutdown(); return hr; }
    initialized_ = true;
    return S_OK;
}

HRESULT HudPresentation::CreateWindowHost(HINSTANCE instance)
{
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    RegisterClassW(&windowClass);
    constexpr DWORD exStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
        WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP;
    window_ = CreateWindowExW(exStyle, kWindowClass, L"ClawHUD Mock HUD", WS_POPUP,
        xPx_, yPx_, static_cast<int>(widthPx_), static_cast<int>(heightPx_),
        nullptr, nullptr, instance, this);
    if (!window_)
        return LastErrorResult();
    if (!SetWindowPos(window_, HWND_TOPMOST, xPx_, yPx_, static_cast<int>(widthPx_),
        static_cast<int>(heightPx_), SWP_NOACTIVATE | SWP_NOOWNERZORDER))
        return LastErrorResult();
    return S_OK;
}

HRESULT HudPresentation::CreateGraphics()
{
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL selected{};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT |
        D3D11_CREATE_DEVICE_SINGLETHREADED |
        D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, &selected, &deviceContext_);
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(hr = device_.As(&dxgiDevice))) return hr;
    if (FAILED(hr = DCompositionCreateDevice(dxgiDevice.Get(), __uuidof(IDCompositionDevice),
        reinterpret_cast<void**>(compositionDevice_.ReleaseAndGetAddressOf())))) return hr;
    if (FAILED(hr = compositionDevice_->CreateTargetForHwnd(window_, TRUE, &compositionTarget_))) return hr;
    if (FAILED(hr = compositionDevice_->CreateVisual(&visual_))) return hr;
    if (FAILED(hr = compositionTarget_->SetRoot(visual_.Get()))) return hr;

    ComPtr<ID2D1Factory1> d2dFactory;
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory1), reinterpret_cast<void**>(d2dFactory.ReleaseAndGetAddressOf())))) return hr;
    ComPtr<ID2D1Device> d2dDevice;
    if (FAILED(hr = d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice))) return hr;
    if (FAILED(hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_))) return hr;
    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(writeFactory_.ReleaseAndGetAddressOf())))) return hr;
    renderer_ = std::make_unique<HudRenderer>(writeFactory_.Get());
    return S_OK;
}

HRESULT HudPresentation::CreatePresentationSurface()
{
    HRESULT hr = CreatePresentationFactory(device_.Get(), __uuidof(IPresentationFactory),
        reinterpret_cast<void**>(presentationFactory_.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return hr;
    if (!presentationFactory_->IsPresentationSupportedWithIndependentFlip())
        return DXGI_ERROR_UNSUPPORTED;
    if (FAILED(hr = presentationFactory_->CreatePresentationManager(&presentationManager_))) return hr;
    if (FAILED(hr = DCompositionCreateSurfaceHandle(COMPOSITIONOBJECT_ALL_ACCESS, nullptr, &surfaceHandle_))) return hr;
    if (FAILED(hr = presentationManager_->CreatePresentationSurface(surfaceHandle_, &presentationSurface_))) return hr;
    if (FAILED(hr = compositionDevice_->CreateSurfaceFromHandle(surfaceHandle_,
        reinterpret_cast<IUnknown**>(compositionSurface_.ReleaseAndGetAddressOf())))) return hr;
    if (FAILED(hr = presentationSurface_->SetAlphaMode(DXGI_ALPHA_MODE_PREMULTIPLIED))) return hr;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = widthPx_; description.Height = heightPx_;
    description.MipLevels = 1; description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    description.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
        D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE;
    for (auto& buffer : buffers_)
    {
        if (FAILED(hr = device_->CreateTexture2D(&description, nullptr, &buffer.texture))) return hr;
        if (FAILED(hr = presentationManager_->AddBufferFromResource(
            buffer.texture.Get(), &buffer.presentationBuffer))) return hr;
    }
    return S_OK;
}

HRESULT HudPresentation::CreateBitmapTargets()
{
    d2dContext_->SetDpi(dpi_, dpi_);
    const auto properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpi_, dpi_);
    for (auto& buffer : buffers_)
    {
        ComPtr<IDXGISurface> surface;
        HRESULT hr = buffer.texture.As(&surface);
        if (FAILED(hr)) return hr;
        if (FAILED(hr = d2dContext_->CreateBitmapFromDxgiSurface(
            surface.Get(), &properties, &buffer.bitmapTarget))) return hr;
    }
    return S_OK;
}

HRESULT HudPresentation::Render(const HudTelemetrySnapshot& snapshot, const HudRenderOptions& options)
{
    if (!initialized_ || !renderer_)
        return E_UNEXPECTED;
    auto* buffer = TryAcquireAvailableBuffer();
    if (!buffer)
        return S_FALSE;
    HudRenderOptions effective = options;
    effective.dpi = dpi_;
    const auto runs = FormatHud(snapshot);
    const float widthDip = DipFromPhysicalPixels(static_cast<float>(widthPx_), dpi_);
    const float heightDip = DipFromPhysicalPixels(static_cast<float>(heightPx_), dpi_);
    d2dContext_->SetTarget(buffer->bitmapTarget.Get());
    d2dContext_->BeginDraw();
    d2dContext_->Clear(D2D1::ColorF(0.0f, 0.0f));
    HRESULT hr = runs.empty() ? S_OK : renderer_->Draw(d2dContext_.Get(), runs, effective,
        D2D1::RectF(0, 0, widthDip, heightDip));
    const HRESULT endHr = d2dContext_->EndDraw();
    if (FAILED(hr)) return hr;
    if (FAILED(endHr)) return endHr;
    deviceContext_->Flush();
    if (FAILED(hr = presentationSurface_->SetBuffer(buffer->presentationBuffer.Get()))) return hr;
    return presentationManager_->Present();
}

HudPresentation::HudFrameBuffer* HudPresentation::TryAcquireAvailableBuffer() noexcept
{
    for (auto& buffer : buffers_)
    {
        BOOLEAN available{};
        if (SUCCEEDED(buffer.presentationBuffer->IsAvailable(&available)) && available)
            return &buffer;
    }
    return nullptr;
}

HRESULT HudPresentation::CommitVisibility(bool visible)
{
    HRESULT hr = visual_->SetContent(visible ? compositionSurface_.Get() : nullptr);
    if (FAILED(hr)) return hr;
    return compositionDevice_->Commit();
}

HRESULT HudPresentation::Show()
{
    if (!initialized_) return E_UNEXPECTED;
    if (visible_) return S_OK;
    HRESULT hr = CommitVisibility(true);
    if (FAILED(hr)) return hr;
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    visible_ = true;
    return S_OK;
}

HRESULT HudPresentation::Hide()
{
    if (!initialized_ || !visible_) return S_OK;
    HRESULT hr = CommitVisibility(false);
    if (FAILED(hr)) return hr;
    ShowWindow(window_, SW_HIDE);
    visible_ = false;
    return S_OK;
}

void HudPresentation::Shutdown() noexcept
{
    if (visible_ && visual_ && compositionDevice_)
    {
        visual_->SetContent(nullptr);
        compositionDevice_->Commit();
    }
    visible_ = false;
    renderer_.reset();
    for (auto& buffer : buffers_)
    {
        buffer.bitmapTarget.Reset();
        buffer.presentationBuffer.Reset();
        buffer.texture.Reset();
    }
    d2dContext_.Reset(); writeFactory_.Reset();
    presentationSurface_.Reset(); presentationManager_.Reset(); presentationFactory_.Reset();
    compositionSurface_.Reset(); visual_.Reset(); compositionTarget_.Reset();
    compositionDevice_.Reset(); deviceContext_.Reset(); device_.Reset();
    if (surfaceHandle_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(surfaceHandle_);
        surfaceHandle_ = INVALID_HANDLE_VALUE;
    }
    if (window_)
    {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    initialized_ = false;
}

LRESULT CALLBACK HudPresentation::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wParam, lParam);
}
}
