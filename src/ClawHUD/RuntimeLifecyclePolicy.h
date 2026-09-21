#pragma once

// CH-I1 — the mode-aware lifecycle decisions, isolated as pure
// constexpr predicates so they can be tested without App / Velopack / a HWND.
//
// Standalone owns its own shell concerns:
//  - Managed launch must not become the owner of the Standalone startup
//    shortcut just because it was launched.
//  - Managed launch must not discover or apply ClawHUD self-updates.

#include "LaunchMode.h"

namespace clawhud
{
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
}
