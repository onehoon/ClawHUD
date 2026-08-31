#include "SettingsWindow.h"
#include "SettingsWindowInternal.h"
#include "SettingsWindowGeometry.h"

#include "App.h"
#include "HudSize.h"
#include "resource.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

namespace
{
namespace settings_internal = clawhud::settings::internal;
using settings_internal::ConfigureVerticalPan;
using settings_internal::kAboutTitle;
using settings_internal::kAboutHowToUse;
using settings_internal::kAlignmentCenter;
using settings_internal::kAlignmentLeft;
using settings_internal::kAlignmentRight;
using settings_internal::kBackgroundContent;
using settings_internal::kBackgroundFull;
using settings_internal::kDefaultWindowHeightDip;
using settings_internal::kDefaultWindowWidthDip;
using settings_internal::kEnableHud;
using settings_internal::kFontSegoeUiVariable;
using settings_internal::kFontUnispace;
using settings_internal::kGeneralHeading;
using settings_internal::kHudHeading;
using settings_internal::kHudSizeMinus;
using settings_internal::kHudSizePlus;
using settings_internal::kIntelVrrToggle;
using settings_internal::kMinimumWindowHeightDip;
using settings_internal::kMinimumWindowWidthDip;
using settings_internal::kSettingsClassName;
using settings_internal::kStartWithWindows;
using settings_internal::kTabAbout;
using settings_internal::kTabCount;
using settings_internal::kTabSettings;
using settings_internal::kTabTweaks;
using settings_internal::kTweaksHeading;
using settings_internal::kVisibilityAlways;
using settings_internal::kVisibilityInGameOnly;
using settings_internal::kWheelStep;
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
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                WS_MAXIMIZEBOX | WS_THICKFRAME,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(kDefaultWindowWidthDip),
            Scale(kDefaultWindowHeightDip), nullptr, nullptr, instance_, this);
        if (!window_) return false;
        dpi_ = GetDpiForWindow(window_);
        if (dpi_ == 0) dpi_ = 96;
        SetWindowPos(window_, nullptr, 0, 0, Scale(kDefaultWindowWidthDip),
            Scale(kDefaultWindowHeightDip),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        ApplyWindowStyle();
        ConfigureVerticalPan(window_);
        CreateTabs();
        RecreateFont();
        ApplyFont();
        Layout();
        NormalizeWindowToWorkArea();
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    UpdateHudControls();
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
}

int SettingsWindow::Scale(int value) const noexcept
{
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

void SettingsWindow::RefreshDpiAndLayout()
{
    if (!window_)
        return;
    UINT newDpi = GetDpiForWindow(window_);
    if (newDpi == 0) newDpi = 96;
    if (newDpi == dpi_)
        return;
    dpi_ = newDpi;
    RecreateFont();
    ApplyFont();
    Layout();
}

void SettingsWindow::NormalizeWindowToWorkArea()
{
    if (!window_ || !ShouldNormalizeSettingsWindow(IsIconic(window_) != FALSE,
        IsZoomed(window_) != FALSE))
        return;

    RECT windowRect{};
    if (!GetWindowRect(window_, &windowRect))
        return;
    const HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{ sizeof(info) };
    if (!monitor || !GetMonitorInfoW(monitor, &info))
        return;

    const RECT repaired = ClampSettingsWindowRectToWorkArea(
        windowRect, info.rcWork, Scale(kMinimumWindowWidthDip),
        Scale(kMinimumWindowHeightDip));
    if (EqualRect(&windowRect, &repaired))
        return;
    SetWindowPos(window_, nullptr, repaired.left, repaired.top,
        repaired.right - repaired.left, repaired.bottom - repaired.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

int SettingsWindow::ActiveTab() const noexcept
{
    const int tab = tabs_ ? TabCtrl_GetCurSel(tabs_) : kTabSettings;
    return tab >= kTabSettings && tab <= kTabAbout ? tab : kTabSettings;
}

int SettingsWindow::ContentHeightForTab(int tab) const noexcept
{
    switch (tab)
    {
    case kTabSettings: return 454;
    case kTabTweaks: return 230;
    case kTabAbout: return 260;
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
    case kTabSettings:
    default: return settingsScrollY_;
    }
}

void SettingsWindow::ClampScrollOffsets()
{
    for (int tab = kTabSettings; tab <= kTabAbout; ++tab)
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
}

void SettingsWindow::CreateTabs()
{
    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    const wchar_t* labels[kTabCount] = { L"Settings", L"Tweaks", L"About" };
    for (int i = 0; i < kTabCount; ++i)
    {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(labels[i]);
        TabCtrl_InsertItem(tabs_, i, &item);
    }
    CreateSettingsControls();
    CreateTweaksControls();
    CreateAboutControls();
    ShowTab(kTabSettings);
    UpdateGeneralControls();
    UpdateHudControls();
}

void SettingsWindow::Layout()
{
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    int tabHeaderHeight = Scale(32);
    RECT tabItem{};
    if (tabs_ && TabCtrl_GetItemRect(tabs_, 0, &tabItem))
        tabHeaderHeight = std::max(tabHeaderHeight,
            static_cast<int>(tabItem.bottom) + Scale(2));
    MoveWindow(tabs_, 0, 0,
        std::max(0, static_cast<int>(client.right)),
        std::min(std::max(0, static_cast<int>(client.bottom)), tabHeaderHeight), TRUE);
    const int panelX = Scale(24);
    const int panelY = Scale(52);
    const int panelWidth = std::max(0, static_cast<int>(client.right) - Scale(48));
    const int panelHeight = std::max(0, static_cast<int>(client.bottom) - Scale(64));
    MoveWindow(settingsPanel_, panelX, panelY, panelWidth, panelHeight, TRUE);
    MoveWindow(tweaksPanel_, panelX, panelY, panelWidth, panelHeight, TRUE);
    MoveWindow(aboutPanel_, panelX, panelY, panelWidth, panelHeight, TRUE);
    ClampScrollOffsets();
    ApplyScrollPosition();
}

void SettingsWindow::ShowTab(int index)
{
    if (settingsPanel_) ShowWindow(settingsPanel_, index == kTabSettings ? SW_SHOW : SW_HIDE);
    if (tweaksPanel_) ShowWindow(tweaksPanel_, index == kTabTweaks ? SW_SHOW : SW_HIDE);
    if (aboutPanel_) ShowWindow(aboutPanel_, index == kTabAbout ? SW_SHOW : SW_HIDE);
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
    if (message == WM_GETMINMAXINFO)
    {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{ sizeof(monitorInfo) };
        if (info && monitor && GetMonitorInfoW(monitor, &monitorInfo))
        {
            const int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
            const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
            info->ptMinTrackSize.x = std::min(self->Scale(kMinimumWindowWidthDip), workWidth);
            info->ptMinTrackSize.y = std::min(self->Scale(kMinimumWindowHeightDip), workHeight);
        }
        return 0;
    }
    if (message == WM_ERASEBKGND)
    {
        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect,
            GetSysColorBrush(COLOR_3DFACE));
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
        int position = static_cast<int>(SendMessageW(self->opacitySlider_, TBM_GETPOS, 0, 0));
        position = ((position - clawhud::kHudOpacityMinimumPercent +
            clawhud::kHudOpacityStepPercent / 2) /
            clawhud::kHudOpacityStepPercent) * clawhud::kHudOpacityStepPercent +
            clawhud::kHudOpacityMinimumPercent;
        position = static_cast<int>(clawhud::ClampHudOpacityPercent(position));
        SendMessageW(self->opacitySlider_, TBM_SETPOS, TRUE, position);
        const bool persist = LOWORD(wParam) != TB_THUMBTRACK;
        self->app_.SetHudOpacity(position / 100.0f, persist);
        self->UpdateHudControls();
        return 0;
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
        self->NormalizeWindowToWorkArea();
        return 0;
    }
    if (message == WM_DISPLAYCHANGE)
    {
        self->RefreshDpiAndLayout();
        self->NormalizeWindowToWorkArea();
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
        self->app_.PostSettingsDestroyed();
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
