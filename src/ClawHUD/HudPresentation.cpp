#include "HudPresentation.h"
#include "HudPresentationLifecycle.h"
#include "RuntimeLogger.h"
#include "Win32Format.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace clawhud
{
namespace
{
constexpr wchar_t kWindowClass[] = L"ClawHUD.MockHudSurface";
constexpr UINT kHudPresentationStatsReadyMessage = WM_APP + 1;
constexpr UINT_PTR kHudPresentationStatsTag = 1;
constexpr unsigned kMaxStatsPerDrain = 4096;

HRESULT LastErrorResult() noexcept
{
    return HRESULT_FROM_WIN32(GetLastError());
}
}

HudPresentation::~HudPresentation()
{
    Shutdown();
}

HRESULT HudPresentation::Initialize(HINSTANCE instance, const HudRenderOptions& options,
    float opacityPercent, bool enablePresentStatisticsDiagnostics)
{
#ifdef _DEBUG
    debugLastValidatedAlpha_ = -1;
#endif
    if (initialized_)
        return S_OK;
    if (!instance || options.barPixelHeight <= 0.0f)
        return E_INVALIDARG;

    instance_ = instance;
    presentStatisticsDiagnosticsEnabled_ = enablePresentStatisticsDiagnostics;
    initializationOptions_ = options;
    opacityPercent_ = opacityPercent;
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

    HRESULT hr = CreateWindowHost(instance, opacityPercent);
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
    RuntimeLogger::Log(RuntimeLogLevel::Debug, style.str());
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
    ++presentationEpoch_;
    if (presentStatisticsDiagnosticsEnabled_)
        InitializePresentStatisticsDiagnostics();
    diagnosticState_.Reset();
    if (!initializationLogged_)
    {
        std::wostringstream message;
        message << L"HUD presentation initialized backend=PresentationAPI independentFlip="
            << (kHudPresentationContract.independentFlipRequired ? L"required" : L"optional")
            << L" alpha=premultiplied surface=" << surfaceWidthPx_ << L"x" << heightPx_
            << L" dpi=" << dpi_;
        RuntimeLogger::Log(RuntimeLogLevel::Info, message.str());
        initializationLogged_ = true;
    }
    LogPresentationState(L"initialized");
    return S_OK;
}

HRESULT HudPresentation::CreateWindowHost(HINSTANCE instance, float opacityPercent)
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
    const HRESULT opacityHr = SetHudOpacity(opacityPercent);
    if (FAILED(opacityHr))
        return opacityHr;
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
    UINT availableMask{};
    hr = TryAcquireAvailableBuffer(buffer, availableMask);
    if (hr == S_FALSE)
    {
        if (diagnosticState_.RecordNoBuffer(GetTickCount64()))
            LogPresentationState(L"no-buffer-enter", availableMask);
        return hr;
    }
    if (FAILED(hr))
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
#ifdef _DEBUG
    const BYTE expectedBackgroundAlpha = static_cast<BYTE>(std::lround(
        std::clamp(effective.layout.backgroundOpacity, 0.0f, 1.0f) * 255.0f));
    const bool canonicalOpacity = expectedBackgroundAlpha == 0 ||
        expectedBackgroundAlpha == 255 ||
        std::abs(static_cast<int>(expectedBackgroundAlpha) - 128) <= 2;
    if (!runs.empty() && canonicalOpacity &&
        debugLastValidatedAlpha_ != expectedBackgroundAlpha)
    {
        const UINT sampleX = std::min<UINT>(2, widthPx_ - 1);
        const UINT sampleY = std::min<UINT>(2, heightPx_ - 1);
        if (FAILED(ValidatePresentedAlpha(buffer->texture.Get(), sampleX, sampleY,
            expectedBackgroundAlpha)))
            RuntimeLogger::Log(RuntimeLogLevel::Warn,
                L"Production buffer alpha validation failed");
        debugLastValidatedAlpha_ = expectedBackgroundAlpha;
    }
#endif
    hr = presentationSurface_->SetBuffer(buffer->presentationBuffer.Get());
    if (FAILED(hr))
    {
        RecordSubmissionFailure(HudPresentationSubmissionStage::SetBuffer, hr, availableMask);
        return hr;
    }
    hr = presentationManager_->Present();
    if (hr == S_OK)
    {
        const auto recovery = diagnosticState_.RecordSuccessfulPresent(GetTickCount64());
        if (recovery.noBufferRecovered)
            LogPresentationState(L"no-buffer-recovered", availableMask,
                HudPresentationSubmissionStage::Present, S_OK, &recovery);
        if (recovery.submissionRecovered)
            LogPresentationState(L"submit-recovered", availableMask,
                HudPresentationSubmissionStage::Present, S_OK, &recovery);
        if (recovery.heartbeat)
            LogPresentationState(L"present-heartbeat", availableMask);
    }
    else if (FAILED(hr))
    {
        RecordSubmissionFailure(HudPresentationSubmissionStage::Present, hr, availableMask);
    }
    return hr;
}

#ifdef _DEBUG
HRESULT HudPresentation::ValidatePresentedAlpha(
    ID3D11Texture2D* texture, UINT sampleX, UINT sampleY, BYTE expectedAlpha)
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
    const BYTE actualAlpha = pixel[3];
    deviceContext_->Unmap(readback.Get(), 0);
    if (std::abs(static_cast<int>(actualAlpha) - static_cast<int>(expectedAlpha)) > 2)
    {
        std::wostringstream message;
        message << L"Production buffer alpha mismatch expected="
            << static_cast<unsigned>(expectedAlpha) << L" actual="
            << static_cast<unsigned>(actualAlpha);
        RuntimeLogger::Log(RuntimeLogLevel::Warn, message.str());
        return E_FAIL;
    }
    return S_OK;
}
#endif

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
    return S_OK;
}

HRESULT HudPresentation::RefreshDisplayIfNeeded()
{
    const auto refreshPlan = BuildHudPresentationRefreshPlan(
        displayChangePending_, visible_, presentStatisticsDiagnosticsEnabled_);
    if (!refreshPlan.recreate)
        return S_OK;
    displayChangePending_ = false;
    const HINSTANCE instance = instance_;
    Shutdown();
    HRESULT hr = Initialize(instance, initializationOptions_, opacityPercent_,
        refreshPlan.enablePresentStatisticsDiagnostics);
    if (FAILED(hr) || !refreshPlan.restoreVisibility)
        return hr;
    hr = CommitVisibility(true);
    if (FAILED(hr))
        return hr;
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    visible_ = true;
    LogDebugWindowState(L"display-refresh-recreate");
    return S_OK;
}

HRESULT HudPresentation::TryAcquireAvailableBuffer(HudFrameBuffer*& selected,
    UINT& availableMask) noexcept
{
    selected = nullptr;
    availableMask = 0;
    for (UINT index = 0; index < buffers_.size(); ++index)
    {
        auto& buffer = buffers_[index];
        BOOLEAN available{};
        const HRESULT hr = buffer.presentationBuffer->IsAvailable(&available);
        if (FAILED(hr))
            return hr;
        if (available)
        {
            availableMask |= (1u << index);
            selected = &buffer;
            return S_OK;
        }
    }
    return S_FALSE;
}

void HudPresentation::RecordSubmissionFailure(HudPresentationSubmissionStage stage,
    HRESULT hr, UINT availableMask) noexcept
{
    if (diagnosticState_.RecordSubmissionFailure(stage, hr, GetTickCount64()))
        LogPresentationState(L"submit-failed", availableMask, stage, hr);
}

void HudPresentation::LogPresentationState(std::wstring_view reason, UINT availableMask,
    HudPresentationSubmissionStage stage, HRESULT hr,
    const HudPresentationDiagnosticRecovery* recovery) const noexcept
{
    try
    {
        const auto stageName = [](HudPresentationSubmissionStage value)
        {
            return value == HudPresentationSubmissionStage::SetBuffer
                ? L"set-buffer" : L"present";
        };
        std::wostringstream message;
        message << L"[HudPresentationState] reason=" << reason
            << L" epoch=" << presentationEpoch_
            << L" hwnd=0x" << std::hex << reinterpret_cast<std::uintptr_t>(window_) << std::dec
            << L" surface=" << surfaceWidthPx_ << L"x" << heightPx_
            << L" bufferCount=" << buffers_.size()
            << L" visible=" << (visible_ ? 1 : 0)
            << L" availableMask=0x" << std::hex << availableMask << std::dec
            << L" successfulPresentCount=" << diagnosticState_.SuccessfulPresentCount()
            << L" lastSuccessfulPresentTickMs=" << diagnosticState_.LastSuccessfulPresentTickMs()
            << L" noBufferActive=" << (diagnosticState_.NoBufferActive() ? 1 : 0)
            << L" consecutiveNoBuffer=" << diagnosticState_.ConsecutiveNoBufferCount()
            << L" submissionFailureActive="
                << (diagnosticState_.SubmissionFailureActive() ? 1 : 0)
            << L" failureCount=" << diagnosticState_.SubmissionFailureCount();
        if (reason == L"no-buffer-enter")
            message << L" consecutiveNoBuffer=" << diagnosticState_.ConsecutiveNoBufferCount();
        if (reason == L"submit-failed")
            message << L" stage=" << stageName(stage) << L" hr=" << HexHresult(hr)
                << L" failureCount=" << diagnosticState_.SubmissionFailureCount();
        if (recovery && recovery->noBufferRecovered)
            message << L" durationMs=" << recovery->noBufferDurationMs
                << L" consecutiveNoBuffer=" << recovery->noBufferCount;
        if (recovery && recovery->submissionRecovered)
            message << L" previousStage=" << stageName(recovery->previousFailureStage)
                << L" previousHr=" << HexHresult(recovery->previousFailureHr)
                << L" durationMs=" << recovery->submissionDurationMs
                << L" failureCount=" << recovery->submissionFailureCount;
        RuntimeLogger::Log(RuntimeLogLevel::Debug, message.str());
    }
    catch (...)
    {
    }
}

void HudPresentation::LogVisibilityMarkerDiagnostic(
    std::uint64_t sequence, bool coveredMarker) const noexcept
{
    try
    {
        const auto hex = [](HWND value)
        {
            std::wostringstream stream;
            stream << L"0x" << std::hex
                << reinterpret_cast<std::uintptr_t>(value) << std::dec;
            return stream.str();
        };
        const HWND hud = window_;
        const bool isWindow = hud && IsWindow(hud);
        const bool isWindowVisible = isWindow && IsWindowVisible(hud);
        const bool isIconic = isWindow && IsIconic(hud);
        const LONG_PTR exStyle = isWindow ? GetWindowLongPtrW(hud, GWL_EXSTYLE) : 0;
        RECT hudRect{};
        if (!isWindow || !GetWindowRect(hud, &hudRect))
            hudRect = RECT{};

        const HWND foreground = GetForegroundWindow();
        DWORD foregroundPid = 0;
        if (foreground)
            GetWindowThreadProcessId(foreground, &foregroundPid);
        const bool foregroundIsWindow = foreground && IsWindow(foreground);
        const bool foregroundVisible = foregroundIsWindow && IsWindowVisible(foreground);
        const bool foregroundIconic = foregroundIsWindow && IsIconic(foreground);
        const LONG_PTR foregroundExStyle = foregroundIsWindow
            ? GetWindowLongPtrW(foreground, GWL_EXSTYLE) : 0;
        RECT foregroundRect{};
        if (!foregroundIsWindow || !GetWindowRect(foreground, &foregroundRect))
            foregroundRect = RECT{};
        wchar_t foregroundClass[256]{};
        if (foregroundIsWindow)
            GetClassNameW(foreground, foregroundClass, ARRAYSIZE(foregroundClass));

        const HWND zPrev = isWindow ? GetWindow(hud, GW_HWNDPREV) : nullptr;
        const HWND zNext = isWindow ? GetWindow(hud, GW_HWNDNEXT) : nullptr;
        DWORD zPrevPid = 0;
        DWORD zNextPid = 0;
        if (zPrev) GetWindowThreadProcessId(zPrev, &zPrevPid);
        if (zNext) GetWindowThreadProcessId(zNext, &zNextPid);

        const auto markTick = GetTickCount64();
        const auto lastPresentTick = diagnosticState_.LastSuccessfulPresentTickMs();
        std::wostringstream message;
        message << L"[HudVisibilityMark] seq=" << sequence
            << L" pair=" << ((sequence + 1) / 2)
            << L" marker=" << (coveredMarker ? L"covered" : L"restored")
            << L" markTickMs=" << markTick
            << L" epoch=" << presentationEpoch_
            << L" successfulPresentCount=" << diagnosticState_.SuccessfulPresentCount()
            << L" lastSuccessfulPresentTickMs=" << lastPresentTick
            << L" msSinceLastSuccessfulPresent="
                << (lastPresentTick && markTick >= lastPresentTick
                    ? markTick - lastPresentTick : 0)
            << L" noBufferActive=" << (diagnosticState_.NoBufferActive() ? 1 : 0)
            << L" consecutiveNoBuffer=" << diagnosticState_.ConsecutiveNoBufferCount()
            << L" submissionFailureActive="
                << (diagnosticState_.SubmissionFailureActive() ? 1 : 0)
            << L" failureCount=" << diagnosticState_.SubmissionFailureCount()
            << L" hwnd=" << hex(hud)
            << L" initialized=" << (initialized_ ? 1 : 0)
            << L" logicalVisible=" << (visible_ ? 1 : 0)
            << L" isWindow=" << (isWindow ? 1 : 0)
            << L" isWindowVisible=" << (isWindowVisible ? 1 : 0)
            << L" isIconic=" << (isIconic ? 1 : 0)
            << L" exStyle=0x" << std::hex << static_cast<std::uintptr_t>(exStyle)
            << std::dec << L" exTopmost=" << ((exStyle & WS_EX_TOPMOST) ? 1 : 0)
            << L" rect=" << hudRect.left << L"," << hudRect.top << L"," << hudRect.right
            << L"," << hudRect.bottom
            << L" foregroundHwnd=" << hex(foreground)
            << L" foregroundPid=" << foregroundPid
            << L" foregroundVisible=" << (foregroundVisible ? 1 : 0)
            << L" foregroundIconic=" << (foregroundIconic ? 1 : 0)
            << L" foregroundClass=\"" << foregroundClass << L"\""
            << L" foregroundRect=" << foregroundRect.left << L"," << foregroundRect.top
            << L"," << foregroundRect.right << L"," << foregroundRect.bottom
            << L" foregroundExStyle=0x" << std::hex
            << static_cast<std::uintptr_t>(foregroundExStyle) << std::dec
            << L" zPrevHwnd=" << hex(zPrev) << L" zPrevPid=" << zPrevPid
            << L" zNextHwnd=" << hex(zNext) << L" zNextPid=" << zNextPid;
        RuntimeLogger::Log(RuntimeLogLevel::Debug, message.str());
    }
    catch (...)
    {
        // Marker diagnostics must never affect HUD behavior.
    }
}

HRESULT HudPresentation::CommitVisibility(bool visible)
{
    HRESULT hr = visual_->SetContent(visible ? compositionSurface_.Get() : nullptr);
    if (FAILED(hr)) return hr;
    return compositionDevice_->Commit();
}

void CALLBACK HudPresentation::PresentStatisticsWaitCallback(
    PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT)
{
    auto* self = static_cast<HudPresentation*>(context);
    if (!self || !self->presentStatisticsDiagnosticsActive_.load())
        return;
    if (self->presentStatisticsMessagePending_.exchange(true))
        return;
    if (!self->window_ || !PostMessageW(self->window_,
        kHudPresentationStatsReadyMessage, 0, 0))
    {
        self->presentStatisticsMessagePending_.store(false);
    }
}

HRESULT HudPresentation::InitializePresentStatisticsDiagnostics() noexcept
{
    if (!presentStatisticsDiagnosticsEnabled_ || !presentationManager_ ||
        !presentationSurface_)
        return S_FALSE;

    const auto fail = [this](std::wstring_view stage, HRESULT hr)
    {
        LogPresentStatisticsFailure(L"init-failed", stage, hr);
        ShutdownPresentStatisticsDiagnostics();
        return hr;
    };

    presentationSurface_->SetTag(kHudPresentationStatsTag);
    HRESULT hr = presentationManager_->EnablePresentStatisticsKind(
        PresentStatisticsKind_PresentStatus, TRUE);
    if (FAILED(hr)) return fail(L"enable-present-status", hr);
    hr = presentationManager_->EnablePresentStatisticsKind(
        PresentStatisticsKind_CompositionFrame, TRUE);
    if (FAILED(hr)) return fail(L"enable-composition-frame", hr);
    hr = presentationManager_->EnablePresentStatisticsKind(
        PresentStatisticsKind_IndependentFlipFrame, TRUE);
    if (FAILED(hr)) return fail(L"enable-independent-flip-frame", hr);
    hr = presentationManager_->GetPresentStatisticsAvailableEvent(
        &presentStatisticsAvailableEvent_);
    if (FAILED(hr)) return fail(L"get-available-event", hr);

    presentStatisticsWait_ = CreateThreadpoolWait(
        &HudPresentation::PresentStatisticsWaitCallback, this, nullptr);
    if (!presentStatisticsWait_)
        return fail(L"create-threadpool-wait", HRESULT_FROM_WIN32(GetLastError()));

    presentStatisticsDiagnosticsActive_.store(true);
    presentStatisticsMessagePending_.store(false);
    presentStatisticsLastSummaryTickMs_ = GetTickCount64();
    lastDisplayStatisticsKind_.reset();
    lastCompositionInstanceKind_.reset();
    lastCompositionCrossAdapterCopy_.reset();
    lastCompositionDisplayUniqueId_.reset();
    lastCompositionDisplayAdapterLuid_.reset();
    lastCompositionDisplayVidPnSourceId_.reset();
    independentFlipObserved_ = false;
    ArmPresentStatisticsWait();
    std::wostringstream message;
    message << L"[HudPresentStats] reason=initialized epoch=" << presentationEpoch_
        << L" contentTag=" << kHudPresentationStatsTag;
    RuntimeLogger::Log(RuntimeLogLevel::Debug, message.str());
    return S_OK;
}

void HudPresentation::ShutdownPresentStatisticsDiagnostics() noexcept
{
    const bool wasActive = presentStatisticsDiagnosticsActive_.exchange(false);
    presentStatisticsMessagePending_.store(false);
    if (presentStatisticsWait_)
    {
        SetThreadpoolWait(presentStatisticsWait_, nullptr, nullptr);
        WaitForThreadpoolWaitCallbacks(presentStatisticsWait_, TRUE);
        CloseThreadpoolWait(presentStatisticsWait_);
        presentStatisticsWait_ = nullptr;
    }
    DisablePresentStatisticsKinds();
    if (presentStatisticsAvailableEvent_)
    {
        CloseHandle(presentStatisticsAvailableEvent_);
        presentStatisticsAvailableEvent_ = nullptr;
    }
    if (wasActive)
    {
        presentStatisticsLastSummaryTickMs_ = 0;
        lastDisplayStatisticsKind_.reset();
        lastCompositionInstanceKind_.reset();
        lastCompositionCrossAdapterCopy_.reset();
        lastCompositionDisplayUniqueId_.reset();
        lastCompositionDisplayAdapterLuid_.reset();
        lastCompositionDisplayVidPnSourceId_.reset();
        presentStatusQueuedCount_ = 0;
        presentStatusSkippedCount_ = 0;
        presentStatusCanceledCount_ = 0;
        composedOnScreenCount_ = 0;
        scanoutOnScreenCount_ = 0;
        composedToIntermediateCount_ = 0;
        independentFlipCount_ = 0;
        lastPresentStatisticsPresentId_ = 0;
        lastIndependentFlipDisplayedTime_ = 0;
        lastIndependentFlipPresentDuration_ = 0;
        lastIndependentFlipOutputAdapterLuid_ = {};
        lastIndependentFlipOutputVidPnSourceId_ = 0;
        independentFlipObserved_ = false;
    }
}

void HudPresentation::DisablePresentStatisticsKinds() noexcept
{
    if (!presentationManager_)
        return;

    // Best-effort cleanup. Diagnostics must never make HUD shutdown/fallback fail.
    presentationManager_->EnablePresentStatisticsKind(
        PresentStatisticsKind_IndependentFlipFrame, FALSE);
    presentationManager_->EnablePresentStatisticsKind(
        PresentStatisticsKind_CompositionFrame, FALSE);
    presentationManager_->EnablePresentStatisticsKind(
        PresentStatisticsKind_PresentStatus, FALSE);
}

void HudPresentation::ArmPresentStatisticsWait() noexcept
{
    if (presentStatisticsDiagnosticsActive_.load() && presentStatisticsWait_ &&
        presentStatisticsAvailableEvent_)
        SetThreadpoolWait(presentStatisticsWait_, presentStatisticsAvailableEvent_, nullptr);
}

void HudPresentation::DrainPresentStatistics() noexcept
{
    if (!presentStatisticsDiagnosticsActive_.load() || !presentationManager_)
        return;
    if (presentStatisticsWait_)
        SetThreadpoolWait(presentStatisticsWait_, nullptr, nullptr);

    unsigned drained = 0;
    for (;;)
    {
        ComPtr<IPresentStatistics> statistics;
        const HRESULT hr = presentationManager_->GetNextPresentStatistics(&statistics);
        if (FAILED(hr))
        {
            LogPresentStatisticsFailure(L"read-failed", L"drain", hr);
            break;
        }
        if (!statistics)
            break;
        ProcessPresentStatistics(statistics.Get());
        if (++drained >= kMaxStatsPerDrain)
        {
            RuntimeLogger::Log(RuntimeLogLevel::Warn,
                L"[HudPresentStats] reason=drain-truncated maxStats=4096");
            break;
        }
    }

    const auto observedTickMs = GetTickCount64();
    if (presentStatisticsLastSummaryTickMs_ == 0 ||
        observedTickMs - presentStatisticsLastSummaryTickMs_ >= 5000)
        LogPresentStatisticsSummary(observedTickMs);
    ArmPresentStatisticsWait();
}

void HudPresentation::ProcessPresentStatistics(IPresentStatistics* statistics) noexcept
{
    if (!statistics)
        return;
    try
    {
        const UINT64 presentId = statistics->GetPresentId();
        lastPresentStatisticsPresentId_ = presentId;
        const auto kind = statistics->GetKind();
        const auto logBase = [this, presentId](std::wostringstream& message,
            std::wstring_view kindName)
        {
            message << L"[HudPresentStats] epoch=" << presentationEpoch_
                << L" presentId=" << presentId << L" kind=" << kindName
                << L" observedTickMs=" << GetTickCount64();
        };
        if (kind == PresentStatisticsKind_PresentStatus)
        {
            ComPtr<IPresentStatusPresentStatistics> presentStatus;
            if (FAILED(statistics->QueryInterface(IID_PPV_ARGS(&presentStatus))))
            {
                LogPresentStatisticsFailure(
                    L"read-failed", L"query-present-status", E_NOINTERFACE);
                return;
            }
            const auto status = presentStatus->GetPresentStatus();
            switch (status)
            {
            case PresentStatus_Queued:
                ++presentStatusQueuedCount_;
                break;
            case PresentStatus_Skipped:
                ++presentStatusSkippedCount_;
                break;
            case PresentStatus_Canceled:
                ++presentStatusCanceledCount_;
                break;
            default:
                break;
            }
            if (status != PresentStatus_Queued)
            {
                std::wostringstream message;
                logBase(message, L"present-status");
                message << L" status=" << (status == PresentStatus_Skipped
                    ? L"skipped" : status == PresentStatus_Canceled ? L"canceled" : L"unknown")
                    << L" compositionFrameId=" << presentStatus->GetCompositionFrameId();
                RuntimeLogger::Log(RuntimeLogLevel::Debug, message.str());
            }
            return;
        }
        if (kind == PresentStatisticsKind_CompositionFrame)
        {
            ComPtr<ICompositionFramePresentStatistics> composition;
            if (FAILED(statistics->QueryInterface(IID_PPV_ARGS(&composition))))
            {
                LogPresentStatisticsFailure(
                    L"read-failed", L"query-composition-frame", E_NOINTERFACE);
                return;
            }
            UINT instanceCount = 0;
            const CompositionFrameDisplayInstance* instances = nullptr;
            composition->GetDisplayInstanceArray(&instanceCount, &instances);
            const bool enteredComposition = !lastDisplayStatisticsKind_.has_value() ||
                lastDisplayStatisticsKind_.value() != PresentStatisticsKind_CompositionFrame;
            for (UINT index = 0; index < instanceCount && instances; ++index)
            {
                const auto& instance = instances[index];
                switch (instance.instanceKind)
                {
                case CompositionFrameInstanceKind_ComposedOnScreen:
                    ++composedOnScreenCount_;
                    break;
                case CompositionFrameInstanceKind_ScanoutOnScreen:
                    ++scanoutOnScreenCount_;
                    break;
                case CompositionFrameInstanceKind_ComposedToIntermediate:
                    ++composedToIntermediateCount_;
                    break;
                default:
                    break;
                }
                const bool kindChanged = !lastCompositionInstanceKind_.has_value() ||
                    lastCompositionInstanceKind_.value() != instance.instanceKind;
                const bool crossAdapterChanged =
                    !lastCompositionCrossAdapterCopy_.has_value() ||
                    lastCompositionCrossAdapterCopy_.value() !=
                        !!instance.requiredCrossAdapterCopy;
                const bool outputChanged =
                    !lastCompositionDisplayUniqueId_.has_value() ||
                    lastCompositionDisplayUniqueId_.value() != instance.displayUniqueId ||
                    !lastCompositionDisplayAdapterLuid_.has_value() ||
                    lastCompositionDisplayAdapterLuid_->HighPart !=
                        instance.displayAdapterLUID.HighPart ||
                    lastCompositionDisplayAdapterLuid_->LowPart !=
                        instance.displayAdapterLUID.LowPart ||
                    !lastCompositionDisplayVidPnSourceId_.has_value() ||
                    lastCompositionDisplayVidPnSourceId_.value() !=
                        instance.displayVidPnSourceId;
                lastCompositionInstanceKind_ = instance.instanceKind;
                lastCompositionCrossAdapterCopy_ = !!instance.requiredCrossAdapterCopy;
                lastCompositionDisplayUniqueId_ = instance.displayUniqueId;
                lastCompositionDisplayAdapterLuid_ = instance.displayAdapterLUID;
                lastCompositionDisplayVidPnSourceId_ = instance.displayVidPnSourceId;
                if (!(enteredComposition && index == 0) &&
                    !(kindChanged || crossAdapterChanged || outputChanged))
                    continue;
                const wchar_t* instanceName = instance.instanceKind ==
                    CompositionFrameInstanceKind_ComposedOnScreen ? L"composed-on-screen" :
                    instance.instanceKind == CompositionFrameInstanceKind_ScanoutOnScreen
                        ? L"scanout-on-screen" : L"composed-to-intermediate";
                std::wostringstream message;
                logBase(message, L"composition-frame");
                message << L" contentTag=" << composition->GetContentTag()
                    << L" compositionFrameId=" << composition->GetCompositionFrameId()
                    << L" instanceCount=" << instanceCount
                    << L" instanceIndex=" << index
                    << L" instanceKind=" << instanceName
                    << L" displayAdapterLuidHigh=0x" << std::hex
                    << static_cast<ULONG>(instance.displayAdapterLUID.HighPart)
                    << L" displayAdapterLuidLow=0x" << instance.displayAdapterLUID.LowPart
                    << L" displayVidPnSourceId=" << std::dec << instance.displayVidPnSourceId
                    << L" displayUniqueId=" << instance.displayUniqueId
                    << L" renderAdapterLuidHigh=0x" << std::hex
                    << static_cast<ULONG>(instance.renderAdapterLUID.HighPart)
                    << L" renderAdapterLuidLow=0x" << instance.renderAdapterLUID.LowPart
                    << L" requiredCrossAdapterCopy=" << std::dec
                    << (instance.requiredCrossAdapterCopy ? 1 : 0)
                    << L" colorSpace=" << static_cast<unsigned>(instance.colorSpace)
                    << L" transform=" << instance.finalTransform.M11 << L"," << instance.finalTransform.M12
                    << L"," << instance.finalTransform.M21 << L"," << instance.finalTransform.M22
                    << L"," << instance.finalTransform.M31 << L"," << instance.finalTransform.M32;
                RuntimeLogger::Log(RuntimeLogLevel::Debug, message.str());
            }
            lastDisplayStatisticsKind_ = PresentStatisticsKind_CompositionFrame;
            return;
        }
        if (kind == PresentStatisticsKind_IndependentFlipFrame)
        {
            ComPtr<IIndependentFlipFramePresentStatistics> independent;
            if (FAILED(statistics->QueryInterface(IID_PPV_ARGS(&independent))))
            {
                LogPresentStatisticsFailure(
                    L"read-failed", L"query-independent-flip", E_NOINTERFACE);
                return;
            }
            const auto outputLuid = independent->GetOutputAdapterLUID();
            const auto displayedTime = independent->GetDisplayedTime().value;
            const auto presentDuration = independent->GetPresentDuration().value;
            const bool enteredIndependentFlip = !lastDisplayStatisticsKind_.has_value() ||
                lastDisplayStatisticsKind_.value() != PresentStatisticsKind_IndependentFlipFrame;
            const bool outputChanged = independentFlipObserved_ &&
                (outputLuid.HighPart != lastIndependentFlipOutputAdapterLuid_.HighPart ||
                    outputLuid.LowPart != lastIndependentFlipOutputAdapterLuid_.LowPart ||
                    independent->GetOutputVidPnSourceId() != lastIndependentFlipOutputVidPnSourceId_);
            ++independentFlipCount_;
            if (!independentFlipObserved_ || enteredIndependentFlip || outputChanged)
            {
                std::wostringstream message;
                logBase(message, L"independent-flip");
                message << L" contentTag=" << independent->GetContentTag()
                    << L" displayedTime100ns=" << displayedTime
                    << L" presentDuration100ns=" << presentDuration
                    << L" outputAdapterLuidHigh=0x" << std::hex
                    << static_cast<ULONG>(outputLuid.HighPart)
                    << L" outputAdapterLuidLow=0x" << outputLuid.LowPart
                    << L" outputVidPnSourceId=" << std::dec
                    << independent->GetOutputVidPnSourceId();
                RuntimeLogger::Log(RuntimeLogLevel::Debug, message.str());
            }
            independentFlipObserved_ = true;
            lastIndependentFlipDisplayedTime_ = displayedTime;
            lastIndependentFlipPresentDuration_ = presentDuration;
            lastIndependentFlipOutputAdapterLuid_ = outputLuid;
            lastIndependentFlipOutputVidPnSourceId_ = independent->GetOutputVidPnSourceId();
            lastDisplayStatisticsKind_ = PresentStatisticsKind_IndependentFlipFrame;
        }
    }
    catch (...)
    {
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"[HudPresentStats] reason=process-exception");
    }
}

void HudPresentation::LogPresentStatisticsFailure(
    std::wstring_view reason, std::wstring_view stage, HRESULT hr) const noexcept
{
    try
    {
        std::wostringstream message;
        message << L"[HudPresentStats] reason=" << reason << L" stage=" << stage
            << L" hr=" << HexHresult(hr) << L" epoch=" << presentationEpoch_;
        RuntimeLogger::Log(RuntimeLogLevel::Warn, message.str());
    }
    catch (...)
    {
    }
}

void HudPresentation::LogPresentStatisticsSummary(
    std::uint64_t observedTickMs, std::wstring_view reason) noexcept
{
    try
    {
        const wchar_t* lastDisplayKind = L"none";
        if (lastDisplayStatisticsKind_)
        {
            lastDisplayKind = lastDisplayStatisticsKind_ ==
                PresentStatisticsKind_CompositionFrame ? L"composition-frame" :
                lastDisplayStatisticsKind_ == PresentStatisticsKind_IndependentFlipFrame
                    ? L"independent-flip" : L"other";
        }
        const wchar_t* lastKind = L"none";
        if (lastCompositionInstanceKind_)
        {
            lastKind = lastCompositionInstanceKind_ ==
                CompositionFrameInstanceKind_ComposedOnScreen ? L"composed-on-screen" :
                lastCompositionInstanceKind_ == CompositionFrameInstanceKind_ScanoutOnScreen
                    ? L"scanout-on-screen" : L"composed-to-intermediate";
        }
        std::wostringstream message;
        message << L"[HudPresentStats] reason=" << reason
            << L" epoch=" << presentationEpoch_
            << L" queued=" << presentStatusQueuedCount_
            << L" skipped=" << presentStatusSkippedCount_
            << L" canceled=" << presentStatusCanceledCount_
            << L" composedOnScreen=" << composedOnScreenCount_
            << L" scanoutOnScreen=" << scanoutOnScreenCount_
            << L" composedToIntermediate=" << composedToIntermediateCount_
            << L" independentFlip=" << independentFlipCount_
            << L" lastPresentId=" << lastPresentStatisticsPresentId_
            << L" lastDisplayKind=" << lastDisplayKind
            << L" lastInstanceKind=" << lastKind
            << L" lastDisplayedTime100ns=" << lastIndependentFlipDisplayedTime_
            << L" lastPresentDuration100ns=" << lastIndependentFlipPresentDuration_
            << L" observedTickMs=" << observedTickMs;
        RuntimeLogger::Log(RuntimeLogLevel::Debug, message.str());
        presentStatusQueuedCount_ = 0;
        presentStatusSkippedCount_ = 0;
        presentStatusCanceledCount_ = 0;
        composedOnScreenCount_ = 0;
        scanoutOnScreenCount_ = 0;
        composedToIntermediateCount_ = 0;
        independentFlipCount_ = 0;
        presentStatisticsLastSummaryTickMs_ = observedTickMs;
    }
    catch (...)
    {
    }
}

void HudPresentation::LogDebugWindowState(
    std::wstring_view reason, const WINDOWPOS* windowPos) const noexcept
{
    try
    {
        const HWND hwnd = window_;
        const bool isWindow = hwnd && IsWindow(hwnd);
        const bool isWindowVisible = isWindow && IsWindowVisible(hwnd);
        const bool isIconic = isWindow && IsIconic(hwnd);
        const LONG_PTR exStyle = isWindow ? GetWindowLongPtrW(hwnd, GWL_EXSTYLE) : 0;
        RECT rect{};
        if (!isWindow || !GetWindowRect(hwnd, &rect))
            rect = RECT{};
        const HWND foreground = GetForegroundWindow();
        DWORD foregroundPid = 0;
        if (foreground) GetWindowThreadProcessId(foreground, &foregroundPid);
        const HWND zPrev = isWindow ? GetWindow(hwnd, GW_HWNDPREV) : nullptr;
        const HWND zNext = isWindow ? GetWindow(hwnd, GW_HWNDNEXT) : nullptr;
        DWORD zPrevPid = 0;
        DWORD zNextPid = 0;
        if (zPrev) GetWindowThreadProcessId(zPrev, &zPrevPid);
        if (zNext) GetWindowThreadProcessId(zNext, &zNextPid);

        const auto hex = [](const void* value)
        {
            std::wostringstream stream;
            stream << L"0x" << std::hex << reinterpret_cast<std::uintptr_t>(value);
            return stream.str();
        };

        std::wostringstream message;
        message << L"[HudWindowState] reason=" << reason
            << L" hwnd=" << hex(hwnd)
            << L" initialized=" << (initialized_ ? 1 : 0)
            << L" logicalVisible=" << (visible_ ? 1 : 0)
            << L" isWindow=" << (isWindow ? 1 : 0)
            << L" isWindowVisible=" << (isWindowVisible ? 1 : 0)
            << L" isIconic=" << (isIconic ? 1 : 0)
            << L" exStyle=0x" << std::hex << static_cast<std::uintptr_t>(exStyle) << std::dec
            << L" exTopmost=" << ((exStyle & WS_EX_TOPMOST) ? 1 : 0)
            << L" rect=" << rect.left << L"," << rect.top << L"," << rect.right
                << L"," << rect.bottom
            << L" foregroundHwnd=" << hex(foreground)
            << L" foregroundPid=" << foregroundPid
            << L" zPrevHwnd=" << hex(zPrev) << L" zPrevPid=" << zPrevPid
            << L" zNextHwnd=" << hex(zNext) << L" zNextPid=" << zNextPid;
        if (windowPos)
        {
            message << L" hwndInsertAfter=" << hex(windowPos->hwndInsertAfter)
                << L" posFlags=0x" << std::hex
                    << static_cast<unsigned>(windowPos->flags) << std::dec
                << L" x=" << windowPos->x << L" y=" << windowPos->y
                << L" cx=" << windowPos->cx << L" cy=" << windowPos->cy;
        }
        RuntimeLogger::Log(RuntimeLogLevel::Debug, message.str());
    }
    catch (...)
    {
        // Diagnostics must never affect HUD behavior.
    }
}

HRESULT HudPresentation::Show()
{
    if (!initialized_)
        return E_UNEXPECTED;
    HRESULT hr = RefreshDisplayIfNeeded();
    if (FAILED(hr)) return hr;
    if (visible_)
    {
        LogDebugWindowState(L"show-already-visible");
        return S_OK;
    }
    hr = CommitVisibility(true);
    if (FAILED(hr)) return hr;
    if (!SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW))
        return LastErrorResult();
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    visible_ = true;
    LogDebugWindowState(L"show-applied");
    return S_OK;
}

HRESULT HudPresentation::Hide()
{
    if (!initialized_ || !visible_) return S_OK;
    HRESULT hr = CommitVisibility(false);
    if (FAILED(hr)) return hr;
    ShowWindow(window_, SW_HIDE);
    visible_ = false;
    LogDebugWindowState(L"hide-applied");
    return S_OK;
}

HRESULT HudPresentation::SetHudOpacity(float opacityPercent)
{
    if (!window_)
        return E_UNEXPECTED;
    const float clamped = std::clamp(opacityPercent, 0.0f, 100.0f);
    const BYTE alpha = static_cast<BYTE>(HudOpacityByte(clamped));
    if (!SetLayeredWindowAttributes(window_, 0, alpha, LWA_ALPHA))
        return LastErrorResult();
    opacityPercent_ = clamped;
    return S_OK;
}

void HudPresentation::Shutdown() noexcept
{
    if (initialized_)
        LogPresentationState(L"shutdown");
    if (presentStatisticsDiagnosticsEnabled_)
    {
        if (presentStatisticsDiagnosticsActive_.load())
            LogPresentStatisticsSummary(GetTickCount64(), L"shutdown-summary");
        ShutdownPresentStatisticsDiagnostics();
    }
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
    if (self && message == kHudPresentationStatsReadyMessage)
    {
        self->presentStatisticsMessagePending_.store(false);
        self->DrainPresentStatistics();
        return 0;
    }
    if (message == WM_DISPLAYCHANGE || message == WM_DPICHANGED)
    {
        if (self)
        {
            self->LogDebugWindowState(message == WM_DISPLAYCHANGE
                ? L"wm-displaychange" : L"wm-dpichanged");
            self->displayChangePending_ = true;
        }
        return 0;
    }
    if (self && message == WM_SHOWWINDOW)
    {
        std::wostringstream reason;
        reason << L"wm-showwindow shown=" << (wParam ? 1 : 0)
            << L" statusLParam=0x" << std::hex
            << static_cast<unsigned>(static_cast<ULONG_PTR>(lParam));
        self->LogDebugWindowState(reason.str());
    }
    else if (self && message == WM_WINDOWPOSCHANGED)
    {
        const auto* pos = reinterpret_cast<const WINDOWPOS*>(lParam);
        const bool zOrderChanged = pos && !(pos->flags & SWP_NOZORDER);
        const bool showHideChanged = pos &&
            (pos->flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)) != 0;
        if (zOrderChanged || showHideChanged)
            self->LogDebugWindowState(L"wm-windowposchanged", pos);
    }
    else if (self && message == WM_STYLECHANGED &&
        static_cast<int>(wParam) == GWL_EXSTYLE)
    {
        if (const auto* styles = reinterpret_cast<const STYLESTRUCT*>(lParam))
        {
            std::wostringstream reason;
            reason << L"wm-stylechanged-exstyle styleOld=0x" << std::hex
                << styles->styleOld << L" styleNew=0x" << styles->styleNew
                << L" oldTopmost=" << ((styles->styleOld & WS_EX_TOPMOST) ? 1 : 0)
                << L" newTopmost=" << ((styles->styleNew & WS_EX_TOPMOST) ? 1 : 0);
            self->LogDebugWindowState(reason.str());
        }
    }
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wParam, lParam);
}
}
