#pragma once

namespace clawhud
{
// Stable process-result contract for failures before Managed Control IPC is
// ready. These values are consumed by the future SteamAddon process owner.
enum class ManagedStartupExitCode : int
{
    AlreadyRunning = 20,
    UnsupportedHardware = 21,
    HardwareIndeterminate = 22,

    PresentMonRebootRequired = 30,
    PresentMonElevationCancelled = 31,
    PresentMonMsiMissing = 32,
    PresentMonInstallTimedOut = 33,
    PresentMonInstallFailed = 34,
    PresentMonValidationFailed = 35,

    RuntimeInitializationFailed = 40,
    ControlIpcUnavailable = 41,
};

constexpr int ToProcessExitCode(ManagedStartupExitCode code) noexcept
{
    return static_cast<int>(code);
}

constexpr const wchar_t* ManagedStartupFailureReason(
    ManagedStartupExitCode code) noexcept
{
    switch (code)
    {
    case ManagedStartupExitCode::AlreadyRunning:
        return L"already-running";
    case ManagedStartupExitCode::UnsupportedHardware:
        return L"unsupported-hardware";
    case ManagedStartupExitCode::HardwareIndeterminate:
        return L"hardware-indeterminate";
    case ManagedStartupExitCode::PresentMonRebootRequired:
        return L"presentmon-reboot-required";
    case ManagedStartupExitCode::PresentMonElevationCancelled:
        return L"presentmon-elevation-cancelled";
    case ManagedStartupExitCode::PresentMonMsiMissing:
        return L"presentmon-msi-missing";
    case ManagedStartupExitCode::PresentMonInstallTimedOut:
        return L"presentmon-install-timeout";
    case ManagedStartupExitCode::PresentMonInstallFailed:
        return L"presentmon-install-failed";
    case ManagedStartupExitCode::PresentMonValidationFailed:
        return L"presentmon-validation-failed";
    case ManagedStartupExitCode::RuntimeInitializationFailed:
        return L"runtime-initialization-failed";
    case ManagedStartupExitCode::ControlIpcUnavailable:
        return L"control-ipc-unavailable";
    }
    return L"unknown";
}
}
