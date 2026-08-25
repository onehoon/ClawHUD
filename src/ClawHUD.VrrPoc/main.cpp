#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <d2d1_3.h>
#include <presentation.h>
#include <wrl/client.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{
class Logger
{
public:
    Logger()
    {
        std::filesystem::create_directories(L"logs");
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{};
        localtime_s(&local, &now);
        std::wstringstream name;
        name << L"logs/vrr-poc-" << std::put_time(&local, L"%Y%m%d-%H%M%S") << L".log";
        file_.open(name.str(), std::ios::out | std::ios::app);
    }

    void Write(const std::wstring& line)
    {
        wprintf(L"%ls\n", line.c_str());
        if (file_.is_open()) file_ << line << L'\n' << std::flush;
    }

private:
    std::wofstream file_;
};

std::wstring HResult(HRESULT hr)
{
    const wchar_t* label = L"FAILED";
    if (hr == S_OK)
        label = L"S_OK";
    else if (hr == S_FALSE)
        label = L"S_FALSE";
    else if (SUCCEEDED(hr))
        label = L"SUCCEEDED";

    std::wstringstream result;
    result << label << L" (0x"
        << std::hex << std::uppercase << static_cast<unsigned long>(hr) << L")";
    return result.str();
}

void LogResult(Logger& log, const wchar_t* operation, HRESULT hr)
{
    log.Write(std::wstring(operation) + L": " + HResult(hr));
}

[[noreturn]] void Fail(Logger& log, const wchar_t* operation, HRESULT hr)
{
    log.Write(std::wstring(operation) + L" failed: " + HResult(hr));
    throw std::runtime_error("ClawHUD initialization failed");
}

class PresentationOverlay
{
public:
    explicit PresentationOverlay(Logger& log) : log_(log) {}

    void Initialize()
    {
        CreateOverlayWindow();
        CreateD3DDevice();
        CreatePresentation();
        CreateDisplayableBuffer();
        RenderBuffer();
        SetHudVisible(true);
    }

    void Toggle() { SetHudVisible(!hudVisible_); }
    HWND Window() const { return hwnd_; }

private:
    void CreateOverlayWindow()
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"ClawHUD.VrrPoc";
        RegisterClassW(&wc);
        constexpr DWORD overlayExStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
            WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP;
        hwnd_ = CreateWindowExW(overlayExStyle,
            wc.lpszClassName, L"ClawHUD VRR TEST", WS_POPUP, 24, 24, 520, 72,
            nullptr, nullptr, wc.hInstance, this);
        if (!hwnd_) Fail(log_, L"CreateWindowEx", HRESULT_FROM_WIN32(GetLastError()));
        if (!SetWindowPos(hwnd_, HWND_TOPMOST, 24, 24, 520, 72, SWP_NOACTIVATE))
            Fail(log_, L"SetWindowPos", HRESULT_FROM_WIN32(GetLastError()));
        log_.Write(L"SetWindowPos(HWND_TOPMOST): success");
    }

    void CreateD3DDevice()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT |
            D3D11_CREATE_DEVICE_SINGLETHREADED |
            D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL selected{};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, &selected, &context_);
        if (FAILED(hr)) Fail(log_, L"D3D11CreateDevice", hr);

        ComPtr<IDXGIDevice> dxgiDevice;
        hr = device_.As(&dxgiDevice);
        if (FAILED(hr)) Fail(log_, L"ID3D11Device::QueryInterface(IDXGIDevice)", hr);
        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(&adapter);
        if (FAILED(hr)) Fail(log_, L"IDXGIDevice::GetAdapter", hr);
        DXGI_ADAPTER_DESC desc{};
        hr = adapter->GetDesc(&desc);
        if (FAILED(hr)) Fail(log_, L"IDXGIAdapter::GetDesc", hr);
        log_.Write(std::wstring(L"Adapter: ") + desc.Description);

        for (UINT index = 0;; ++index)
        {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(index, &output);
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(hr)) Fail(log_, L"IDXGIAdapter::EnumOutputs", hr);

            DXGI_OUTPUT_DESC outputDesc{};
            hr = output->GetDesc(&outputDesc);
            if (FAILED(hr)) Fail(log_, L"IDXGIOutput::GetDesc", hr);
            if (!outputDesc.AttachedToDesktop) continue;

            std::wstringstream outputLog;
            outputLog << L"Output: " << outputDesc.DeviceName
                << L"; Desktop: " << outputDesc.DesktopCoordinates.left << L","
                << outputDesc.DesktopCoordinates.top << L" - "
                << outputDesc.DesktopCoordinates.right << L","
                << outputDesc.DesktopCoordinates.bottom
                << L"; AttachedToDesktop: YES; Rotation: "
                << static_cast<int>(outputDesc.Rotation);
            log_.Write(outputLog.str());
        }

        hr = DCompositionCreateDevice(dxgiDevice.Get(), __uuidof(IDCompositionDevice),
            reinterpret_cast<void**>(compositionDevice_.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) Fail(log_, L"DCompositionCreateDevice", hr);
        hr = compositionDevice_->CreateTargetForHwnd(hwnd_, TRUE, &target_);
        if (FAILED(hr)) Fail(log_, L"IDCompositionDevice::CreateTargetForHwnd", hr);
        hr = compositionDevice_->CreateVisual(&visual_);
        if (FAILED(hr)) Fail(log_, L"IDCompositionDevice::CreateVisual", hr);
        hr = target_->SetRoot(visual_.Get());
        if (FAILED(hr)) Fail(log_, L"IDCompositionTarget::SetRoot", hr);
    }

    void CreatePresentation()
    {
        HRESULT hr = CreatePresentationFactory(device_.Get(), __uuidof(IPresentationFactory),
            reinterpret_cast<void**>(factory_.ReleaseAndGetAddressOf()));
        LogResult(log_, L"CreatePresentationFactory", hr);
        if (FAILED(hr)) Fail(log_, L"CreatePresentationFactory", hr);
        const BOOL supported = factory_->IsPresentationSupportedWithIndependentFlip();
        log_.Write(L"Composition Swapchain: initialized");
        log_.Write(std::wstring(L"Independent Flip Presentation Support: ") + (supported ? L"YES" : L"NO"));
        if (!supported) throw std::runtime_error("Independent flip presentation is not supported");
        hr = factory_->CreatePresentationManager(&manager_);
        LogResult(log_, L"CreatePresentationManager", hr);
        if (FAILED(hr)) Fail(log_, L"IPresentationFactory::CreatePresentationManager", hr);
        hr = DCompositionCreateSurfaceHandle(COMPOSITIONOBJECT_ALL_ACCESS, nullptr, &surfaceHandle_);
        LogResult(log_, L"DCompositionCreateSurfaceHandle", hr);
        if (FAILED(hr)) Fail(log_, L"DCompositionCreateSurfaceHandle", hr);
        hr = manager_->CreatePresentationSurface(surfaceHandle_, &presentationSurface_);
        LogResult(log_, L"CreatePresentationSurface", hr);
        if (FAILED(hr)) Fail(log_, L"IPresentationManager::CreatePresentationSurface", hr);
        hr = compositionDevice_->CreateSurfaceFromHandle(surfaceHandle_,
            reinterpret_cast<IUnknown**>(compositionSurface_.ReleaseAndGetAddressOf()));
        LogResult(log_, L"CreateSurfaceFromHandle", hr);
        if (FAILED(hr)) Fail(log_, L"IDCompositionDevice::CreateSurfaceFromHandle", hr);
        hr = presentationSurface_->SetAlphaMode(DXGI_ALPHA_MODE_PREMULTIPLIED);
        LogResult(log_, L"SetAlphaMode(PREMULTIPLIED)", hr);
        if (FAILED(hr)) Fail(log_, L"SetAlphaMode(PREMULTIPLIED)", hr);

        RECT sourceRect{ 0, 0, 520, 72 };
        hr = presentationSurface_->SetSourceRect(&sourceRect);
        LogResult(log_, L"SetSourceRect(0,0,520,72)", hr);
        if (FAILED(hr)) Fail(log_, L"SetSourceRect", hr);

        PresentationTransform transform{};
        transform.M11 = 1.0f;
        transform.M22 = 1.0f;
        hr = presentationSurface_->SetTransform(&transform);
        LogResult(log_, L"SetTransform(identity)", hr);
        if (FAILED(hr)) Fail(log_, L"SetTransform", hr);

        hr = presentationSurface_->SetLetterboxingMargins(0.0f, 0.0f, 0.0f, 0.0f);
        LogResult(log_, L"SetLetterboxingMargins(0,0,0,0)", hr);
        if (FAILED(hr)) Fail(log_, L"SetLetterboxingMargins", hr);
        log_.Write(L"Presentation manager: initialized");
    }

    void CreateDisplayableBuffer()
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 520; desc.Height = 72; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
            D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE;
        HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &texture_);
        LogResult(log_, L"CreateTexture2D(displayable)", hr);
        if (FAILED(hr)) Fail(log_, L"ID3D11Device::CreateTexture2D(displayable)", hr);
        hr = device_->CreateRenderTargetView(texture_.Get(), nullptr, &renderTarget_);
        LogResult(log_, L"CreateRenderTargetView", hr);
        if (FAILED(hr)) Fail(log_, L"ID3D11Device::CreateRenderTargetView", hr);

        ComPtr<IDXGIDevice> dxgiDevice;
        hr = device_.As(&dxgiDevice);
        LogResult(log_, L"Query IDXGIDevice", hr);
        if (FAILED(hr)) Fail(log_, L"ID3D11Device::QueryInterface(IDXGIDevice)", hr);

        ComPtr<ID2D1Factory1> d2dFactory;
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
            reinterpret_cast<void**>(d2dFactory.ReleaseAndGetAddressOf()));
        LogResult(log_, L"CreateD2DFactory", hr);
        if (FAILED(hr)) Fail(log_, L"D2D1CreateFactory", hr);

        ComPtr<ID2D1Device> d2dDevice;
        hr = d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
        LogResult(log_, L"CreateD2DDevice", hr);
        if (FAILED(hr)) Fail(log_, L"ID2D1Factory1::CreateDevice", hr);

        ComPtr<ID2D1DeviceContext> d2dContext;
        hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext);
        LogResult(log_, L"CreateD2DDeviceContext", hr);
        if (FAILED(hr)) Fail(log_, L"ID2D1Device::CreateDeviceContext", hr);

        ComPtr<IDXGISurface> dxgiSurface;
        hr = texture_.As(&dxgiSurface);
        LogResult(log_, L"Query IDXGISurface", hr);
        if (FAILED(hr)) Fail(log_, L"ID3D11Texture2D::QueryInterface(IDXGISurface)", hr);

        const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap1> bitmap;
        hr = d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &properties, &bitmap);
        LogResult(log_, L"CreateBitmapFromDxgiSurface", hr);
        if (FAILED(hr)) Fail(log_, L"ID2D1DeviceContext::CreateBitmapFromDxgiSurface", hr);

        d2dContext->SetTarget(bitmap.Get());
        log_.Write(L"D2D SetTarget: OK");
        d2dContext->BeginDraw();
        log_.Write(L"D2D BeginDraw: OK");
        d2dContext->Clear(D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f));
        log_.Write(L"D2D Clear: OK (solid opaque red)");
        hr = d2dContext->EndDraw();
        LogResult(log_, L"D2D EndDraw", hr);
        if (FAILED(hr)) Fail(log_, L"ID2D1DeviceContext::EndDraw", hr);
        context_->Flush();
        log_.Write(L"D2D Flush: OK (D3D11 device context)");

        hr = manager_->AddBufferFromResource(texture_.Get(), &buffer_);
        LogResult(log_, L"AddBufferFromResource", hr);
        if (FAILED(hr)) Fail(log_, L"IPresentationManager::AddBufferFromResource", hr);
        hr = presentationSurface_->SetBuffer(buffer_.Get());
        LogResult(log_, L"SetBuffer", hr);
        if (FAILED(hr)) Fail(log_, L"IPresentationSurface::SetBuffer", hr);
    }

    void RenderBuffer()
    {
        HRESULT hr = manager_->Present();
        LogResult(log_, L"IPresentationManager::Present", hr);
        if (FAILED(hr)) Fail(log_, L"IPresentationManager::Present", hr);
    }

    void SetHudVisible(bool visible)
    {
        if (!visible) ShowWindow(hwnd_, SW_HIDE);
        HRESULT hr = visual_->SetContent(visible ? compositionSurface_.Get() : nullptr);
        LogResult(log_, visible ? L"Visual.SetContent(attach)" : L"Visual.SetContent(detach)", hr);
        if (FAILED(hr)) Fail(log_, L"IDCompositionVisual::SetContent", hr);
        hr = compositionDevice_->Commit();
        LogResult(log_, L"DComposition.Commit", hr);
        if (FAILED(hr)) Fail(log_, L"IDCompositionDevice::Commit", hr);
        if (visible)
        {
            ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
            log_.Write(L"ShowWindow: called");
            RECT rect{};
            const BOOL valid = IsWindow(hwnd_);
            const BOOL shown = IsWindowVisible(hwnd_);
            const BOOL gotRect = GetWindowRect(hwnd_, &rect);
            const LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
            log_.Write(std::wstring(L"HWND valid: ") + (valid ? L"YES" : L"NO"));
            log_.Write(std::wstring(L"HWND visible: ") + (shown ? L"YES" : L"NO"));
            if (gotRect)
            {
                std::wstringstream windowLog;
                windowLog << L"WindowRect: " << rect.left << L"," << rect.top << L" - "
                    << rect.right << L"," << rect.bottom;
                log_.Write(windowLog.str());
            }
            std::wstringstream styleLog;
            styleLog << L"WindowExStyle: 0x" << std::hex << std::uppercase
                << static_cast<unsigned long long>(exStyle);
            log_.Write(styleLog.str());
        }
        hudVisible_ = visible;
        log_.Write(std::wstring(L"Visual attached: ") + (visible ? L"YES" : L"NO"));
        log_.Write(std::wstring(L"Window shown: ") + (visible ? L"YES" : L"NO"));
        log_.Write(L"Physical visibility: NOT VERIFIED");
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCHITTEST) return HTTRANSPARENT;
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_KEYDOWN && wParam == VK_ESCAPE) PostQuitMessage(0);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    Logger& log_;
    HWND hwnd_{};
    HANDLE surfaceHandle_{ INVALID_HANDLE_VALUE };
    bool hudVisible_{};
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDCompositionDevice> compositionDevice_;
    ComPtr<IDCompositionTarget> target_;
    ComPtr<IDCompositionVisual> visual_;
    ComPtr<IDCompositionSurface> compositionSurface_;
    ComPtr<IPresentationFactory> factory_;
    ComPtr<IPresentationManager> manager_;
    ComPtr<IPresentationSurface> presentationSurface_;
    ComPtr<IPresentationBuffer> buffer_;
    ComPtr<ID3D11Texture2D> texture_;
    ComPtr<ID3D11RenderTargetView> renderTarget_;
};
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    Logger log;
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        Fail(log, L"SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)",
            HRESULT_FROM_WIN32(GetLastError()));
    log.Write(L"ClawHUD VRR PoC");
    log.Write(L"OS: Windows 11 (required; no compatibility fallback)");
    try
    {
        PresentationOverlay overlay(log);
        overlay.Initialize();
        const bool diagnostic = GetCommandLineW() && wcsstr(GetCommandLineW(), L"--diagnostic") != nullptr;
        if (diagnostic)
        {
            log.Write(L"Diagnostic mode: HUD ON for 35 seconds");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
            MSG message{};
            while (std::chrono::steady_clock::now() < deadline)
            {
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT) return 0;
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                Sleep(50);
            }
            log.Write(L"Diagnostic mode: normal exit");
            return 0;
        }
        log.Write(L"F8 = HUD ON/OFF; ESC = Exit");
        if (!RegisterHotKey(overlay.Window(), 1, MOD_NOREPEAT, VK_F8))
            log.Write(L"RegisterHotKey(F8) unavailable; continuing without toggle");
        if (!RegisterHotKey(overlay.Window(), 2, MOD_NOREPEAT, VK_ESCAPE))
            log.Write(L"RegisterHotKey(ESC) unavailable; continuing without exit hotkey");
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (message.message == WM_HOTKEY && message.wParam == 1) overlay.Toggle();
            if (message.message == WM_HOTKEY && message.wParam == 2) PostQuitMessage(0);
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        UnregisterHotKey(overlay.Window(), 1);
        UnregisterHotKey(overlay.Window(), 2);
        return 0;
    }
    catch (const std::exception&)
    {
        MessageBoxW(nullptr, L"ClawHUD VRR PoC initialization failed. See console/logs for HRESULT.",
            L"ClawHUD", MB_OK | MB_ICONERROR);
        return 1;
    }
}
