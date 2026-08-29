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
using settings_internal::kDiagnosticsEcDescription;
using settings_internal::kDiagnosticsEcHeading;
using settings_internal::kDiagnosticsIgclDescription;
using settings_internal::kDiagnosticsIgclHeading;
using settings_internal::kDiagnosticsVrrDescription;
using settings_internal::kDiagnosticsVrrHeading;
using settings_internal::kOpenLogs;
using settings_internal::kStartEc;
using settings_internal::kStartIgcl;
using settings_internal::kStartVrr;
using settings_internal::kStopVrr;
}

void SettingsWindow::CreateDiagnosticsControls()
{
    diagnosticsPanel_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    if (diagnosticsPanel_) SetWindowSubclass(diagnosticsPanel_, ForwardPanelNotifications, 2, 0);
    CreateWindowW(L"STATIC", L"VRR / Presentation Test", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDiagnosticsVrrHeading)), instance_, nullptr);
    CreateWindowW(L"STATIC", L"Start closes Settings. Return to the game and press F8.\r\nA ding confirms the target, then OFF -> DYNAMIC @ 500 ms (~28 sec each).\r\nA second ding confirms successful completion.",
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
    debugLoggingToggle_ = CreateWindowW(L"BUTTON", L"Enable debug logging",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0,
        diagnosticsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDebugLoggingToggle)), instance_, nullptr);
    openLogsButton_ = CreateWindowW(L"BUTTON", L"Open Log Folder",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, diagnosticsPanel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenLogs)), instance_, nullptr);
    diagnosticStatus_ = CreateWindowW(L"STATIC", L"Status: Idle", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, diagnosticsPanel_, nullptr, instance_, nullptr);
    EnableMouseWheelForwarding(startVrrButton_);
    EnableMouseWheelForwarding(stopVrrButton_);
    EnableMouseWheelForwarding(startEcButton_);
    EnableMouseWheelForwarding(startIgclButton_);
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
    MoveWindow(debugLoggingToggle_, x, Scale(456) - scrollY, Scale(300), Scale(32), TRUE);
    MoveWindow(diagnosticStatus_, x, Scale(496) - scrollY, contentWidth, Scale(24), TRUE);
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
    if (debugLoggingToggle_) SendMessageW(debugLoggingToggle_, BM_SETCHECK,
        app_.DebugLoggingEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
}
