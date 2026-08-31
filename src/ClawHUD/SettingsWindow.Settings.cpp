#include "SettingsWindow.h"
#include "SettingsWindowInternal.h"

#include "App.h"
#include "HudSize.h"

#include <commctrl.h>

#include <cmath>

namespace
{
namespace settings_internal = clawhud::settings::internal;
using settings_internal::EnableMouseWheelForwarding;
using settings_internal::EnableStaticPanForwarding;
using settings_internal::ForwardPanelNotifications;
using settings_internal::MoveControl;
using settings_internal::kAlignmentCenter;
using settings_internal::kAlignmentLabel;
using settings_internal::kAlignmentLeft;
using settings_internal::kAlignmentRight;
using settings_internal::kBackgroundContent;
using settings_internal::kBackgroundFull;
using settings_internal::kBackgroundWidthLabel;
using settings_internal::kDefaultWindowHeightDip;
using settings_internal::kDefaultWindowWidthDip;
using settings_internal::kEnableHud;
using settings_internal::kFontLabel;
using settings_internal::kFontSegoeUiVariable;
using settings_internal::kFontUnispace;
using settings_internal::kGeneralHeading;
using settings_internal::kHudHeading;
using settings_internal::kHudSizeLabel;
using settings_internal::kHudSizeMinus;
using settings_internal::kHudSizePlus;
using settings_internal::kOpacityLabel;
using settings_internal::kOpacitySlider;
using settings_internal::kSettingsClassName;
using settings_internal::kStartWithWindows;
using settings_internal::kTabSettings;
using settings_internal::kTabCount;
using settings_internal::kVisibilityAlways;
using settings_internal::kVisibilityInGameOnly;
using settings_internal::kVisibilityLabel;
using settings_internal::kWheelStep;
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
    CreateWindowW(L"STATIC", L"HUD Opacity", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpacityLabel)), instance_, nullptr);
    opacitySlider_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_NOTICKS, 0, 0, 0, 0,
        settingsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpacitySlider)), instance_, nullptr);
    SendMessageW(opacitySlider_, TBM_SETRANGE, TRUE, MAKELONG(
        clawhud::kHudOpacityMinimumPercent, clawhud::kHudOpacityMaximumPercent));
    SendMessageW(opacitySlider_, TBM_SETLINESIZE, 0, clawhud::kHudOpacityStepPercent);
    SendMessageW(opacitySlider_, TBM_SETPAGESIZE, 0, clawhud::kHudOpacityStepPercent);
    opacityLabel_ = CreateWindowW(L"STATIC", L"70%", WS_CHILD | WS_VISIBLE,
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

void SettingsWindow::UpdateGeneralControls()
{
    if (startWithWindows_)
        SendMessageW(startWithWindows_, BM_SETCHECK,
            app_.StartWithWindows() ? BST_CHECKED : BST_UNCHECKED, 0);
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
        app_.HudEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    if (visibilityAlways_) SendMessageW(visibilityAlways_, BM_SETCHECK,
        options.visibilityMode == clawhud::HudVisibilityMode::Always ? BST_CHECKED : BST_UNCHECKED, 0);
    if (visibilityInGameOnly_) SendMessageW(visibilityInGameOnly_, BM_SETCHECK,
        options.visibilityMode == clawhud::HudVisibilityMode::InGameOnly ? BST_CHECKED : BST_UNCHECKED, 0);
    if (backgroundFull_) SendMessageW(backgroundFull_, BM_SETCHECK,
        options.backgroundMode == clawhud::HudBackgroundMode::FullWidth ? BST_CHECKED : BST_UNCHECKED, 0);
    if (backgroundContent_) SendMessageW(backgroundContent_, BM_SETCHECK,
        options.backgroundMode == clawhud::HudBackgroundMode::ContentWidth ? BST_CHECKED : BST_UNCHECKED, 0);
    const int percent = static_cast<int>(clawhud::ClampHudOpacityPercent(
        static_cast<long>(std::lround(options.backgroundOpacity * 100.0f))));
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
    if (hudSizeMinus_) EnableWindow(hudSizeMinus_, size > clawhud::kMinHudSizeOffset);
    if (hudSizePlus_) EnableWindow(hudSizePlus_, size < clawhud::kMaxHudSizeOffset);
}
