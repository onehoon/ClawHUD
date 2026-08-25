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
constexpr UINT kTrackMockGameCommand = 1006;
constexpr UINT kInGameOnlyCommand = 1007;
constexpr UINT kAlwaysCommand = 1008;
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
    const HWND previousForeground = GetForegroundWindow();
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"ClawHUD");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kSettingsCommand, L"Settings");
    if (app_.DiagnosticRunning()) AppendMenuW(menu, MF_STRING, kStopDiagnosticCommand, L"Stop Diagnostic Test");
    AppendMenuW(menu, MF_STRING, app_.MockHudVisible() ? kHideMockHudCommand : kShowMockHudCommand,
        app_.MockHudVisible() ? L"Hide Mock HUD" : L"Show Mock HUD");
    AppendMenuW(menu, MF_STRING, kTrackMockGameCommand, L"Track Foreground as Mock Game");
    AppendMenuW(menu, MF_STRING, kInGameOnlyCommand, L"HUD: In Game Only");
    AppendMenuW(menu, MF_STRING, kAlwaysCommand, L"HUD: Always");
    AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
        point.x, point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    switch (command)
    {
    case kSettingsCommand: app_.OpenSettings(); break;
    case kStopDiagnosticCommand: app_.StopDiagnostic(); break;
    case kShowMockHudCommand: app_.StartMockHud(); break;
    case kHideMockHudCommand: app_.StopMockHud(); break;
    case kTrackMockGameCommand: app_.TrackMockGameWindow(previousForeground); break;
    case kInGameOnlyCommand: app_.SetHudVisibilityMode(clawhud::HudVisibilityMode::InGameOnly); break;
    case kAlwaysCommand: app_.SetHudVisibilityMode(clawhud::HudVisibilityMode::Always); break;
    case kExitCommand: app_.Exit(); break;
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
    if (message == kTrayMessage && (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP))
    {
        self->ShowMenu();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
