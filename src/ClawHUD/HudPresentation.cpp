#include "HudPresentation.h"
#include "HudPresentationLifecycle.h"
#include "RuntimeLogger.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace clawhud
{
namespace
{
constexpr wchar_t kWindowClass[] = L"ClawHUD.MockHudSurface";
constexpr COLORREF kOpacityPocClientPaintColor = RGB(255, 0, 255);

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
    opacityPocLogged_ = false;
    opacityPocBackgroundPainted_ = false;
    if (initialized_)
        return S_OK;
    if (!instance || options.barPixelHeight <= 0.0f)
        return E_INVALIDARG;

    instance_ = instance;
    initializationOptions_ = options;
    barPixelHeight_ = options.barPixelHeight;
    MONITORINFO monitor{ sizeof(monitor) };
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &monitor))
        return LastErrorResult();
    monitorRect_ = monitor.rcMonitor;
    xPx_ = monitorRect_.left;
    yPx_ = monitorRect_.top;
    widthPx_ = static_cast<UINT>(monitorRect_.right - monitorRect_.left);
    surfaceWidthPx_ = widthPx_;
    heightPx_ = 1;
    if (!widthPx_ || !heightPx_)
        return E_INVALIDARG;

    HRESULT hr = CreateWindowHost(instance);
    if (FAILED(hr)) { Shutdown(); return hr; }
    dpi_ = static_cast<float>(GetDpiForWindow(window_));
    if (dpi_ <= 0.0f) dpi_ = 96.0f;
    initializationOptions_.dpi = dpi_;
    if (FAILED(hr = CreateGraphics())) { Shutdown(); return hr; }
    float measuredTextHeightPx{};
    if (SUCCEEDED(renderer_->MeasureMainTextHeight(initializationOptions_, measuredTextHeightPx)) &&
        measuredTextHeightPx > 0.0f)
    {
        barPixelHeight_ = measuredTextHeightPx + 10.0f;
        initializationOptions_.barPixelHeight = barPixelHeight_;
    }
    else
    {
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"Unispace text metrics unavailable; using configured HUD bar height");
    }
    std::wostringstream style;
    style << L"HUD style font=Unispace private=" << (renderer_->PrivateFontLoaded() ? 1 : 0)
        << L" main=" << initializationOptions_.fontPixelSize
        << L" unit=" << initializationOptions_.unitFontPixelSize
        << L" bar=" << barPixelHeight_
        << L" opacity=" << initializationOptions_.layout.backgroundOpacity
        << L" padding=" << initializationOptions_.horizontalPaddingPx;
    RuntimeLogger::Log(RuntimeLogLevel::Info, style.str());
    heightPx_ = static_cast<UINT>(std::max(1.0f, std::ceil(barPixelHeight_)));
    if (options.layout.backgroundMode == HudBackgroundMode::ContentWidth)
    {
        // The first frame supplies the actual runs. Start with a minimal surface;
        // Render() grows it to the measured width when content is available.
        const auto geometry = CalculateHudWindowGeometry(
            monitorRect_, options.layout.backgroundMode,
            options.layout.alignment, 1);
        xPx_ = geometry.xPx;
        yPx_ = geometry.yPx;
        widthPx_ = geometry.widthPx;
    }
    if (!SetWindowPos(window_, HWND_TOPMOST, xPx_, yPx_,
        static_cast<int>(widthPx_), static_cast<int>(heightPx_),
        SWP_NOACTIVATE | SWP_NOOWNERZORDER))
    {
        Shutdown();
        return LastErrorResult();
    }
    if (FAILED(hr = CreatePresentationSurface())) { Shutdown(); return hr; }
    if (FAILED(hr = CreateBitmapTargets())) { Shutdown(); return hr; }
    displayChangePending_ = false;
    initialized_ = true;
    return S_OK;
}

HRESULT HudPresentation::CreateWindowHost(HINSTANCE instance)
{
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_CLAWHUD));
    RegisterClassW(&windowClass);
    window_ = CreateWindowExW(kHudPresentationContract.windowExStyle, kWindowClass, L"ClawHUD Mock HUD", WS_POPUP,
        xPx_, yPx_, static_cast<int>(widthPx_), static_cast<int>(heightPx_),
        nullptr, nullptr, instance, this);
    if (!window_)
        return LastErrorResult();
    if (!SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA))
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
    if (initializationOptions_.fontFilePath.empty())
    {
        wchar_t modulePath[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(instance_, modulePath, ARRAYSIZE(modulePath));
        if (length > 0 && length < ARRAYSIZE(modulePath))
            initializationOptions_.fontFilePath =
                (std::filesystem::path(modulePath).parent_path() / L"fonts" / L"Unispace.otf").wstring();
    }
    renderer_ = std::make_unique<HudRenderer>(writeFactory_.Get(),
        initializationOptions_.fontFilePath);
    return S_OK;
}

HRESULT HudPresentation::CreatePresentationSurface()
{
    HRESULT hr = CreatePresentationFactory(device_.Get(), __uuidof(IPresentationFactory),
        reinterpret_cast<void**>(presentationFactory_.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return hr;
    if (kHudPresentationContract.independentFlipRequired &&
        !presentationFactory_->IsPresentationSupportedWithIndependentFlip())
        return DXGI_ERROR_UNSUPPORTED;
    if (FAILED(hr = presentationFactory_->CreatePresentationManager(&presentationManager_))) return hr;
    if (FAILED(hr = DCompositionCreateSurfaceHandle(COMPOSITIONOBJECT_ALL_ACCESS, nullptr, &surfaceHandle_))) return hr;
    if (FAILED(hr = presentationManager_->CreatePresentationSurface(surfaceHandle_, &presentationSurface_))) return hr;
    if (FAILED(hr = compositionDevice_->CreateSurfaceFromHandle(surfaceHandle_,
        reinterpret_cast<IUnknown**>(compositionSurface_.ReleaseAndGetAddressOf())))) return hr;
    if (FAILED(hr = presentationSurface_->SetAlphaMode(kHudPresentationContract.alphaMode))) return hr;
    RECT sourceRect{ 0, 0, static_cast<LONG>(widthPx_), static_cast<LONG>(heightPx_) };
    if (FAILED(hr = presentationSurface_->SetSourceRect(&sourceRect))) return hr;
    PresentationTransform transform{};
    if (kHudPresentationContract.identityTransform)
    {
        transform.M11 = 1.0f;
        transform.M22 = 1.0f;
    }
    if (FAILED(hr = presentationSurface_->SetTransform(&transform))) return hr;
    if (FAILED(hr = presentationSurface_->SetLetterboxingMargins(
        kHudPresentationContract.letterboxLeft,
        kHudPresentationContract.letterboxTop,
        kHudPresentationContract.letterboxRight,
        kHudPresentationContract.letterboxBottom))) return hr;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = surfaceWidthPx_; description.Height = heightPx_;
    description.MipLevels = 1; description.ArraySize = 1;
    description.Format = kHudPresentationContract.textureFormat;
    description.SampleDesc.Count = kHudPresentationContract.sampleCount;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    description.MiscFlags = kHudPresentationContract.resourceMiscFlags;
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
        D2D1::PixelFormat(kHudPresentationContract.textureFormat, D2D1_ALPHA_MODE_PREMULTIPLIED),
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
    HRESULT hr = RefreshDisplayIfNeeded();
    if (FAILED(hr))
        return hr;
    const HudRenderOptions effective = BuildEffectiveHudRenderOptions(
        options, initializationOptions_, dpi_);
    const auto runs = FormatHud(snapshot);
    if (effective.layout.backgroundMode == HudBackgroundMode::ContentWidth)
    {
        HudMeasureResult measure{};
        hr = renderer_->Measure(runs, effective, measure);
        if (FAILED(hr)) return hr;
        const UINT requiredWidthPx = CalculateHudContentWidthPixels(
            measure.contentWidth, dpi_,
            static_cast<UINT>(std::max<LONG>(0, monitorRect_.right - monitorRect_.left)));
        const auto geometry = CalculateHudWindowGeometry(
            monitorRect_, HudBackgroundMode::ContentWidth,
            effective.layout.alignment, requiredWidthPx);
        if (geometry.widthPx != widthPx_ || geometry.xPx != xPx_)
        {
            hr = ResizeContentWidth(geometry.widthPx, effective.layout.alignment);
            if (FAILED(hr)) return hr;
        }
    }
    HudFrameBuffer* buffer{};
    hr = TryAcquireAvailableBuffer(buffer);
    if (FAILED(hr) || hr == S_FALSE)
        return hr;
    const float widthDip = DipFromPhysicalPixels(static_cast<float>(widthPx_), dpi_);
    const float heightDip = DipFromPhysicalPixels(static_cast<float>(heightPx_), dpi_);
    d2dContext_->SetTarget(buffer->bitmapTarget.Get());
    d2dContext_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    d2dContext_->BeginDraw();
    d2dContext_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    hr = runs.empty() ? S_OK : renderer_->Draw(d2dContext_.Get(), runs, effective,
        D2D1::RectF(0, 0, widthDip, heightDip));
    const HRESULT endHr = d2dContext_->EndDraw();
    if (FAILED(hr)) return hr;
    if (FAILED(endHr)) return endHr;
    deviceContext_->Flush();
    const BYTE expectedBackgroundAlpha = static_cast<BYTE>(std::lround(
        std::clamp(effective.layout.backgroundOpacity, 0.0f, 1.0f) * 255.0f));
    const bool diagnoseFrame = !runs.empty() && !opacityPocLogged_;
    std::optional<std::array<BYTE, 4>> sample;
    UINT sampleX{};
    UINT sampleY{};
    HRESULT readbackHr = S_OK;
    if (diagnoseFrame)
    {
        sampleX = std::min<UINT>(2, widthPx_ - 1);
        sampleY = std::min<UINT>(2, heightPx_ - 1);
        std::array<BYTE, 4> pixel{};
        readbackHr = ValidatePresentedAlpha(buffer->texture.Get(), sampleX, sampleY,
            pixel[0], pixel[1], pixel[2], pixel[3]);
        if (SUCCEEDED(readbackHr))
            sample = pixel;
        opacityPocLogged_ = true;
    }

    const HRESULT setBufferHr = presentationSurface_->SetBuffer(
        buffer->presentationBuffer.Get());
    HRESULT presentHr = S_OK;
    if (SUCCEEDED(setBufferHr))
        presentHr = presentationManager_->Present();
    if (diagnoseFrame)
    {
        std::wostringstream message;
        message << L"HUD opacity POC: requestedOpacity=" << std::fixed
            << std::setprecision(3) << effective.layout.backgroundOpacity
            << L" expectedAlpha=" << static_cast<unsigned>(expectedBackgroundAlpha)
            << L" sampleX=" << sampleX << L" sampleY=" << sampleY;
        if (sample)
        {
            message << L" B=" << static_cast<unsigned>((*sample)[0])
                << L" G=" << static_cast<unsigned>((*sample)[1])
                << L" R=" << static_cast<unsigned>((*sample)[2])
                << L" A=" << static_cast<unsigned>((*sample)[3]);
        }
        else
        {
            message << L" BGRA=<readback-failed hr=0x" << std::hex << std::setw(8)
                << std::setfill(L'0') << static_cast<unsigned long>(readbackHr)
                << std::setfill(L' ') << std::dec << L">";
        }
        message << L" textureFormat=DXGI_FORMAT_B8G8R8A8_UNORM"
            << L" alphaMode=PREMULTIPLIED bufferAlphaResult="
            << (sample && std::abs(static_cast<int>((*sample)[3]) -
                static_cast<int>(expectedBackgroundAlpha)) <= 2 ? L"PASS" : L"FAIL")
            << L" SetBuffer=0x" << std::hex << std::setw(8) << std::setfill(L'0')
            << static_cast<unsigned long>(setBufferHr) << std::setfill(L' ')
            << L" Present=0x" << std::setw(8) << static_cast<unsigned long>(presentHr);
        RuntimeLogger::Log(SUCCEEDED(setBufferHr) && SUCCEEDED(presentHr)
            ? RuntimeLogLevel::Info : RuntimeLogLevel::Warn, message.str());
    }
    if (FAILED(setBufferHr)) return setBufferHr;
    return presentHr;
}

HRESULT HudPresentation::ValidatePresentedAlpha(
    ID3D11Texture2D* texture, UINT sampleX, UINT sampleY,
    BYTE& blue, BYTE& green, BYTE& red, BYTE& alpha)
{
    if (!texture || !device_ || !deviceContext_)
        return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC source{};
    texture->GetDesc(&source);
    if (sampleX >= source.Width || sampleY >= source.Height ||
        source.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
        return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC staging = source;
    staging.Width = 1;
    staging.Height = 1;
    staging.Usage = D3D11_USAGE_STAGING;
    staging.BindFlags = 0;
    staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> readback;
    HRESULT hr = device_->CreateTexture2D(&staging, nullptr, &readback);
    if (FAILED(hr))
        return hr;
    const D3D11_BOX box = HudAlphaSampleSourceBox(sampleX, sampleY);
    deviceContext_->CopySubresourceRegion(
        readback.Get(), 0, 0, 0, 0, texture, 0, &box);
    deviceContext_->Flush();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = deviceContext_->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
        return hr;
    const auto* pixel = static_cast<const BYTE*>(mapped.pData);
    blue = pixel[0];
    green = pixel[1];
    red = pixel[2];
    alpha = pixel[3];
    deviceContext_->Unmap(readback.Get(), 0);
    return S_OK;
}

HRESULT HudPresentation::ResizeContentWidth(UINT widthPx, HudAlignment alignment)
{
    const auto geometry = CalculateHudWindowGeometry(
        monitorRect_, HudBackgroundMode::ContentWidth, alignment, widthPx);
    if (geometry.widthPx == widthPx_)
    {
        if (!SetWindowPos(window_, HWND_TOPMOST, geometry.xPx, geometry.yPx,
            static_cast<int>(widthPx_), static_cast<int>(heightPx_),
            SWP_NOACTIVATE | SWP_NOOWNERZORDER))
            return LastErrorResult();
        xPx_ = geometry.xPx;
        yPx_ = geometry.yPx;
        return S_OK;
    }

    const int oldX = xPx_;
    const int oldY = yPx_;
    const UINT oldWidth = widthPx_;
    if (!SetWindowPos(window_, HWND_TOPMOST, geometry.xPx, geometry.yPx,
        static_cast<int>(geometry.widthPx), static_cast<int>(heightPx_),
        SWP_NOACTIVATE | SWP_NOOWNERZORDER))
        return LastErrorResult();
    RECT sourceRect{ 0, 0, static_cast<LONG>(geometry.widthPx),
        static_cast<LONG>(heightPx_) };
    const HRESULT hr = presentationSurface_->SetSourceRect(&sourceRect);
    if (FAILED(hr))
    {
        SetWindowPos(window_, HWND_TOPMOST, oldX, oldY,
            static_cast<int>(oldWidth), static_cast<int>(heightPx_),
            SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        return hr;
    }
    widthPx_ = geometry.widthPx;
    xPx_ = geometry.xPx;
    yPx_ = geometry.yPx;
    opacityPocBackgroundPainted_ = false;
    InvalidateRect(window_, nullptr, FALSE);
    UpdateWindow(window_);
    return S_OK;
}

HRESULT HudPresentation::RefreshDisplayIfNeeded()
{
    const auto refreshPlan = BuildHudPresentationRefreshPlan(displayChangePending_, visible_);
    if (!refreshPlan.recreate)
        return S_OK;
    displayChangePending_ = false;
    const HINSTANCE instance = instance_;
    Shutdown();
    HRESULT hr = Initialize(instance, initializationOptions_);
    if (FAILED(hr) || !refreshPlan.restoreVisibility)
        return hr;
    hr = CommitVisibility(true);
    if (FAILED(hr))
        return hr;
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    visible_ = true;
    return S_OK;
}

HRESULT HudPresentation::TryAcquireAvailableBuffer(HudFrameBuffer*& selected) noexcept
{
    selected = nullptr;
    for (auto& buffer : buffers_)
    {
        BOOLEAN available{};
        const HRESULT hr = buffer.presentationBuffer->IsAvailable(&available);
        if (FAILED(hr))
            return hr;
        if (available)
        {
            selected = &buffer;
            return S_OK;
        }
    }
    return S_FALSE;
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
    HRESULT hr = RefreshDisplayIfNeeded();
    if (FAILED(hr)) return hr;
    if (visible_) return S_OK;
    hr = CommitVisibility(true);
    if (FAILED(hr)) return hr;
    if (!SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW))
        return LastErrorResult();
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    InvalidateRect(window_, nullptr, FALSE);
    UpdateWindow(window_);
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
    displayChangePending_ = false;
}

LRESULT CALLBACK HudPresentation::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<HudPresentation*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_DISPLAYCHANGE || message == WM_DPICHANGED)
    {
        if (self) self->displayChangePending_ = true;
        return 0;
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        if (self && !self->opacityPocBackgroundPainted_)
        {
            RECT client{};
            GetClientRect(window, &client);
            HBRUSH brush = CreateSolidBrush(kOpacityPocClientPaintColor);
            bool fillSucceeded = false;
            if (brush)
            {
                fillSucceeded = FillRect(dc, &client, brush) != FALSE;
                DeleteObject(brush);
            }
            RuntimeLogger::Log(fillSucceeded ? RuntimeLogLevel::Info : RuntimeLogLevel::Warn,
                fillSucceeded
                    ? L"HUD opacity POC: HWND magenta paint executed FillRect=SUCCESS"
                    : L"HUD opacity POC: HWND magenta paint executed FillRect=FAILURE");
            self->opacityPocBackgroundPainted_ = true;
        }
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wParam, lParam);
}
}
