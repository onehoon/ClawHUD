#pragma once

#include "PresentMonRuntimeBootstrap.h"

namespace clawhud
{
// What a PresentMon runtime bootstrap result means for normal startup. Pure
// decision, no side effects -- App maps it to a Win32 message + exit.
enum class PresentMonRuntimeStartupAction
{
    Continue,           // runtime is ready; proceed into normal initialization
    ExitInformational,  // expected condition (reboot / user declined); warn + exit
    ExitFailure,        // the runtime could not be prepared; error + exit
};

constexpr PresentMonRuntimeStartupAction PresentMonRuntimeStartupActionForResult(
    PresentMonRuntimeBootstrapResult result) noexcept
{
    switch (result)
    {
    case PresentMonRuntimeBootstrapResult::AlreadyReady:
    case PresentMonRuntimeBootstrapResult::Installed:
        return PresentMonRuntimeStartupAction::Continue;

    case PresentMonRuntimeBootstrapResult::InstalledRebootRequired:
    case PresentMonRuntimeBootstrapResult::ElevationCancelled:
        return PresentMonRuntimeStartupAction::ExitInformational;

    case PresentMonRuntimeBootstrapResult::MsiMissing:
    case PresentMonRuntimeBootstrapResult::InstallTimedOut:
    case PresentMonRuntimeBootstrapResult::InstallFailed:
    case PresentMonRuntimeBootstrapResult::ValidationFailed:
        return PresentMonRuntimeStartupAction::ExitFailure;
    }
    return PresentMonRuntimeStartupAction::ExitFailure;
}

constexpr const wchar_t* PresentMonRuntimeBootstrapResultName(
    PresentMonRuntimeBootstrapResult result) noexcept
{
    switch (result)
    {
    case PresentMonRuntimeBootstrapResult::AlreadyReady:
        return L"AlreadyReady";
    case PresentMonRuntimeBootstrapResult::Installed:
        return L"Installed";
    case PresentMonRuntimeBootstrapResult::InstalledRebootRequired:
        return L"InstalledRebootRequired";
    case PresentMonRuntimeBootstrapResult::MsiMissing:
        return L"MsiMissing";
    case PresentMonRuntimeBootstrapResult::ElevationCancelled:
        return L"ElevationCancelled";
    case PresentMonRuntimeBootstrapResult::InstallTimedOut:
        return L"InstallTimedOut";
    case PresentMonRuntimeBootstrapResult::InstallFailed:
        return L"InstallFailed";
    case PresentMonRuntimeBootstrapResult::ValidationFailed:
        return L"ValidationFailed";
    }
    return L"Unknown";
}
}
