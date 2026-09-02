#pragma once

// CH-RTF-9 — the two mode-aware lifecycle decisions, isolated as pure
// constexpr predicates so they can be tested without App / Velopack / a HWND.
//
// Both are Standalone-only:
//  - Managed launch must not become the owner of the Standalone startup
//    shortcut just because it was launched.
//  - Managed update applies with restart=false; the surviving external owner
//    relaunches `ClawHUD.exe --managed`, so Velopack must not restart it.
//
// Explicit SetStartWithWindows via IPC is NOT gated by these — a frontend
// controlling a Managed runtime may still change the user's Standalone
// preference.

#include "LaunchMode.h"

namespace clawhud
{
constexpr bool ShouldReconcileStartupRegistration(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Standalone;
}

constexpr bool ShouldRestartAfterVelopackUpdate(LaunchMode mode) noexcept
{
    return mode == LaunchMode::Standalone;
}
}
