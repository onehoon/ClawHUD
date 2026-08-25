#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <d2d1_3.h>
#include <dwrite.h>
#include <presentation.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
        name << std::put_time(&local, L"%Y%m%d-%H%M%S");
        stamp_ = name.str();
        name.str(L"");
        name.clear();
        name << L"logs/vrr-poc-" << stamp_ << L".log";
        file_.open(name.str(), std::ios::out | std::ios::app);
    }

    void Write(const std::wstring& line)
    {
        wprintf(L"%ls\n", line.c_str());
        if (file_.is_open()) file_ << line << L'\n' << std::flush;
    }

    const std::wstring& Stamp() const { return stamp_; }
    std::filesystem::path CsvPath() const
    {
        return std::filesystem::path(L"logs") / (L"vrr-poc-" + stamp_ + L".csv");
    }

private:
    std::wofstream file_;
    std::wstring stamp_;
};

std::wstring WallClock()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::wstringstream value;
    value << std::setfill(L'0') << std::setw(4) << time.wYear << L'-'
        << std::setw(2) << time.wMonth << L'-' << std::setw(2) << time.wDay << L' '
        << std::setw(2) << time.wHour << L':' << std::setw(2) << time.wMinute << L':'
        << std::setw(2) << time.wSecond << L'.' << std::setw(3) << time.wMilliseconds;
    return value.str();
}

double QpcMilliseconds()
{
    LARGE_INTEGER counter{}, frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(counter.QuadPart) * 1000.0 /
        static_cast<double>(frequency.QuadPart);
}

struct TargetProcess
{
    DWORD pid{};
    HANDLE handle{};
    std::wstring path;
};

void CloseTarget(TargetProcess& target)
{
    if (target.handle)
    {
        CloseHandle(target.handle);
        target.handle = nullptr;
    }
}

bool TargetAlive(const TargetProcess& target)
{
    DWORD exitCode{};
    return target.handle && GetExitCodeProcess(target.handle, &exitCode) != FALSE &&
        exitCode == STILL_ACTIVE;
}

bool IsExcludedTarget(const std::wstring& path)
{
    std::wstring lowered = path;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(towlower(value)); });
    return lowered.ends_with(L"\\explorer.exe") || lowered.ends_with(L"\\clawhud.vrrpoc.exe");
}

DWORD ForegroundPid()
{
    const HWND window = GetForegroundWindow();
    DWORD pid{};
    if (!window || !GetWindowThreadProcessId(window, &pid)) return 0;
    return pid;
}

bool TryOpenTarget(DWORD pid, TargetProcess& target)
{
    if (!pid || pid == GetCurrentProcessId()) return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return false;

    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length))
    {
        CloseHandle(process);
        return false;
    }
    path.resize(length);
    if (IsExcludedTarget(path))
    {
        CloseHandle(process);
        return false;
    }

    target = TargetProcess{ pid, process, std::move(path) };
    return true;
}

bool AcquireTarget(Logger& log, TargetProcess& target)
{
    constexpr auto kTargetWait = std::chrono::seconds(15);
    constexpr auto kPollInterval = std::chrono::milliseconds(250);
    const DWORD launcherForegroundPid = ForegroundPid();
    const auto deadline = std::chrono::steady_clock::now() + kTargetWait;

    log.Write(L"Waiting for foreground game; switch back to the game now");
    while (std::chrono::steady_clock::now() < deadline)
    {
        const DWORD pid = ForegroundPid();
        if (pid && pid != launcherForegroundPid && TryOpenTarget(pid, target))
        {
            log.Write(L"Target Process: " + target.path);
            log.Write(L"Target PID: " + std::to_wstring(target.pid));
            return true;
        }
        Sleep(static_cast<DWORD>(kPollInterval.count()));
    }

    log.Write(L"TEST FAILED");
    log.Write(L"Reason: No valid foreground target within 15 seconds");
    return false;
}

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
    void SetVisible(bool visible) { SetHudVisible(visible); }
    bool Visible() const { return hudVisible_; }
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
            WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST;
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
            if (!output_) output_ = output;

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
        LogMpoCapability();
    }

    void LogMpoCapability()
    {
        log_.Write(L"=== MPO / HARDWARE COMPOSITION CAPABILITY ===");
        ComPtr<IDXGIOutput3> output3;
        if (output_ && SUCCEEDED(output_.As(&output3)))
        {
            UINT flags{};
            const HRESULT hr = output3->CheckOverlaySupport(
                DXGI_FORMAT_B8G8R8A8_UNORM, device_.Get(), &flags);
            LogResult(log_, L"CheckOverlaySupport(BGRA8)", hr);
            if (SUCCEEDED(hr))
            {
                log_.Write(std::wstring(L"  DIRECT: ") +
                    ((flags & DXGI_OVERLAY_SUPPORT_FLAG_DIRECT) ? L"YES" : L"NO"));
                log_.Write(std::wstring(L"  SCALING: ") +
                    ((flags & DXGI_OVERLAY_SUPPORT_FLAG_SCALING) ? L"YES" : L"NO"));
            }
        }
        else
        {
            log_.Write(L"DXGI Overlay Support (BGRA8): Unavailable");
        }

        ComPtr<IDXGIOutput6> output6;
        if (output_ && SUCCEEDED(output_.As(&output6)))
        {
            UINT flags{};
            const HRESULT hr = output6->CheckHardwareCompositionSupport(&flags);
            LogResult(log_, L"CheckHardwareCompositionSupport", hr);
            if (SUCCEEDED(hr))
            {
                log_.Write(std::wstring(L"  FULLSCREEN: ") +
                    ((flags & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_FULLSCREEN) ? L"YES" : L"NO"));
                log_.Write(std::wstring(L"  WINDOWED: ") +
                    ((flags & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_WINDOWED) ? L"YES" : L"NO"));
                log_.Write(std::wstring(L"  CURSOR_STRETCHED: ") +
                    ((flags & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_CURSOR_STRETCHED) ? L"YES" : L"NO"));
            }
        }
        else
        {
            log_.Write(L"DXGI Hardware Composition Support: Unavailable");
        }
        log_.Write(L"MPO capability: SUPPORTING EVIDENCE ONLY");
        log_.Write(L"Actual MPO plane assignment: NOT DIRECTLY OBSERVABLE");
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

        ComPtr<IDWriteFactory> writeFactory;
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(writeFactory.ReleaseAndGetAddressOf()));
        LogResult(log_, L"CreateDWriteFactory", hr);
        if (FAILED(hr)) Fail(log_, L"DWriteCreateFactory", hr);

        ComPtr<IDWriteTextFormat> textFormat;
        hr = writeFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 28.0f, L"", &textFormat);
        LogResult(log_, L"CreateTextFormat", hr);
        if (FAILED(hr)) Fail(log_, L"IDWriteFactory::CreateTextFormat", hr);

        ComPtr<ID2D1SolidColorBrush> textBrush;
        hr = d2dContext->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &textBrush);
        LogResult(log_, L"CreateTextBrush", hr);
        if (FAILED(hr)) Fail(log_, L"ID2D1DeviceContext::CreateSolidColorBrush", hr);

        constexpr wchar_t text[] = L"ClawHUD";
        const D2D1_RECT_F textRect = D2D1::RectF(20.0f, 10.0f, 500.0f, 65.0f);
        d2dContext->DrawText(text, ARRAYSIZE(text) - 1, textFormat.Get(), textRect, textBrush.Get());
        log_.Write(L"DirectWrite DrawText: issued (\"ClawHUD\")");
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
            if (!SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW))
                Fail(log_, L"SetWindowPos(HWND_TOPMOST)", HRESULT_FROM_WIN32(GetLastError()));
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
            log_.Write(std::wstring(L"WS_EX_TOPMOST: ") +
                ((exStyle & WS_EX_TOPMOST) ? L"YES" : L"NO"));
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
    ComPtr<IDXGIOutput> output_;
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

constexpr UINT_PTR kAutomaticTimerId = 1;
constexpr UINT kAutomaticToggleMs = 5000;
constexpr std::chrono::seconds kCaptureWatchdog{ 70 };

void LogHudState(Logger& log, const PresentationOverlay& overlay)
{
    log.Write(L"=== HUD STATE ===");
    log.Write(L"Wall Time: " + WallClock());
    log.Write(L"QPC_MS: " + std::to_wstring(QpcMilliseconds()));
    log.Write(std::wstring(L"HUD: ") + (overlay.Visible() ? L"ON" : L"OFF"));
    log.Write(std::wstring(L"Visual attached: ") + (overlay.Visible() ? L"YES" : L"NO"));
    log.Write(std::wstring(L"HWND visible: ") +
        (IsWindowVisible(overlay.Window()) ? L"YES" : L"NO"));
}

std::filesystem::path ExecutableDirectory()
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length) return {};
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

struct PresentMonResult
{
    DWORD exitCode{ STILL_ACTIVE };
    bool csvCreated{};
};

bool StartPresentMon(Logger& log, const TargetProcess& target, PROCESS_INFORMATION& process)
{
    const auto presentMon = ExecutableDirectory() / L"PresentMon.exe";
    const auto csv = std::filesystem::absolute(log.CsvPath());
    log.Write(L"PresentMon path: " + presentMon.wstring());
    log.Write(L"PresentMon version expected: 2.5.1");
    log.Write(L"CSV path: " + csv.wstring());
    if (!std::filesystem::exists(presentMon))
    {
        log.Write(L"PresentMon: FAILED");
        log.Write(L"Reason: PresentMon.exe not found beside ClawHUD.VrrPoc.exe");
        return false;
    }

    const std::wstring session = L"ClawHUD-VrrPoc-" + std::to_wstring(target.pid) + L"-" + log.Stamp();
    const std::wstring command = L"\"" + presentMon.wstring() + L"\" --process_id " +
        std::to_wstring(target.pid) + L" --output_file \"" + csv.wstring() +
        L"\" --timed 60 --terminate_after_timed --no_console_stats --qpc_time_ms --session_name " + session;
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    STARTUPINFOW startup{ sizeof(startup) };
    if (!CreateProcessW(presentMon.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, presentMon.parent_path().c_str(), &startup, &process))
    {
        log.Write(L"PresentMon: FAILED");
        log.Write(L"Reason: CreateProcessW failed");
        return false;
    }
    CloseHandle(process.hThread);
    process.hThread = nullptr;
    log.Write(L"PresentMon: started");
    return true;
}

PresentMonResult FinishPresentMon(Logger& log, PROCESS_INFORMATION& process,
    const std::filesystem::path& csv, bool terminate)
{
    if (!process.hProcess) return {};
    if (terminate && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT)
    {
        TerminateProcess(process.hProcess, 3);
    }
    if (WaitForSingleObject(process.hProcess, 10000) == WAIT_TIMEOUT)
    {
        TerminateProcess(process.hProcess, 4);
        WaitForSingleObject(process.hProcess, 5000);
    }

    PresentMonResult result{};
    GetExitCodeProcess(process.hProcess, &result.exitCode);
    result.csvCreated = std::filesystem::exists(csv) && std::filesystem::file_size(csv) > 0;
    log.Write(L"PresentMon Exit Code: " + std::to_wstring(result.exitCode));
    log.Write(std::wstring(L"Capture CSV Created: ") + (result.csvCreated ? L"YES" : L"NO"));
    CloseHandle(process.hProcess);
    process.hProcess = nullptr;
    return result;
}

bool RunAutomaticTest(Logger& log, PresentationOverlay& overlay, const TargetProcess& target)
{
    const auto csv = std::filesystem::absolute(log.CsvPath());
    overlay.SetVisible(false);
    LogHudState(log, overlay);

    PROCESS_INFORMATION presentMon{};
    if (!StartPresentMon(log, target, presentMon)) return false;

    try
    {
        log.Write(L"=== TEST START ===");
        log.Write(L"Wall Time: " + WallClock());
        log.Write(L"QPC_MS: " + std::to_wstring(QpcMilliseconds()));

        if (!SetTimer(overlay.Window(), kAutomaticTimerId, kAutomaticToggleMs, nullptr))
        {
            log.Write(L"TEST FAILED");
            log.Write(L"Reason: SetTimer failed");
            FinishPresentMon(log, presentMon, csv, true);
            return false;
        }

        log.Write(L"F8 ignored while automatic VRR test is running");
        const auto watchdog = std::chrono::steady_clock::now() + kCaptureWatchdog;
        bool success = true;
        bool cancelled = false;
        MSG message{};
        for (;;)
        {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
            if (message.message == WM_QUIT)
            {
                cancelled = true;
                success = false;
                break;
            }
            if (message.message == WM_HOTKEY && message.wParam == 2)
            {
                PostQuitMessage(0);
                continue;
            }
            if (message.message == WM_TIMER && message.hwnd == overlay.Window() &&
                message.wParam == kAutomaticTimerId)
            {
                if (!TargetAlive(target))
                {
                    log.Write(L"TEST FAILED");
                    log.Write(L"Reason: Target process exited");
                    success = false;
                    break;
                }
                overlay.SetVisible(!overlay.Visible());
                LogHudState(log, overlay);
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
            }
            if (!success) break;
            if (!TargetAlive(target))
            {
                log.Write(L"TEST FAILED");
                log.Write(L"Reason: Target process exited");
                success = false;
                break;
            }
            const DWORD presentMonWait = WaitForSingleObject(presentMon.hProcess, 0);
            if (presentMonWait == WAIT_OBJECT_0)
                break;
            if (presentMonWait == WAIT_FAILED || std::chrono::steady_clock::now() >= watchdog)
            {
                log.Write(L"TEST FAILED");
                log.Write(L"Reason: PresentMon capture watchdog expired");
                success = false;
                break;
            }
            Sleep(20);
        }
        KillTimer(overlay.Window(), kAutomaticTimerId);
        if (cancelled) log.Write(L"TEST CANCELLED");

        const PresentMonResult result = FinishPresentMon(log, presentMon, csv, !success);
        const bool targetAlive = TargetAlive(target);
        log.Write(std::wstring(L"Target still alive: ") + (targetAlive ? L"YES" : L"NO"));
        if (!targetAlive) success = false;
        if (result.exitCode != 0 || !result.csvCreated) success = false;

        log.Write(L"=== TEST END ===");
        log.Write(L"Wall Time: " + WallClock());
        log.Write(L"QPC_MS: " + std::to_wstring(QpcMilliseconds()));
        log.Write(L"VRR Analysis: NEEDS MANUAL REVIEW");
        log.Write(L"NOTE:");
        log.Write(L"PresentMon captures application presents and OS-visible display timing.");
        log.Write(L"Intel UMD XeFG-generated output frames may not all be observable.");
        log.Write(L"Do not treat PresentMon capture as authoritative true XeFG displayed FPS.");
        overlay.SetVisible(false);
        log.Write(L"Automatic test completed. PoC exiting.");
        return success;
    }
    catch (...)
    {
        KillTimer(overlay.Window(), kAutomaticTimerId);
        FinishPresentMon(log, presentMon, csv, true);
        throw;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    Logger log;
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        Fail(log, L"SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)",
            HRESULT_FROM_WIN32(GetLastError()));
    log.Write(L"ClawHUD VRR PoC");
    log.Write(L"OS: Windows 11 (required; no compatibility fallback)");
    log.Write(L"Test mode: PresentMon 60-second static HUD ON/OFF comparison");
    TargetProcess target{};
    if (!AcquireTarget(log, target)) return 1;
    try
    {
        PresentationOverlay overlay(log);
        overlay.Initialize();
        if (!RegisterHotKey(overlay.Window(), 2, MOD_NOREPEAT, VK_ESCAPE))
            log.Write(L"RegisterHotKey(ESC) unavailable; continuing without exit hotkey");
        const bool success = RunAutomaticTest(log, overlay, target);
        UnregisterHotKey(overlay.Window(), 2);
        CloseTarget(target);
        return success ? 0 : 1;
    }
    catch (const std::exception&)
    {
        CloseTarget(target);
        MessageBoxW(nullptr, L"ClawHUD VRR PoC initialization failed. See console/logs for HRESULT.",
            L"ClawHUD", MB_OK | MB_ICONERROR);
        return 1;
    }
}
