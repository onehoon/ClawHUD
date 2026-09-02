#include "TrayIcon.h"

#include "resource.h"

#include <shellapi.h>
#include <utility>

namespace
{
constexpr UINT kTrayMessage = WM_APP + 10;
constexpr UINT kSettingsCommand = 1001;
constexpr UINT kExitCommand = 1002;
constexpr wchar_t kTrayClassName[] = L"ClawHUD.TrayMessageWindow";
}

TrayIcon::TrayIcon(TrayActions actions) : actions_(std::move(actions))
{
}

TrayIcon::~TrayIcon()
{
    Destroy();
}

bool TrayIcon::Create(HINSTANCE instance)
{
    instance_ = instance;
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbarCreatedMessage_ == 0) return false;

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kTrayClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_CLAWHUD));
    RegisterClassW(&windowClass);

    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kTrayClassName, L"ClawHUD", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (!window_) return false;

    notifyIcon_.cbSize = sizeof(notifyIcon_);
    notifyIcon_.hWnd = window_;
    notifyIcon_.uID = 1;
    notifyIcon_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    notifyIcon_.uCallbackMessage = kTrayMessage;
    notifyIcon_.hIcon = windowClass.hIcon;
    wcscpy_s(notifyIcon_.szTip, L"ClawHUD");
    created_ = AddIcon();
    if (!created_) Destroy();
    return created_;
}

bool TrayIcon::AddIcon()
{
    created_ = Shell_NotifyIconW(NIM_ADD, &notifyIcon_) == TRUE;
    return created_;
}

void TrayIcon::Destroy()
{
    if (created_)
    {
        Shell_NotifyIconW(NIM_DELETE, &notifyIcon_);
        created_ = false;
    }
    if (window_)
    {
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

void TrayIcon::ShowMenu()
{
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"ClawHUD");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kSettingsCommand, L"Settings");
    AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
        point.x, point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    switch (command)
    {
    case kSettingsCommand: if (actions_.openSettings) actions_.openSettings(); break;
    case kExitCommand: if (actions_.exit) actions_.exit(); break;
    default: break;
    }
}

LRESULT CALLBACK TrayIcon::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TrayIcon*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    if (message == self->taskbarCreatedMessage_)
    {
        self->created_ = false;
        self->AddIcon();
        return 0;
    }
    if (message == kTrayMessage && lParam == WM_LBUTTONUP)
    {
        if (self->actions_.openSettings)
            self->actions_.openSettings();
        return 0;
    }
    if (message == kTrayMessage && lParam == WM_RBUTTONUP)
    {
        self->ShowMenu();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
