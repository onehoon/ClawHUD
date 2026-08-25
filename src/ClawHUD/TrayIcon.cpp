#include "TrayIcon.h"

#include "App.h"

#include <shellapi.h>

namespace
{
constexpr UINT kTrayMessage = WM_APP + 10;
constexpr UINT kSettingsCommand = 1001;
constexpr UINT kExitCommand = 1002;
constexpr UINT kStopDiagnosticCommand = 1003;
constexpr UINT kShowMockHudCommand = 1004;
constexpr UINT kHideMockHudCommand = 1005;
constexpr wchar_t kTrayClassName[] = L"ClawHUD.TrayMessageWindow";
}

TrayIcon::TrayIcon(App& app) : app_(app)
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
    notifyIcon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
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
    if (app_.DiagnosticRunning()) AppendMenuW(menu, MF_STRING, kStopDiagnosticCommand, L"Stop Diagnostic Test");
    AppendMenuW(menu, MF_STRING, app_.MockHudVisible() ? kHideMockHudCommand : kShowMockHudCommand,
        app_.MockHudVisible() ? L"Hide Mock HUD" : L"Show Mock HUD");
    AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
        point.x, point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    if (command == kSettingsCommand) app_.OpenSettings();
    if (command == kExitCommand) app_.Exit();
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
    if (message == kTrayMessage && (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP))
    {
        self->ShowMenu();
        return 0;
    }
    if (message == WM_COMMAND)
    {
        if (LOWORD(wParam) == kSettingsCommand) self->app_.OpenSettings();
        if (LOWORD(wParam) == kExitCommand) self->app_.Exit();
        if (LOWORD(wParam) == kStopDiagnosticCommand) self->app_.StopDiagnostic();
        if (LOWORD(wParam) == kShowMockHudCommand) self->app_.StartMockHud();
        if (LOWORD(wParam) == kHideMockHudCommand) self->app_.StopMockHud();
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
