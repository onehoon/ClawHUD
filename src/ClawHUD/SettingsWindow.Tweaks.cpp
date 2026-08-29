#include "SettingsWindow.h"
#include "SettingsWindowInternal.h"

#include "App.h"

#include <commctrl.h>

#include <algorithm>
#include <string>

namespace
{
namespace settings_internal = clawhud::settings::internal;
using settings_internal::EnableMouseWheelForwarding;
using settings_internal::EnableStaticPanForwarding;
using settings_internal::ForwardPanelNotifications;
using settings_internal::MoveControl;
using settings_internal::kIntelVrrToggle;
using settings_internal::kTweaksDescription;
using settings_internal::kTweaksHeading;
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
