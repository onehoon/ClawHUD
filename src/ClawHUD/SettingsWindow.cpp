#include "SettingsWindow.h"

#include "App.h"
#include "HudSize.h"
#include "Version.h"
#include "resource.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

namespace
{
constexpr wchar_t kSettingsClassName[] = L"ClawHUD.SettingsWindow";
constexpr int kTabSettings = 0;
constexpr int kTabTweaks = 1;
constexpr int kTabAbout = 2;
constexpr int kTabDiagnostics = 3;
constexpr int kTabCount = 4;
constexpr int kStartWithWindows = 1001;
constexpr int kStartEc = 1101;
constexpr int kStartIgcl = 1105;
constexpr int kOpenLogs = 1102;
constexpr int kStartVrr = 1103;
constexpr int kStopVrr = 1104;
constexpr int kAlignmentLeft = 1201;
constexpr int kAlignmentCenter = 1202;
constexpr int kAlignmentRight = 1203;
constexpr int kBackgroundFull = 1204;
constexpr int kBackgroundContent = 1205;
constexpr int kOpacitySlider = 1206;
constexpr int kIntelVrrToggle = 1301;
constexpr int kEnableHud = 1207;
constexpr int kVisibilityAlways = 1208;
constexpr int kVisibilityInGameOnly = 1209;
constexpr int kHudSizeMinus = 1210;
constexpr int kHudSizePlus = 1211;
constexpr int kFontUnispace = 1212;
constexpr int kFontSegoeUiVariable = 1213;
constexpr int kGeneralHeading = 2001;
constexpr int kHudHeading = 2002;
constexpr int kVisibilityLabel = 2003;
constexpr int kHudSizeLabel = 2004;
constexpr int kFontLabel = 2005;
constexpr int kAlignmentLabel = 2006;
constexpr int kBackgroundWidthLabel = 2007;
constexpr int kOpacityLabel = 2008;
constexpr int kTweaksHeading = 2101;
constexpr int kTweaksDescription = 2102;
constexpr int kDiagnosticsVrrHeading = 2201;
constexpr int kDiagnosticsVrrDescription = 2202;
constexpr int kDiagnosticsEcHeading = 2203;
constexpr int kDiagnosticsEcDescription = 2204;
constexpr int kDiagnosticsIgclHeading = 2205;
constexpr int kDiagnosticsIgclDescription = 2206;
constexpr int kAboutTitle = 2301;
constexpr int kAboutDescription = 2302;
constexpr int kAboutVersion = 2303;
constexpr int kAboutHowToUse = 2304;
constexpr int kAboutInstructions = 2305;
constexpr int kWheelStep = 48;

LRESULT CALLBACK ForwardPanelNotifications(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR)
{
    if (message == WM_COMMAND || message == WM_HSCROLL)
        return SendMessageW(GetParent(window), message, wParam, lParam);
    if (message == WM_MOUSEWHEEL)
        return SendMessageW(GetParent(window), message, wParam, lParam);
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, ForwardPanelNotifications, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK ForwardPanGesture(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR)
{
    if (message == WM_GESTURE)
    {
        GESTUREINFO info{};
        info.cbSize = sizeof(info);
        const HGESTUREINFO gesture = reinterpret_cast<HGESTUREINFO>(lParam);
        if (!GetGestureInfo(gesture, &info) || info.dwID != GID_PAN)
            return DefSubclassProc(window, message, wParam, lParam);

        if (HWND root = GetAncestor(window, GA_ROOT))
            return SendMessageW(root, message, wParam, lParam);
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, ForwardPanGesture, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

void ConfigureVerticalPan(HWND window)
{
    if (!window)
        return;

    GESTURECONFIG config{};
    config.dwID = GID_PAN;
    config.dwWant = GC_PAN |
        GC_PAN_WITH_SINGLE_FINGER_VERTICALLY |
        GC_PAN_WITH_INERTIA;
    config.dwBlock = GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY;
    SetGestureConfig(window, 0, 1, &config, sizeof(config));
}

void MoveControl(HWND parent, int id, int x, int y, int width, int height)
{
    if (HWND control = GetDlgItem(parent, id))
        MoveWindow(control, x, y, width, height, TRUE);
}

void EnableMouseWheelForwarding(HWND control)
{
    if (control)
        SetWindowSubclass(control, ForwardPanelNotifications, 4, 0);
}

void EnableStaticPanForwarding(HWND panel)
{
    if (!panel)
        return;

    ConfigureVerticalPan(panel);
    SetWindowSubclass(panel, ForwardPanGesture, 5, 0);
    EnumChildWindows(panel, [](HWND child, LPARAM) -> BOOL
    {
        wchar_t className[32]{};
        GetClassNameW(child, className, _countof(className));
        if (_wcsicmp(className, L"Static") == 0)
        {
            ConfigureVerticalPan(child);
            SetWindowSubclass(child, ForwardPanGesture, 5, 0);
        }
        return TRUE;
    }, 0);
}
}

SettingsWindow::SettingsWindow(App& app) : app_(app)
{
}

SettingsWindow::~SettingsWindow()
{
    if (window_) DestroyWindow(window_);
    if (uiFont_) DeleteObject(uiFont_);
    if (headingFont_) DeleteObject(headingFont_);
}

bool SettingsWindow::Show(HINSTANCE instance)
{
    instance_ = instance;
    if (!window_)
    {
        INITCOMMONCONTROLSEX controls{
            sizeof(controls), ICC_TAB_CLASSES | ICC_BAR_CLASSES };
        if (!InitCommonControlsEx(&controls)) return false;
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_CLAWHUD));
        windowClass.lpszClassName = kSettingsClassName;
        RegisterClassW(&windowClass);
        dpi_ = GetDpiForSystem();
        if (dpi_ == 0) dpi_ = 96;
        window_ = CreateWindowExW(0, kSettingsClassName, L"ClawHUD Settings",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(680), Scale(600), nullptr, nullptr, instance_, this);
        if (!window_) return false;
        dpi_ = GetDpiForWindow(window_);
        if (dpi_ == 0) dpi_ = 96;
        ApplyWindowStyle();
        ConfigureVerticalPan(window_);
        CreateTabs();
        RecreateFont();
        ApplyFont();
        Layout();
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    UpdateHudControls();
    SetDiagnosticStatus(app_.IgclStatus() == L"Idle" ? app_.EcStatus() : app_.IgclStatus()); SetVrrStatus(app_.VrrStatus());
    UpdateTweaksControls();
    return true;
}

void SettingsWindow::ApplyWindowStyle()
{
    if (!window_) return;
    const DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
    systemBackdropActive_ = SUCCEEDED(DwmSetWindowAttribute(
        window_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop)));
    const COLORREF borderColor = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(window_, DWMWA_BORDER_COLOR, &borderColor,
        sizeof(borderColor));
}

void SettingsWindow::RecreateFont()
{
    if (uiFont_)
    {
        DeleteObject(uiFont_);
        uiFont_ = nullptr;
    }
    if (headingFont_)
    {
        DeleteObject(headingFont_);
        headingFont_ = nullptr;
    }

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoForDpi(
            SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_))
    {
        uiFont_ = CreateFontIndirectW(&metrics.lfMessageFont);
    }
    if (uiFont_)
    {
        LOGFONTW heading{};
        if (GetObjectW(uiFont_, static_cast<int>(sizeof(heading)), &heading) ==
            static_cast<int>(sizeof(heading)))
        {
            heading.lfWeight = FW_SEMIBOLD;
            headingFont_ = CreateFontIndirectW(&heading);
        }
    }
}

void SettingsWindow::ApplyFont()
{
    if (!window_ || !uiFont_) return;
    EnumChildWindows(window_, [](HWND child, LPARAM parameter) -> BOOL
    {
        SendMessageW(child, WM_SETFONT, parameter, TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(uiFont_));
    ApplyHeadingFont();
}

void SettingsWindow::ApplyHeadingFont()
{
    if (!headingFont_)
        return;

    const auto apply = [this](HWND panel, int id)
    {
        if (HWND control = GetDlgItem(panel, id))
            SendMessageW(control, WM_SETFONT,
                reinterpret_cast<WPARAM>(headingFont_), TRUE);
    };

    apply(settingsPanel_, kGeneralHeading);
    apply(settingsPanel_, kHudHeading);
    apply(tweaksPanel_, kTweaksHeading);
    apply(aboutPanel_, kAboutTitle);
    apply(aboutPanel_, kAboutHowToUse);
    apply(diagnosticsPanel_, kDiagnosticsVrrHeading);
    apply(diagnosticsPanel_, kDiagnosticsEcHeading);
}

int SettingsWindow::Scale(int value) const noexcept
{
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

int SettingsWindow::ActiveTab() const noexcept
{
    const int tab = tabs_ ? TabCtrl_GetCurSel(tabs_) : kTabSettings;
    return tab >= kTabSettings && tab <= kTabDiagnostics ? tab : kTabSettings;
}

int SettingsWindow::ContentHeightForTab(int tab) const noexcept
{
    switch (tab)
    {
    case kTabSettings: return 454;
    case kTabTweaks: return 230;
    case kTabAbout: return 260;
    case kTabDiagnostics: return 500;
    default: return 0;
    }
}

int SettingsWindow::ViewportHeight() const noexcept
{
    if (!window_) return 0;
    RECT client{};
    GetClientRect(window_, &client);
    const int physicalHeight = std::max(0,
        static_cast<int>(client.bottom) - Scale(64));
    return MulDiv(physicalHeight, 96, static_cast<int>(dpi_));
}

int& SettingsWindow::ScrollOffsetForTab(int tab) noexcept
{
    switch (tab)
    {
    case kTabTweaks: return tweaksScrollY_;
    case kTabAbout: return aboutScrollY_;
    case kTabDiagnostics: return diagnosticsScrollY_;
    case kTabSettings:
    default: return settingsScrollY_;
    }
}

void SettingsWindow::ClampScrollOffsets()
{
    for (int tab = kTabSettings; tab <= kTabDiagnostics; ++tab)
    {
        int& offset = ScrollOffsetForTab(tab);
        const int maxScroll = std::max(0,
            ContentHeightForTab(tab) - ViewportHeight());
        offset = std::clamp(offset, 0, maxScroll);
    }
}

void SettingsWindow::ScrollActivePanel(int delta)
{
    if (delta == 0) return;
    int& offset = ScrollOffsetForTab(ActiveTab());
    offset += delta;
    ClampScrollOffsets();
    ApplyScrollPosition();
}

void SettingsWindow::ApplyScrollPosition()
{
    LayoutSettings();
    LayoutTweaks();
    LayoutAbout();
    LayoutDiagnostics();
}

void SettingsWindow::CreateTabs()
{
    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    const wchar_t* labels[kTabCount] = { L"Settings", L"Tweaks", L"About", L"Diagnostics" };
    const int tabCount = app_.DiagnosticsTabEnabled() ? kTabCount : kTabCount - 1;
    for (int i = 0; i < tabCount; ++i)
    {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(labels[i]);
        TabCtrl_InsertItem(tabs_, i, &item);
    }
    CreateSettingsControls();
    CreateTweaksControls();
    CreateAboutControls();
    if (app_.DiagnosticsTabEnabled())
        CreateDiagnosticsControls();
    ShowTab(kTabSettings);
    UpdateGeneralControls();
    UpdateHudControls();
}

void SettingsWindow::CreateSettingsControls()
{
    settingsPanel_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    if (settingsPanel_) SetWindowSubclass(settingsPanel_, ForwardPanelNotifications, 3, 0);
    CreateWindowW(L"STATIC", L"General", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGeneralHeading)), instance_, nullptr);
    startWithWindows_ = CreateWindowW(L"BUTTON", L"Start ClawHUD with Windows",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartWithWindows)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"HUD", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHudHeading)), instance_, nullptr);
    enableHud_ = CreateWindowW(L"BUTTON", L"Enable HUD",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEnableHud)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Display mode", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kVisibilityLabel)), instance_, nullptr);
    visibilityInGameOnly_ = CreateWindowW(L"BUTTON", L"In-game only",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_GROUP, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kVisibilityInGameOnly)), instance_, nullptr);
    visibilityAlways_ = CreateWindowW(L"BUTTON", L"Always on",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kVisibilityAlways)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"HUD size", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHudSizeLabel)), instance_, nullptr);
    hudSizeMinus_ = CreateWindowW(L"BUTTON", L"-", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHudSizeMinus)), instance_, nullptr);
    hudSizeValue_ = CreateWindowW(L"STATIC", L"Default", WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 0, 0, 0, settingsPanel_, nullptr, instance_, nullptr);
    hudSizePlus_ = CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHudSizePlus)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Font", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontLabel)), instance_, nullptr);
    fontUnispace_ = CreateWindowW(L"BUTTON", L"Unispace",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_GROUP,
        0, 0, 0, 0, settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontUnispace)), instance_, nullptr);
    fontSegoeUiVariable_ = CreateWindowW(L"BUTTON", L"Segoe UI Variable",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE,
        0, 0, 0, 0, settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontSegoeUiVariable)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Alignment", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAlignmentLabel)), instance_, nullptr);
    alignmentLeft_ = CreateWindowW(L"BUTTON", L"Left",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_GROUP, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAlignmentLeft)), instance_, nullptr);
    alignmentCenter_ = CreateWindowW(L"BUTTON", L"Center",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAlignmentCenter)), instance_, nullptr);
    alignmentRight_ = CreateWindowW(L"BUTTON", L"Right",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAlignmentRight)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Background width", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackgroundWidthLabel)), instance_, nullptr);
    backgroundFull_ = CreateWindowW(L"BUTTON", L"Full width",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_GROUP, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackgroundFull)), instance_, nullptr);
    backgroundContent_ = CreateWindowW(L"BUTTON", L"Content width",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackgroundContent)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Background opacity", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpacityLabel)), instance_, nullptr);
    opacitySlider_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_NOTICKS, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpacitySlider)), instance_, nullptr);
    SendMessageW(opacitySlider_, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    opacityLabel_ = CreateWindowW(L"STATIC", L"50%", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, settingsPanel_, nullptr, instance_, nullptr);
    EnableMouseWheelForwarding(startWithWindows_);
    EnableMouseWheelForwarding(enableHud_);
    EnableMouseWheelForwarding(visibilityInGameOnly_);
    EnableMouseWheelForwarding(visibilityAlways_);
    EnableMouseWheelForwarding(hudSizeMinus_);
    EnableMouseWheelForwarding(hudSizePlus_);
    EnableMouseWheelForwarding(fontUnispace_);
    EnableMouseWheelForwarding(fontSegoeUiVariable_);
    EnableMouseWheelForwarding(alignmentLeft_);
    EnableMouseWheelForwarding(alignmentCenter_);
    EnableMouseWheelForwarding(alignmentRight_);
    EnableMouseWheelForwarding(backgroundFull_);
    EnableMouseWheelForwarding(backgroundContent_);
    EnableMouseWheelForwarding(opacitySlider_);
    EnableStaticPanForwarding(settingsPanel_);
}

void SettingsWindow::CreateTweaksControls()
{
    tweaksPanel_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    if (tweaksPanel_) SetWindowSubclass(tweaksPanel_, ForwardPanelNotifications, 3, 0);
    CreateWindowW(L"STATIC", L"Intel VRR Range Fix", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        tweaksPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTweaksHeading)), instance_, nullptr);
    intelVrrToggle_ = CreateWindowW(L"BUTTON", L"Enable Intel VRR Range Fix",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0,
        tweaksPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIntelVrrToggle)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Restores the native VRR range on the affected\r\nMSI Claw display. Applied at application startup.",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, tweaksPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTweaksDescription)), instance_, nullptr);
    intelVrrPanel_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, tweaksPanel_, nullptr, instance_, nullptr);
    intelVrrRange_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, tweaksPanel_, nullptr, instance_, nullptr);
    intelVrrResult_ = CreateWindowW(L"STATIC", L"Last result: No result yet", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, tweaksPanel_, nullptr, instance_, nullptr);
    EnableMouseWheelForwarding(intelVrrToggle_);
    EnableStaticPanForwarding(tweaksPanel_);
}

void SettingsWindow::CreateAboutControls()
{
    aboutPanel_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    aboutIcon_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ICON,
        0, 0, 0, 0, aboutPanel_, nullptr, instance_, nullptr);
    SendMessageW(aboutIcon_, STM_SETIMAGE, IMAGE_ICON,
        reinterpret_cast<LPARAM>(LoadIconW(instance_, MAKEINTRESOURCEW(IDI_CLAWHUD))));
    CreateWindowW(L"STATIC", L"ClawHUD", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        aboutPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAboutTitle)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Lightweight performance HUD for MSI Claw", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, aboutPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAboutDescription)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Version: " CLAWHUD_VERSION, WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, aboutPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAboutVersion)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"How to use", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, aboutPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAboutHowToUse)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"ClawHUD runs from the system tray.\r\nConfigure HUD behavior from the Settings tab.\r\nF8 toggles HUD visibility.",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, aboutPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAboutInstructions)), instance_, nullptr);
    EnableStaticPanForwarding(aboutPanel_);
}

void SettingsWindow::CreateDiagnosticsControls()
{
    diagnosticsPanel_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    if (diagnosticsPanel_) SetWindowSubclass(diagnosticsPanel_, ForwardPanelNotifications, 2, 0);
    CreateWindowW(L"STATIC", L"VRR / Presentation Test", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDiagnosticsVrrHeading)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Start closes Settings. Return to the game and press F8.\r\nA ding confirms the target, then OFF -> STATIC -> DYNAMIC (~28 sec each).\r\nA second ding confirms successful completion.",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, diagnosticsPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDiagnosticsVrrDescription)), instance_, nullptr);
    startVrrButton_ = CreateWindowW(L"BUTTON", L"Start VRR Test",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, diagnosticsPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartVrr)), instance_, nullptr);
    stopVrrButton_ = CreateWindowW(L"BUTTON", L"Stop",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, diagnosticsPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopVrr)), instance_, nullptr);
    vrrStatus_ = CreateWindowW(L"STATIC", L"Status: Idle", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, diagnosticsPanel_, nullptr, instance_, nullptr);
    CreateWindowW(L"STATIC", L"MSI EC Read Test", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDiagnosticsEcHeading)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Reads MSI Claw telemetry without changing\r\nhardware state.", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDiagnosticsEcDescription)), instance_, nullptr);
    startEcButton_ = CreateWindowW(L"BUTTON", L"Start EC Test",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, diagnosticsPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartEc)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"IGCL Read-only Capability Test", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDiagnosticsIgclHeading)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Uses the driver-installed ControlLib.dll. Closes Settings, waits 5 seconds, then records read-only IGCL capabilities and telemetry for approximately 5 seconds.", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDiagnosticsIgclDescription)), instance_, nullptr);
    startIgclButton_ = CreateWindowW(L"BUTTON", L"Start IGCL Test",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, diagnosticsPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartIgcl)), instance_, nullptr);
    openLogsButton_ = CreateWindowW(L"BUTTON", L"Open Log Folder",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, diagnosticsPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenLogs)), instance_, nullptr);
    diagnosticStatus_ = CreateWindowW(L"STATIC", L"Status: Idle", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, diagnosticsPanel_, nullptr, instance_, nullptr);
    EnableMouseWheelForwarding(startVrrButton_);
    EnableMouseWheelForwarding(stopVrrButton_);
    EnableMouseWheelForwarding(startEcButton_);
    EnableMouseWheelForwarding(startIgclButton_);
    EnableMouseWheelForwarding(openLogsButton_);
    EnableStaticPanForwarding(diagnosticsPanel_);
}

void SettingsWindow::Layout()
{
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    MoveWindow(tabs_, 0, 0,
        std::max(0, static_cast<int>(client.right)),
        std::max(0, static_cast<int>(client.bottom)), TRUE);
    const int panelX = Scale(24);
    const int panelY = Scale(52);
    const int panelWidth = std::max(0, static_cast<int>(client.right) - Scale(48));
    const int panelHeight = std::max(0, static_cast<int>(client.bottom) - Scale(64));
    MoveWindow(settingsPanel_, panelX, panelY, panelWidth, panelHeight, TRUE);
    MoveWindow(tweaksPanel_, panelX, panelY, panelWidth, panelHeight, TRUE);
    MoveWindow(aboutPanel_, panelX, panelY, panelWidth, panelHeight, TRUE);
    if (diagnosticsPanel_)
        MoveWindow(diagnosticsPanel_, panelX, panelY, panelWidth, panelHeight, TRUE);
    ClampScrollOffsets();
    ApplyScrollPosition();
}

void SettingsWindow::LayoutSettings()
{
    if (!settingsPanel_) return;
    const int labelX = Scale(24);
    const int controlX = Scale(220);
    const int labelWidth = Scale(180);
    const int labelHeight = Scale(28);
    const int checkboxHeight = Scale(30);
    const int optionHeight = Scale(32);
    const int scrollY = Scale(settingsScrollY_);
    MoveControl(settingsPanel_, kGeneralHeading, labelX, Scale(8) - scrollY, labelWidth, labelHeight);
    MoveWindow(startWithWindows_, labelX, Scale(40) - scrollY, Scale(260), checkboxHeight, TRUE);
    MoveControl(settingsPanel_, kHudHeading, labelX, Scale(86) - scrollY, labelWidth, labelHeight);
    MoveWindow(enableHud_, labelX, Scale(118) - scrollY, Scale(220), checkboxHeight, TRUE);
    MoveControl(settingsPanel_, kVisibilityLabel, labelX, Scale(166) - scrollY, labelWidth, labelHeight);
    MoveWindow(visibilityInGameOnly_, controlX, Scale(162) - scrollY, Scale(130), optionHeight, TRUE);
    MoveWindow(visibilityAlways_, controlX + Scale(138), Scale(162) - scrollY, Scale(100), optionHeight, TRUE);
    MoveControl(settingsPanel_, kHudSizeLabel, labelX, Scale(210) - scrollY, labelWidth, labelHeight);
    MoveWindow(hudSizeMinus_, controlX, Scale(206) - scrollY, Scale(44), optionHeight, TRUE);
    MoveWindow(hudSizeValue_, controlX + Scale(52), Scale(210) - scrollY, Scale(72), labelHeight, TRUE);
    MoveWindow(hudSizePlus_, controlX + Scale(132), Scale(206) - scrollY, Scale(44), optionHeight, TRUE);
    MoveControl(settingsPanel_, kFontLabel, labelX, Scale(254) - scrollY, labelWidth, labelHeight);
    MoveWindow(fontUnispace_, controlX, Scale(250) - scrollY, Scale(120), optionHeight, TRUE);
    MoveWindow(fontSegoeUiVariable_, controlX + Scale(128), Scale(250) - scrollY, Scale(165), optionHeight, TRUE);
    MoveControl(settingsPanel_, kAlignmentLabel, labelX, Scale(298) - scrollY, labelWidth, labelHeight);
    MoveWindow(alignmentLeft_, controlX, Scale(294) - scrollY, Scale(90), optionHeight, TRUE);
    MoveWindow(alignmentCenter_, controlX + Scale(98), Scale(294) - scrollY, Scale(90), optionHeight, TRUE);
    MoveWindow(alignmentRight_, controlX + Scale(196), Scale(294) - scrollY, Scale(90), optionHeight, TRUE);
    MoveControl(settingsPanel_, kBackgroundWidthLabel, labelX, Scale(342) - scrollY, labelWidth, labelHeight);
    MoveWindow(backgroundFull_, controlX, Scale(338) - scrollY, Scale(110), optionHeight, TRUE);
    MoveWindow(backgroundContent_, controlX + Scale(120), Scale(338) - scrollY, Scale(130), optionHeight, TRUE);
    MoveControl(settingsPanel_, kOpacityLabel, labelX, Scale(386) - scrollY, labelWidth, labelHeight);
    MoveWindow(opacitySlider_, controlX, Scale(382) - scrollY, Scale(260), optionHeight, TRUE);
    MoveWindow(opacityLabel_, controlX + Scale(270), Scale(386) - scrollY, Scale(60), labelHeight, TRUE);
}

void SettingsWindow::LayoutTweaks()
{
    if (!tweaksPanel_) return;
    const int x = Scale(24);
    RECT panelRect{};
    GetClientRect(tweaksPanel_, &panelRect);
    const int width = std::max(0, static_cast<int>(panelRect.right) - x - Scale(24));
    const int scrollY = Scale(tweaksScrollY_);
    MoveControl(tweaksPanel_, kTweaksHeading, x, Scale(8) - scrollY, width, Scale(28));
    MoveWindow(intelVrrToggle_, x, Scale(44) - scrollY, Scale(300), Scale(32), TRUE);
    MoveControl(tweaksPanel_, kTweaksDescription, x, Scale(82) - scrollY, width, Scale(44));
    MoveWindow(intelVrrPanel_, x, Scale(140) - scrollY, width, Scale(24), TRUE);
    MoveWindow(intelVrrRange_, x, Scale(168) - scrollY, width, Scale(24), TRUE);
    MoveWindow(intelVrrResult_, x, Scale(196) - scrollY, width, Scale(24), TRUE);
}

void SettingsWindow::LayoutAbout()
{
    if (!aboutPanel_) return;
    const int x = Scale(24);
    const int scrollY = Scale(aboutScrollY_);
    MoveWindow(aboutIcon_, x, Scale(12) - scrollY, Scale(48), Scale(48), TRUE);
    RECT panelRect{};
    GetClientRect(aboutPanel_, &panelRect);
    const int titleX = Scale(88);
    const int textWidthFromTitle = std::max(0,
        static_cast<int>(panelRect.right) - titleX - Scale(24));
    const int contentWidth = std::max(0,
        static_cast<int>(panelRect.right) - x - Scale(24));
    MoveControl(aboutPanel_, kAboutTitle, titleX, Scale(8) - scrollY, textWidthFromTitle, Scale(28));
    MoveControl(aboutPanel_, kAboutDescription, titleX, Scale(40) - scrollY, textWidthFromTitle, Scale(24));
    MoveControl(aboutPanel_, kAboutVersion, Scale(88), Scale(68) - scrollY, Scale(300), Scale(24));
    MoveControl(aboutPanel_, kAboutHowToUse, x, Scale(122) - scrollY, Scale(400), Scale(28));
    MoveControl(aboutPanel_, kAboutInstructions, x, Scale(156) - scrollY, contentWidth, Scale(84));
}

void SettingsWindow::LayoutDiagnostics()
{
    if (!diagnosticsPanel_) return;
    const int x = Scale(24);
    const int scrollY = Scale(diagnosticsScrollY_);
    RECT panelRect{};
    GetClientRect(diagnosticsPanel_, &panelRect);
    const int contentWidth = std::max(0,
        static_cast<int>(panelRect.right) - x - Scale(24));
    MoveControl(diagnosticsPanel_, kDiagnosticsVrrHeading, x, Scale(8) - scrollY, contentWidth, Scale(28));
    MoveControl(diagnosticsPanel_, kDiagnosticsVrrDescription, x, Scale(40) - scrollY, contentWidth, Scale(64));
    MoveWindow(startVrrButton_, x, Scale(112) - scrollY, Scale(140), Scale(32), TRUE);
    MoveWindow(stopVrrButton_, x + Scale(152), Scale(112) - scrollY, Scale(80), Scale(32), TRUE);
    MoveWindow(vrrStatus_, x, Scale(152) - scrollY, contentWidth, Scale(24), TRUE);
    MoveControl(diagnosticsPanel_, kDiagnosticsEcHeading, x, Scale(196) - scrollY, contentWidth, Scale(28));
    MoveControl(diagnosticsPanel_, kDiagnosticsEcDescription, x, Scale(228) - scrollY, contentWidth, Scale(44));
    MoveWindow(startEcButton_, x, Scale(280) - scrollY, Scale(140), Scale(32), TRUE);
    MoveControl(diagnosticsPanel_, kDiagnosticsIgclHeading, x, Scale(328) - scrollY, contentWidth, Scale(28));
    MoveControl(diagnosticsPanel_, kDiagnosticsIgclDescription, x, Scale(360) - scrollY, contentWidth, Scale(48));
    MoveWindow(startIgclButton_, x, Scale(416) - scrollY, Scale(140), Scale(32), TRUE);
    MoveWindow(openLogsButton_, x + Scale(152), Scale(416) - scrollY, Scale(150), Scale(32), TRUE);
    MoveWindow(diagnosticStatus_, x, Scale(456) - scrollY, contentWidth, Scale(24), TRUE);
}

void SettingsWindow::UpdateGeneralControls()
{
    if (startWithWindows_)
        SendMessageW(startWithWindows_, BM_SETCHECK,
            app_.StartWithWindows() ? BST_CHECKED : BST_UNCHECKED, 0);
}

void SettingsWindow::ShowTab(int index)
{
    if (settingsPanel_) ShowWindow(settingsPanel_, index == kTabSettings ? SW_SHOW : SW_HIDE);
    if (tweaksPanel_) ShowWindow(tweaksPanel_, index == kTabTweaks ? SW_SHOW : SW_HIDE);
    if (aboutPanel_) ShowWindow(aboutPanel_, index == kTabAbout ? SW_SHOW : SW_HIDE);
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
    if (fontUnispace_) SendMessageW(fontUnispace_, BM_SETCHECK,
        app_.HudFont() == clawhud::HudFont::Unispace ? BST_CHECKED : BST_UNCHECKED, 0);
    if (fontSegoeUiVariable_) SendMessageW(fontSegoeUiVariable_, BM_SETCHECK,
        app_.HudFont() == clawhud::HudFont::SegoeUiVariable ? BST_CHECKED : BST_UNCHECKED, 0);
    if (enableHud_) SendMessageW(enableHud_, BM_SETCHECK,
        app_.MockHudEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    if (visibilityAlways_) SendMessageW(visibilityAlways_, BM_SETCHECK,
        options.visibilityMode == clawhud::HudVisibilityMode::Always ? BST_CHECKED : BST_UNCHECKED, 0);
    if (visibilityInGameOnly_) SendMessageW(visibilityInGameOnly_, BM_SETCHECK,
        options.visibilityMode == clawhud::HudVisibilityMode::InGameOnly ? BST_CHECKED : BST_UNCHECKED, 0);
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
    const int size = app_.HudSizeOffset();
    if (hudSizeValue_)
    {
        const wchar_t* text = size == 0 ? L"Default" :
            size > 0 ? (size == 1 ? L"+1" : L"+2") :
            (size == -1 ? L"-1" : L"-2");
        SetWindowTextW(hudSizeValue_, text);
    }
    const bool sizeChangeEnabled = !app_.VrrDiagnosticRunning();
    if (hudSizeMinus_) EnableWindow(hudSizeMinus_, sizeChangeEnabled && size > clawhud::kMinHudSizeOffset);
    if (hudSizePlus_) EnableWindow(hudSizePlus_, sizeChangeEnabled && size < clawhud::kMaxHudSizeOffset);
}

void SettingsWindow::UpdateTweaksControls()
{
    if (intelVrrToggle_) SendMessageW(intelVrrToggle_, BM_SETCHECK, app_.IntelVrrRangeFixEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    const auto result = app_.IntelVrrLastResult();
    if (!result) { if (intelVrrResult_) SetWindowTextW(intelVrrResult_, L"Last result: No result yet"); return; }
    const auto panel = std::wstring(result->panelName.begin(), result->panelName.end());
    const auto before = std::wstring(result->rangeBefore.begin(), result->rangeBefore.end());
    const auto after = std::wstring(result->rangeAfter.begin(), result->rangeAfter.end());
    std::wstring message;
    switch (result->status)
    {
    case clawhud::IntelVrrRunStatus::Disabled: message = L"Disabled"; break;
    case clawhud::IntelVrrRunStatus::Unavailable: message = L"Unavailable"; break;
    case clawhud::IntelVrrRunStatus::UnsupportedPanel: message = L"This panel is not affected."; break;
    case clawhud::IntelVrrRunStatus::AmbiguousDisplay: message = L"Skipped: multiple displays matched."; break;
    case clawhud::IntelVrrRunStatus::AlreadyCorrect: message = L"Already using the native VRR range."; break;
    case clawhud::IntelVrrRunStatus::SkippedUserProfile: message = L"Skipped: a custom or OFF profile is already set."; break;
    case clawhud::IntelVrrRunStatus::Applied: message = L"Native VRR range restored."; break;
    case clawhud::IntelVrrRunStatus::ApplyFailed: message = L"Failed to apply: " + std::wstring(result->message.begin(), result->message.end()); break;
    case clawhud::IntelVrrRunStatus::VerificationFailed: message = L"Applied but could not verify."; break;
    }
    if (intelVrrPanel_) SetWindowTextW(intelVrrPanel_, panel.empty() ? L"" : (L"Panel: " + panel).c_str());
    if (intelVrrRange_) SetWindowTextW(intelVrrRange_, (before.empty() || after.empty()) ? L"" : (L"Range: " + before + L" -> " + after).c_str());
    if (intelVrrResult_) SetWindowTextW(intelVrrResult_, (L"Last result: " + message).c_str());
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
    if (startIgclButton_) EnableWindow(startIgclButton_, !busy);
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
    if (message == WM_ERASEBKGND)
    {
        if (self->systemBackdropActive_)
            return TRUE;

        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect,
            GetSysColorBrush(COLOR_WINDOW));
        return TRUE;
    }
    if (message == WM_MOUSEWHEEL)
    {
        self->wheelRemainder_ += GET_WHEEL_DELTA_WPARAM(wParam);
        while (self->wheelRemainder_ >= WHEEL_DELTA)
        {
            self->ScrollActivePanel(-kWheelStep);
            self->wheelRemainder_ -= WHEEL_DELTA;
        }
        while (self->wheelRemainder_ <= -WHEEL_DELTA)
        {
            self->ScrollActivePanel(kWheelStep);
            self->wheelRemainder_ += WHEEL_DELTA;
        }
        return 0;
    }
    if (message == WM_GESTURE)
    {
        GESTUREINFO info{};
        info.cbSize = sizeof(info);
        const HGESTUREINFO gesture = reinterpret_cast<HGESTUREINFO>(lParam);
        if (!GetGestureInfo(gesture, &info) || info.dwID != GID_PAN)
            return DefWindowProcW(window, message, wParam, lParam);

        const LONG y = info.ptsLocation.y;
        if (info.dwFlags & GF_BEGIN)
        {
            self->panActive_ = true;
            self->lastPanY_ = y;
        }
        else if (self->panActive_)
        {
            const int delta = MulDiv(static_cast<int>(y - self->lastPanY_),
                96, static_cast<int>(self->dpi_));
            self->lastPanY_ = y;
            self->ScrollActivePanel(-delta);
        }
        if (info.dwFlags & GF_END)
            self->panActive_ = false;

        CloseGestureInfoHandle(gesture);
        return 0;
    }
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED)
    {
        switch (LOWORD(wParam))
        {
        case kStartWithWindows:
        {
            const bool requested =
                SendMessageW(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0) == BST_CHECKED;
            self->app_.SetStartWithWindows(
                requested);
            self->UpdateGeneralControls();
            return 0;
        }
        case kEnableHud:
            self->app_.SetHudEnabled(
                SendMessageW(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0) == BST_CHECKED);
            self->UpdateHudControls();
            return 0;
        case kVisibilityAlways:
            self->app_.SetHudVisibilityMode(clawhud::HudVisibilityMode::Always);
            self->UpdateHudControls();
            return 0;
        case kVisibilityInGameOnly:
            self->app_.SetHudVisibilityMode(clawhud::HudVisibilityMode::InGameOnly);
            self->UpdateHudControls();
            return 0;
        case kFontUnispace: self->app_.SetHudFont(clawhud::HudFont::Unispace); return 0;
        case kFontSegoeUiVariable: self->app_.SetHudFont(clawhud::HudFont::SegoeUiVariable); return 0;
        case kAlignmentLeft: self->app_.SetHudAlignment(clawhud::HudAlignment::Left); return 0;
        case kAlignmentCenter: self->app_.SetHudAlignment(clawhud::HudAlignment::Center); return 0;
        case kAlignmentRight: self->app_.SetHudAlignment(clawhud::HudAlignment::Right); return 0;
        case kBackgroundFull: self->app_.SetHudBackgroundMode(clawhud::HudBackgroundMode::FullWidth); return 0;
        case kBackgroundContent: self->app_.SetHudBackgroundMode(clawhud::HudBackgroundMode::ContentWidth); return 0;
        case kHudSizeMinus:
            self->app_.SetHudSizeOffset(self->app_.HudSizeOffset() - 1);
            self->UpdateHudControls();
            return 0;
        case kHudSizePlus:
            self->app_.SetHudSizeOffset(self->app_.HudSizeOffset() + 1);
            self->UpdateHudControls();
            return 0;
        case kIntelVrrToggle: self->app_.SetIntelVrrRangeFixEnabled(SendMessageW(self->intelVrrToggle_, BM_GETCHECK, 0, 0) == BST_CHECKED); return 0;
        default: break;
        }
    }
    if (message == WM_HSCROLL && reinterpret_cast<HWND>(lParam) == self->opacitySlider_)
    {
        const int position = static_cast<int>(SendMessageW(self->opacitySlider_, TBM_GETPOS, 0, 0));
        const bool persist = LOWORD(wParam) != TB_THUMBTRACK;
        self->app_.SetHudBackgroundOpacity(position / 100.0f, persist);
        if (self->opacityLabel_)
        {
            wchar_t text[8]{};
            swprintf_s(text, L"%d%%", position);
            SetWindowTextW(self->opacityLabel_, text);
        }
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == kStartEc)
    {
        if (self->app_.StartEcDiagnostic()) self->SetDiagnosticStatus(L"Running"); return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == kStartIgcl)
    {
        if (self->app_.StartIgclDiagnostic()) self->SetDiagnosticStatus(L"Waiting 5 seconds...");
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == kStartVrr)
    {
        if (self->app_.StartVrrDiagnostic())
            self->SetVrrStatus(L"Waiting for F8");
        else
            self->SetVrrStatus(self->app_.VrrStatus().c_str());
        return 0;
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
        self->ShowTab(TabCtrl_GetCurSel(self->tabs_));
        self->Layout();
        return 0;
    }
    if (message == WM_DPICHANGED)
    {
        self->dpi_ = HIWORD(wParam);
        if (self->dpi_ == 0) self->dpi_ = 96;
        const auto* rect = reinterpret_cast<const RECT*>(lParam);
        if (rect)
        {
            SetWindowPos(window, nullptr, rect->left, rect->top,
                rect->right - rect->left, rect->bottom - rect->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        self->RecreateFont();
        self->ApplyFont();
        self->Layout();
        return 0;
    }
    if (message == WM_SIZE)
    {
        if (wParam == SIZE_MINIMIZED)
        {
            DestroyWindow(window);
            return 0;
        }
        self->Layout();
        return 0;
    }
    if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_NCDESTROY)
    {
        if (self->uiFont_)
        {
            DeleteObject(self->uiFont_);
            self->uiFont_ = nullptr;
        }
        if (self->headingFont_)
        {
            DeleteObject(self->headingFont_);
            self->headingFont_ = nullptr;
        }
        self->window_ = nullptr;
        self->tabs_ = nullptr;
        PostMessageW(self->app_.MessageWindow(), WM_APP + 1, 0, 0);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
