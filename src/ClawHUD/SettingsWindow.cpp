#include "SettingsWindow.h"

#include "App.h"

#include <commctrl.h>

namespace
{
constexpr wchar_t kSettingsClassName[] = L"ClawHUD.SettingsWindow";
constexpr int kTabGeneral = 0;
constexpr int kTabHud = 1;
constexpr int kTabDiagnostics = 2;
constexpr int kTabCount = 3;
constexpr int kStartEc = 1101;
constexpr int kOpenLogs = 1102;
}

SettingsWindow::SettingsWindow(App& app) : app_(app)
{
}

SettingsWindow::~SettingsWindow()
{
    if (window_) DestroyWindow(window_);
}

bool SettingsWindow::Show(HINSTANCE instance)
{
    instance_ = instance;
    if (!window_)
    {
        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_TAB_CLASSES };
        InitCommonControlsEx(&controls);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kSettingsClassName;
        RegisterClassW(&windowClass);
        window_ = CreateWindowExW(0, kSettingsClassName, L"ClawHUD Settings",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, 520, 360, nullptr, nullptr, instance_, this);
        if (!window_) return false;
        CreateTabs();
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    SetDiagnosticStatus(app_.EcStatus());
    return true;
}

void SettingsWindow::CreateTabs()
{
    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        8, 8, 488, 300, window_, nullptr, instance_, nullptr);
    const wchar_t* labels[kTabCount] = { L"General", L"HUD", L"Diagnostics" };
    for (int i = 0; i < kTabCount; ++i)
    {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(labels[i]);
        TabCtrl_InsertItem(tabs_, i, &item);
    }
    CreateWindowW(L"STATIC", L"ClawHUD\r\nVersion: 0.1.0", WS_CHILD | WS_VISIBLE,
        24, 52, 440, 100, window_, nullptr, instance_, nullptr);
    diagnosticsPanel_ = CreateWindowW(L"STATIC", L"", WS_CHILD, 24, 52, 440, 190, window_, nullptr, instance_, nullptr);
    CreateWindowW(L"STATIC", L"MSI EC Read Test\r\nReads MSI Claw telemetry without changing hardware state.", WS_CHILD | WS_VISIBLE,
        0, 0, 420, 45, diagnosticsPanel_, nullptr, instance_, nullptr);
    startEcButton_ = CreateWindowW(L"BUTTON", L"Start EC Test", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 58, 130, 28, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartEc)), instance_, nullptr);
    openLogsButton_ = CreateWindowW(L"BUTTON", L"Open Log Folder", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        145, 58, 135, 28, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenLogs)), instance_, nullptr);
    diagnosticStatus_ = CreateWindowW(L"STATIC", L"Status: Idle", WS_CHILD | WS_VISIBLE,
        0, 100, 420, 24, diagnosticsPanel_, nullptr, instance_, nullptr);
    ShowTab(kTabGeneral);
}

void SettingsWindow::ShowTab(int index)
{
    if (diagnosticsPanel_) ShowWindow(diagnosticsPanel_, index == kTabDiagnostics ? SW_SHOW : SW_HIDE);
}

void SettingsWindow::SetDiagnosticStatus(const std::wstring& status)
{
    if (diagnosticStatus_) SetWindowTextW(diagnosticStatus_, (L"Status: " + status).c_str());
    if (startEcButton_) EnableWindow(startEcButton_, status != L"Running");
}

LRESULT CALLBACK SettingsWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    if (message == WM_COMMAND && LOWORD(wParam) == kStartEc)
    {
        if (self->app_.StartEcDiagnostic()) self->SetDiagnosticStatus(L"Running"); return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == kOpenLogs)
    {
        self->app_.OpenDiagnosticLogFolder(); return 0;
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE)
    {
        self->ShowTab(TabCtrl_GetCurSel(self->tabs_)); return 0;
    }
    if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_NCDESTROY)
    {
        self->window_ = nullptr;
        self->tabs_ = nullptr;
        PostMessageW(self->app_.MessageWindow(), WM_APP + 1, 0, 0);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
