#include "PresentMonRuntimeStartupPolicy.h"

#include <cassert>
#include <string>
#include <string_view>

// Cleanup 2 (work order 11.1): every PresentMonRuntimeBootstrapResult must have
// an explicit startup meaning. The bootstrap result is no longer ignored.

using clawhud::PresentMonRuntimeBootstrapResult;
using clawhud::PresentMonRuntimeStartupAction;
using clawhud::PresentMonRuntimeStartupActionForResult;
using clawhud::PresentMonRuntimeBootstrapResultName;
using clawhud::ManagedPresentMonStartupExitCodeForResult;
using clawhud::ManagedStartupExitCode;
using clawhud::ManagedStartupFailureReason;
using clawhud::ToProcessExitCode;

static_assert(ToProcessExitCode(ManagedStartupExitCode::AlreadyRunning) == 20);
static_assert(ToProcessExitCode(ManagedStartupExitCode::UnsupportedHardware) == 21);
static_assert(ToProcessExitCode(ManagedStartupExitCode::HardwareIndeterminate) == 22);
static_assert(ToProcessExitCode(ManagedStartupExitCode::PresentMonRebootRequired) == 30);
static_assert(ToProcessExitCode(ManagedStartupExitCode::PresentMonElevationCancelled) == 31);
static_assert(ToProcessExitCode(ManagedStartupExitCode::PresentMonMsiMissing) == 32);
static_assert(ToProcessExitCode(ManagedStartupExitCode::PresentMonInstallTimedOut) == 33);
static_assert(ToProcessExitCode(ManagedStartupExitCode::PresentMonInstallFailed) == 34);
static_assert(ToProcessExitCode(ManagedStartupExitCode::PresentMonValidationFailed) == 35);
static_assert(ToProcessExitCode(ManagedStartupExitCode::RuntimeInitializationFailed) == 40);
static_assert(ToProcessExitCode(ManagedStartupExitCode::ControlIpcUnavailable) == 41);

static_assert(std::wstring_view(ManagedStartupFailureReason(
    ManagedStartupExitCode::AlreadyRunning)) == L"already-running");
static_assert(std::wstring_view(ManagedStartupFailureReason(
    ManagedStartupExitCode::UnsupportedHardware)) == L"unsupported-hardware");
static_assert(std::wstring_view(ManagedStartupFailureReason(
    ManagedStartupExitCode::HardwareIndeterminate)) == L"hardware-indeterminate");
static_assert(std::wstring_view(ManagedStartupFailureReason(
    ManagedStartupExitCode::PresentMonRebootRequired)) == L"presentmon-reboot-required");
static_assert(std::wstring_view(ManagedStartupFailureReason(
    ManagedStartupExitCode::PresentMonElevationCancelled)) == L"presentmon-elevation-cancelled");
static_assert(std::wstring_view(ManagedStartupFailureReason(
    ManagedStartupExitCode::PresentMonMsiMissing)) == L"presentmon-msi-missing");
static_assert(std::wstring_view(ManagedStartupFailureReason(
    ManagedStartupExitCode::PresentMonInstallTimedOut)) == L"presentmon-install-timeout");
static_assert(std::wstring_view(ManagedStartupFailureReason(
    ManagedStartupExitCode::PresentMonInstallFailed)) == L"presentmon-install-failed");
static_assert(std::wstring_view(ManagedStartupFailureReason(
    ManagedStartupExitCode::PresentMonValidationFailed)) == L"presentmon-validation-failed");
static_assert(std::wstring_view(ManagedStartupFailureReason(
    ManagedStartupExitCode::RuntimeInitializationFailed)) == L"runtime-initialization-failed");
static_assert(std::wstring_view(ManagedStartupFailureReason(
    ManagedStartupExitCode::ControlIpcUnavailable)) == L"control-ipc-unavailable");

static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::AlreadyReady) ==
    PresentMonRuntimeStartupAction::Continue);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::Installed) ==
    PresentMonRuntimeStartupAction::Continue);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::InstalledRebootRequired) ==
    PresentMonRuntimeStartupAction::ExitInformational);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::ElevationCancelled) ==
    PresentMonRuntimeStartupAction::ExitInformational);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::MsiMissing) ==
    PresentMonRuntimeStartupAction::ExitFailure);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::InstallTimedOut) ==
    PresentMonRuntimeStartupAction::ExitFailure);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::InstallFailed) ==
    PresentMonRuntimeStartupAction::ExitFailure);
static_assert(PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult::ValidationFailed) ==
    PresentMonRuntimeStartupAction::ExitFailure);

int main()
{
    // Continue
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::AlreadyReady) ==
        PresentMonRuntimeStartupAction::Continue);
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::Installed) ==
        PresentMonRuntimeStartupAction::Continue);

    // ExitInformational
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::InstalledRebootRequired) ==
        PresentMonRuntimeStartupAction::ExitInformational);
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::ElevationCancelled) ==
        PresentMonRuntimeStartupAction::ExitInformational);

    // ExitFailure
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::MsiMissing) ==
        PresentMonRuntimeStartupAction::ExitFailure);
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::InstallTimedOut) ==
        PresentMonRuntimeStartupAction::ExitFailure);
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::InstallFailed) ==
        PresentMonRuntimeStartupAction::ExitFailure);
    assert(PresentMonRuntimeStartupActionForResult(
        PresentMonRuntimeBootstrapResult::ValidationFailed) ==
        PresentMonRuntimeStartupAction::ExitFailure);

    assert(std::wstring(PresentMonRuntimeBootstrapResultName(
        PresentMonRuntimeBootstrapResult::InstallTimedOut)) == L"InstallTimedOut");
    assert(std::wstring(PresentMonRuntimeBootstrapResultName(
        PresentMonRuntimeBootstrapResult::AlreadyReady)) == L"AlreadyReady");

    assert(!ManagedPresentMonStartupExitCodeForResult(
        PresentMonRuntimeBootstrapResult::AlreadyReady));
    assert(!ManagedPresentMonStartupExitCodeForResult(
        PresentMonRuntimeBootstrapResult::Installed));
    assert(ManagedPresentMonStartupExitCodeForResult(
        PresentMonRuntimeBootstrapResult::InstalledRebootRequired) ==
        ManagedStartupExitCode::PresentMonRebootRequired);
    assert(ManagedPresentMonStartupExitCodeForResult(
        PresentMonRuntimeBootstrapResult::ElevationCancelled) ==
        ManagedStartupExitCode::PresentMonElevationCancelled);
    assert(ManagedPresentMonStartupExitCodeForResult(
        PresentMonRuntimeBootstrapResult::MsiMissing) ==
        ManagedStartupExitCode::PresentMonMsiMissing);
    assert(ManagedPresentMonStartupExitCodeForResult(
        PresentMonRuntimeBootstrapResult::InstallTimedOut) ==
        ManagedStartupExitCode::PresentMonInstallTimedOut);
    assert(ManagedPresentMonStartupExitCodeForResult(
        PresentMonRuntimeBootstrapResult::InstallFailed) ==
        ManagedStartupExitCode::PresentMonInstallFailed);
    assert(ManagedPresentMonStartupExitCodeForResult(
        PresentMonRuntimeBootstrapResult::ValidationFailed) ==
        ManagedStartupExitCode::PresentMonValidationFailed);
    return 0;
}
