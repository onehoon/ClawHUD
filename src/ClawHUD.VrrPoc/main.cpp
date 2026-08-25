#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <d2d1_3.h>
#include <dwrite_3.h>
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
    std::wstringstream result;
    result << L"0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr);
    return result.str();
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
        hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
            wc.lpszClassName, L"ClawHUD VRR TEST", WS_POPUP, 24, 24, 520, 72,
            nullptr, nullptr, wc.hInstance, this);
        if (!hwnd_) Fail(log_, L"CreateWindowEx", HRESULT_FROM_WIN32(GetLastError()));
        SetWindowPos(hwnd_, HWND_TOPMOST, 24, 24, 520, 72, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    void CreateD3DDevice()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
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
        if (FAILED(hr)) Fail(log_, L"CreatePresentationFactory", hr);
        const BOOL supported = factory_->IsPresentationSupportedWithIndependentFlip();
        log_.Write(L"Composition Swapchain: initialized");
        log_.Write(std::wstring(L"Independent Flip Presentation Support: ") + (supported ? L"YES" : L"NO"));
        if (!supported) throw std::runtime_error("Independent flip presentation is not supported");
        hr = factory_->CreatePresentationManager(&manager_);
        if (FAILED(hr)) Fail(log_, L"IPresentationFactory::CreatePresentationManager", hr);
        hr = DCompositionCreateSurfaceHandle(COMPOSITIONOBJECT_ALL_ACCESS, nullptr, &surfaceHandle_);
        if (FAILED(hr)) Fail(log_, L"DCompositionCreateSurfaceHandle", hr);
        hr = manager_->CreatePresentationSurface(surfaceHandle_, &presentationSurface_);
        if (FAILED(hr)) Fail(log_, L"IPresentationManager::CreatePresentationSurface", hr);
        hr = compositionDevice_->CreateSurfaceFromHandle(surfaceHandle_,
            reinterpret_cast<IUnknown**>(compositionSurface_.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) Fail(log_, L"IDCompositionDevice::CreateSurfaceFromHandle", hr);
        hr = presentationSurface_->SetAlphaMode(DXGI_ALPHA_MODE_PREMULTIPLIED);
        if (FAILED(hr)) Fail(log_, L"IPresentationSurface::SetAlphaMode", hr);
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
        if (FAILED(hr)) Fail(log_, L"ID3D11Device::CreateTexture2D(displayable)", hr);
        hr = device_->CreateRenderTargetView(texture_.Get(), nullptr, &renderTarget_);
        if (FAILED(hr)) Fail(log_, L"ID3D11Device::CreateRenderTargetView", hr);
        hr = manager_->AddBufferFromResource(texture_.Get(), &buffer_);
        if (FAILED(hr)) Fail(log_, L"IPresentationManager::AddBufferFromResource", hr);
        hr = presentationSurface_->SetBuffer(buffer_.Get());
        if (FAILED(hr)) Fail(log_, L"IPresentationSurface::SetBuffer", hr);
    }

    void RenderBuffer()
    {
        const float clear[] = { 0.03f, 0.03f, 0.03f, 0.80f };
        context_->ClearRenderTargetView(renderTarget_.Get(), clear);
        context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
        D3D11_VIEWPORT viewport{ 0, 0, 520, 72, 0, 1 };
        context_->RSSetViewports(1, &viewport);
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        context_->Flush();
        DrawLabel();
        HRESULT hr = manager_->Present();
        if (FAILED(hr)) Fail(log_, L"IPresentationManager::Present", hr);
    }

    void DrawLabel()
    {
        ComPtr<IDXGISurface> surface;
        HRESULT hr = texture_.As(&surface);
        if (FAILED(hr)) Fail(log_, L"ID3D11Texture2D::QueryInterface(IDXGISurface)", hr);

        ComPtr<ID2D1Factory1> d2dFactory;
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
            reinterpret_cast<void**>(d2dFactory.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) Fail(log_, L"D2D1CreateFactory", hr);
        ComPtr<IDXGIDevice> dxgiDevice;
        hr = device_.As(&dxgiDevice);
        if (FAILED(hr)) Fail(log_, L"ID3D11Device::QueryInterface(IDXGIDevice)", hr);
        ComPtr<ID2D1Device> d2dDevice;
        hr = d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
        if (FAILED(hr)) Fail(log_, L"ID2D1Factory1::CreateDevice", hr);
        ComPtr<ID2D1DeviceContext> d2dContext;
        hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext);
        if (FAILED(hr)) Fail(log_, L"ID2D1Device::CreateDeviceContext", hr);

        D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap1> bitmap;
        hr = d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &bitmap);
        if (FAILED(hr)) Fail(log_, L"ID2D1DeviceContext::CreateBitmapFromDxgiSurface", hr);
        d2dContext->SetTarget(bitmap.Get());
        d2dContext->BeginDraw();

        ComPtr<IDWriteFactory> writeFactory;
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(writeFactory.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) Fail(log_, L"DWriteCreateFactory", hr);
        ComPtr<IDWriteTextFormat> format;
        hr = writeFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"en-US", &format);
        if (FAILED(hr)) Fail(log_, L"IDWriteFactory::CreateTextFormat", hr);
        ComPtr<ID2D1SolidColorBrush> brush;
        hr = d2dContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &brush);
        if (FAILED(hr)) Fail(log_, L"ID2D1DeviceContext::CreateSolidColorBrush", hr);
        const D2D1_RECT_F textRect = D2D1::RectF(16.0f, 20.0f, 504.0f, 60.0f);
        d2dContext->DrawText(L"ClawHUD VRR TEST", 16, format.Get(), textRect, brush.Get());
        hr = d2dContext->EndDraw();
        if (FAILED(hr)) Fail(log_, L"ID2D1DeviceContext::EndDraw", hr);
    }

    void SetHudVisible(bool visible)
    {
        hudVisible_ = visible;
        HRESULT hr = visual_->SetContent(visible ? compositionSurface_.Get() : nullptr);
        if (FAILED(hr)) Fail(log_, L"IDCompositionVisual::SetContent", hr);
        hr = compositionDevice_->Commit();
        if (FAILED(hr)) Fail(log_, L"IDCompositionDevice::Commit", hr);
        log_.Write(std::wstring(L"HUD: ") + (visible ? L"ON" : L"OFF"));
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
    log.Write(L"ClawHUD VRR PoC");
    log.Write(L"OS: Windows 11 (required; no compatibility fallback)");
    try
    {
        PresentationOverlay overlay(log);
        overlay.Initialize();
        log.Write(L"F8 = HUD ON/OFF; ESC = Exit");
        RegisterHotKey(overlay.Window(), 1, MOD_NOREPEAT, VK_F8);
        RegisterHotKey(overlay.Window(), 2, MOD_NOREPEAT, VK_ESCAPE);
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
