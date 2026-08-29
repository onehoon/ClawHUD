#include "SettingsWindow.h"
#include "SettingsWindowInternal.h"

#include "Version.h"
#include "resource.h"

#include <commctrl.h>

#include <algorithm>

namespace
{
namespace settings_internal = clawhud::settings::internal;
using settings_internal::EnableStaticPanForwarding;
using settings_internal::MoveControl;
using settings_internal::kAboutDescription;
using settings_internal::kAboutHowToUse;
using settings_internal::kAboutInstructions;
using settings_internal::kAboutTitle;
using settings_internal::kAboutVersion;
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
