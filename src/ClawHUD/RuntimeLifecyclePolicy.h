#pragma once

// Mode-specific lifecycle decisions stay pure and testable without App, Velopack,
// or a HWND. Managed launch must not acquire Standalone shell ownership.
#include "LaunchMode.h"
#include "ManagedStartupExitCode.h"

#include <optional>

namespace clawhud
{
enum class SingleInstanceResult
{
    Acquired,
    AlreadyRunning,
    Failed,
};

constexpr bool ShouldReconcileStartupRegistration(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Standalone;
}

constexpr bool ShouldAllowStartWithWindowsMutation(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Standalone;
}

constexpr bool ShouldRunSelfUpdate(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Standalone;
}

constexpr bool ShouldShowStartupFailureUi(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Standalone;
}

constexpr bool ShouldFailStartupWhenControlIpcUnavailable(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Managed;
}

constexpr bool ShouldRestartAfterVelopackUpdate(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Standalone;
}

constexpr bool ShouldRegisterGlobalHudHotkey(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Standalone;
}

constexpr bool ShouldInitializeFreshManagedSettings(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Managed;
}

constexpr std::optional<ManagedStartupExitCode>
ManagedStartupExitCodeForSingleInstanceResult(SingleInstanceResult result) noexcept
{
    switch (result)
    {
    case SingleInstanceResult::Acquired:
        return std::nullopt;
    case SingleInstanceResult::AlreadyRunning:
        return ManagedStartupExitCode::AlreadyRunning;
    case SingleInstanceResult::Failed:
        return ManagedStartupExitCode::RuntimeInitializationFailed;
    }
    return ManagedStartupExitCode::RuntimeInitializationFailed;
}
}
