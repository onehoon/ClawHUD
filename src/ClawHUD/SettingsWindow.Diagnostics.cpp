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
using settings_internal::kDebugLoggingToggle;
using settings_internal::kDiagnosticsApi2Heading;
using settings_internal::kDiagnosticsApi2Description;
using settings_internal::kOpenLogs;
using settings_internal::kStartApi2;
using settings_internal::kStopApi2;
}

void SettingsWindow::CreateDiagnosticsControls()
{
    diagnosticsPanel_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    if (diagnosticsPanel_) SetWindowSubclass(diagnosticsPanel_, ForwardPanelNotifications, 2, 0);
    CreateWindowW(L"STATIC", L"PresentMon API2 Read-only Capability Test", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDiagnosticsApi2Heading)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Requires the installed PresentMon API2 SDK/runtime. Closes Settings, waits 5 seconds, then records a fixed-target survey for approximately 15 seconds. A game-detection research probe continues until you press Stop.",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDiagnosticsApi2Description)), instance_, nullptr);
    startApi2Button_ = CreateWindowW(L"BUTTON", L"Start API2 Test",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, diagnosticsPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartApi2)), instance_, nullptr);
    stopApi2Button_ = CreateWindowW(L"BUTTON", L"Stop",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, diagnosticsPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStopApi2)), instance_, nullptr);
    debugLoggingToggle_ = CreateWindowW(L"BUTTON", L"Enable debug logging",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0,
        diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDebugLoggingToggle)), instance_, nullptr);
    openLogsButton_ = CreateWindowW(L"BUTTON", L"Open Log Folder",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, diagnosticsPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenLogs)), instance_, nullptr);
    diagnosticStatus_ = CreateWindowW(L"STATIC", L"Status: Idle", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, diagnosticsPanel_, nullptr, instance_, nullptr);
    EnableMouseWheelForwarding(startApi2Button_);
    EnableMouseWheelForwarding(stopApi2Button_);
    EnableMouseWheelForwarding(debugLoggingToggle_);
    EnableMouseWheelForwarding(openLogsButton_);
    EnableStaticPanForwarding(diagnosticsPanel_);
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
    MoveControl(diagnosticsPanel_, kDiagnosticsApi2Heading, x, Scale(8) - scrollY, contentWidth, Scale(28));
    MoveControl(diagnosticsPanel_, kDiagnosticsApi2Description, x, Scale(40) - scrollY, contentWidth, Scale(64));
    MoveWindow(startApi2Button_, x, Scale(112) - scrollY, Scale(140), Scale(32), TRUE);
    MoveWindow(stopApi2Button_, x + Scale(152), Scale(112) - scrollY, Scale(80), Scale(32), TRUE);
    MoveWindow(openLogsButton_, x + Scale(248), Scale(112) - scrollY, Scale(150), Scale(32), TRUE);
    MoveWindow(debugLoggingToggle_, x, Scale(152) - scrollY, Scale(300), Scale(32), TRUE);
    MoveWindow(diagnosticStatus_, x, Scale(192) - scrollY, contentWidth, Scale(24), TRUE);
}

void SettingsWindow::SetDiagnosticStatus(const std::wstring& status)
{
    if (diagnosticStatus_) SetWindowTextW(diagnosticStatus_, (L"Status: " + status).c_str());
    UpdateDiagnosticButtons();
}

void SettingsWindow::UpdateDiagnosticButtons()
{
    const bool busy = app_.DiagnosticRunning();
    if (startApi2Button_) EnableWindow(startApi2Button_, !busy);
    if (stopApi2Button_) EnableWindow(stopApi2Button_, app_.PresentMonApi2DiagnosticRunning());
    if (debugLoggingToggle_) SendMessageW(debugLoggingToggle_, BM_SETCHECK,
        app_.DebugLoggingEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
}
