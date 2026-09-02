#include "RuntimeMessageWindow.h"

#include "App.h"
#include "RuntimeLogger.h"

namespace
{
constexpr wchar_t kRuntimeClassName[] = L"ClawHUD.RuntimeMessageWindow";
}

RuntimeMessageWindow::RuntimeMessageWindow(App& app) : app_(app)
{
}

RuntimeMessageWindow::~RuntimeMessageWindow()
{
    Destroy();
}

bool RuntimeMessageWindow::Create(HINSTANCE instance)
{
    instance_ = instance;

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kRuntimeClassName;
    RegisterClassW(&windowClass);

    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kRuntimeClassName, L"ClawHUD", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (!window_) return false;

    suspendResumeNotification_ = RegisterSuspendResumeNotification(
        window_, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!suspendResumeNotification_)
        clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn,
            L"Suspend/resume notification registration failed; recovery may be unavailable");

    return true;
}

void RuntimeMessageWindow::Destroy()
{
    if (suspendResumeNotification_)
    {
        UnregisterSuspendResumeNotification(suspendResumeNotification_);
        suspendResumeNotification_ = nullptr;
    }
    if (window_)
    {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

LRESULT CALLBACK RuntimeMessageWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<RuntimeMessageWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<RuntimeMessageWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_HOTKEY && wParam == kHudToggleHotkeyId)
    {
        self->app_.HandleHudToggleHotkey();
        return 0;
    }
    if (message == WM_POWERBROADCAST)
    {
        switch (wParam)
        {
        case PBT_APMSUSPEND:
            self->app_.HandleSystemSuspend();
            break;
        case PBT_APMRESUMEAUTOMATIC:
            self->app_.HandleSystemResume();
            break;
        case PBT_APMRESUMESUSPEND:
            break;
        default:
            break;
        }
        return TRUE;
    }
    if (message == WM_TIMER)
    {
        self->app_.HandleTimer(static_cast<UINT_PTR>(wParam));
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
