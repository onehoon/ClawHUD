#include "SettingsWindow.h"

#include "App.h"
#include "Version.h"

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
constexpr int kStartVrr = 1103;
constexpr int kStopVrr = 1104;
constexpr int kAlignmentLeft = 1201;
constexpr int kAlignmentCenter = 1202;
constexpr int kAlignmentRight = 1203;
constexpr int kBackgroundFull = 1204;
constexpr int kBackgroundContent = 1205;
constexpr int kOpacitySlider = 1206;
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
    UpdateHudControls();
    SetDiagnosticStatus(app_.EcStatus()); SetVrrStatus(app_.VrrStatus());
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
    generalPanel_ = CreateWindowW(L"STATIC", L"ClawHUD\r\nVersion: " CLAWHUD_VERSION,
        WS_CHILD | WS_VISIBLE, 24, 52, 440, 240, window_, nullptr, instance_, nullptr);
    hudPanel_ = CreateWindowW(L"STATIC", L"", WS_CHILD, 24, 52, 440, 240, window_, nullptr, instance_, nullptr);
    CreateWindowW(L"STATIC", L"Alignment", WS_CHILD | WS_VISIBLE,
        0, 0, 160, 22, hudPanel_, nullptr, instance_, nullptr);
    alignmentLeft_ = CreateWindowW(L"BUTTON", L"Left", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
        0, 28, 90, 24, hudPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAlignmentLeft)), instance_, nullptr);
    alignmentCenter_ = CreateWindowW(L"BUTTON", L"Center", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
        95, 28, 90, 24, hudPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAlignmentCenter)), instance_, nullptr);
    alignmentRight_ = CreateWindowW(L"BUTTON", L"Right", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
        190, 28, 90, 24, hudPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAlignmentRight)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Background Width", WS_CHILD | WS_VISIBLE,
        0, 70, 180, 22, hudPanel_, nullptr, instance_, nullptr);
    backgroundFull_ = CreateWindowW(L"BUTTON", L"Full Width", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
        0, 98, 110, 24, hudPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackgroundFull)), instance_, nullptr);
    backgroundContent_ = CreateWindowW(L"BUTTON", L"Content Width", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
        115, 98, 125, 24, hudPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackgroundContent)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Background Opacity", WS_CHILD | WS_VISIBLE,
        0, 140, 180, 22, hudPanel_, nullptr, instance_, nullptr);
    opacitySlider_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
        0, 168, 300, 32, hudPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpacitySlider)), instance_, nullptr);
    SendMessageW(opacitySlider_, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    opacityLabel_ = CreateWindowW(L"STATIC", L"50%", WS_CHILD | WS_VISIBLE,
        310, 172, 60, 22, hudPanel_, nullptr, instance_, nullptr);
    diagnosticsPanel_ = CreateWindowW(L"STATIC", L"", WS_CHILD, 24, 52, 440, 240, window_, nullptr, instance_, nullptr);
    CreateWindowW(L"STATIC", L"VRR / Presentation Test\r\nRuns HUD OFF / HUD ON phases for presentation validation.", WS_CHILD | WS_VISIBLE,
        0, 0, 420, 35, diagnosticsPanel_, nullptr, instance_, nullptr);
    startVrrButton_ = CreateWindowW(L"BUTTON", L"Start VRR Test", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 42, 130, 28, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartVrr)), instance_, nullptr);
    stopVrrButton_ = CreateWindowW(L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        145, 42, 80, 28, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopVrr)), instance_, nullptr);
    vrrStatus_ = CreateWindowW(L"STATIC", L"Status: Idle", WS_CHILD | WS_VISIBLE,
        0, 74, 420, 24, diagnosticsPanel_, nullptr, instance_, nullptr);
    CreateWindowW(L"STATIC", L"MSI EC Read Test\r\nReads MSI Claw telemetry without changing hardware state.", WS_CHILD | WS_VISIBLE,
        0, 106, 420, 35, diagnosticsPanel_, nullptr, instance_, nullptr);
    startEcButton_ = CreateWindowW(L"BUTTON", L"Start EC Test", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 147, 130, 28, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartEc)), instance_, nullptr);
    openLogsButton_ = CreateWindowW(L"BUTTON", L"Open Log Folder", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        145, 147, 135, 28, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenLogs)), instance_, nullptr);
    diagnosticStatus_ = CreateWindowW(L"STATIC", L"Status: Idle", WS_CHILD | WS_VISIBLE,
        0, 179, 420, 24, diagnosticsPanel_, nullptr, instance_, nullptr);
    ShowTab(kTabGeneral);
    UpdateHudControls();
}

void SettingsWindow::ShowTab(int index)
{
    if (generalPanel_) ShowWindow(generalPanel_, index == kTabGeneral ? SW_SHOW : SW_HIDE);
    if (hudPanel_) ShowWindow(hudPanel_, index == kTabHud ? SW_SHOW : SW_HIDE);
    if (diagnosticsPanel_) ShowWindow(diagnosticsPanel_, index == kTabDiagnostics ? SW_SHOW : SW_HIDE);
}

void SettingsWindow::UpdateHudControls()
{
    const auto& options = app_.HudOptions();
    if (alignmentLeft_) SendMessageW(alignmentLeft_, BM_SETCHECK,
        options.alignment == clawhud::HudAlignment::Left ? BST_CHECKED : BST_UNCHECKED, 0);
    if (alignmentCenter_) SendMessageW(alignmentCenter_, BM_SETCHECK,
        options.alignment == clawhud::HudAlignment::Center ? BST_CHECKED : BST_UNCHECKED, 0);
    if (alignmentRight_) SendMessageW(alignmentRight_, BM_SETCHECK,
        options.alignment == clawhud::HudAlignment::Right ? BST_CHECKED : BST_UNCHECKED, 0);
    if (backgroundFull_) SendMessageW(backgroundFull_, BM_SETCHECK,
        options.backgroundMode == clawhud::HudBackgroundMode::FullWidth ? BST_CHECKED : BST_UNCHECKED, 0);
    if (backgroundContent_) SendMessageW(backgroundContent_, BM_SETCHECK,
        options.backgroundMode == clawhud::HudBackgroundMode::ContentWidth ? BST_CHECKED : BST_UNCHECKED, 0);
    const int percent = static_cast<int>(options.backgroundOpacity * 100.0f + 0.5f);
    if (opacitySlider_) SendMessageW(opacitySlider_, TBM_SETPOS, TRUE, percent);
    if (opacityLabel_)
    {
        wchar_t text[8]{};
        swprintf_s(text, L"%d%%", percent);
        SetWindowTextW(opacityLabel_, text);
    }
}

void SettingsWindow::SetDiagnosticStatus(const std::wstring& status)
{
    if (diagnosticStatus_) SetWindowTextW(diagnosticStatus_, (L"Status: " + status).c_str());
    UpdateDiagnosticButtons();
}

void SettingsWindow::SetVrrStatus(const std::wstring& status)
{
    if (vrrStatus_) SetWindowTextW(vrrStatus_, (L"Status: " + status).c_str());
    UpdateDiagnosticButtons();
}

void SettingsWindow::UpdateDiagnosticButtons()
{
    const bool busy = app_.DiagnosticRunning();
    if (startEcButton_) EnableWindow(startEcButton_, !busy);
    if (startVrrButton_) EnableWindow(startVrrButton_, !busy);
    if (stopVrrButton_) EnableWindow(stopVrrButton_, app_.VrrDiagnosticRunning());
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
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED)
    {
        switch (LOWORD(wParam))
        {
        case kAlignmentLeft: self->app_.SetHudAlignment(clawhud::HudAlignment::Left); return 0;
        case kAlignmentCenter: self->app_.SetHudAlignment(clawhud::HudAlignment::Center); return 0;
        case kAlignmentRight: self->app_.SetHudAlignment(clawhud::HudAlignment::Right); return 0;
        case kBackgroundFull: self->app_.SetHudBackgroundMode(clawhud::HudBackgroundMode::FullWidth); return 0;
        case kBackgroundContent: self->app_.SetHudBackgroundMode(clawhud::HudBackgroundMode::ContentWidth); return 0;
        default: break;
        }
    }
    if (message == WM_HSCROLL && reinterpret_cast<HWND>(lParam) == self->opacitySlider_)
    {
        const int position = static_cast<int>(SendMessageW(self->opacitySlider_, TBM_GETPOS, 0, 0));
        const bool persist = LOWORD(wParam) != TB_THUMBTRACK;
        self->app_.SetHudBackgroundOpacity(position / 100.0f, persist);
        self->UpdateHudControls();
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == kStartEc)
    {
        if (self->app_.StartEcDiagnostic()) self->SetDiagnosticStatus(L"Running"); return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == kStartVrr)
    {
        if (self->app_.StartVrrDiagnostic()) self->SetVrrStatus(L"Waiting for game"); return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == kStopVrr)
    {
        self->app_.StopVrrDiagnostic(); return 0;
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
