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
using settings_internal::kDebugLoggingToggle;
using settings_internal::kOpenLogs;
}

void SettingsWindow::CreateDiagnosticsControls()
{
    diagnosticsPanel_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    if (diagnosticsPanel_) SetWindowSubclass(diagnosticsPanel_, ForwardPanelNotifications, 2, 0);
    debugLoggingToggle_ = CreateWindowW(L"BUTTON", L"Enable debug logging",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0,
        diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDebugLoggingToggle)), instance_, nullptr);
    openLogsButton_ = CreateWindowW(L"BUTTON", L"Open Log Folder",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, diagnosticsPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenLogs)), instance_, nullptr);
    EnableMouseWheelForwarding(debugLoggingToggle_);
    EnableMouseWheelForwarding(openLogsButton_);
    EnableStaticPanForwarding(diagnosticsPanel_);
}

void SettingsWindow::LayoutDiagnostics()
{
    if (!diagnosticsPanel_) return;
    const int x = Scale(24);
    const int scrollY = Scale(diagnosticsScrollY_);
    MoveWindow(debugLoggingToggle_, x, Scale(8) - scrollY, Scale(300), Scale(32), TRUE);
    MoveWindow(openLogsButton_, x, Scale(48) - scrollY, Scale(150), Scale(32), TRUE);
}

void SettingsWindow::UpdateDiagnosticButtons()
{
    if (debugLoggingToggle_) SendMessageW(debugLoggingToggle_, BM_SETCHECK,
        app_.DebugLoggingEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
}
