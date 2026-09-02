#pragma once

// CH-RTF-6 — Control pipe endpoint naming and security policy.
//
// Local, current-user, same-session only. These helpers are factored out so
// the deterministic name, the protected DACL, and the session gate can be
// tested without a real cross-account client.

#include <windows.h>

#include <optional>
#include <string>

namespace clawhud::control
{
// Windows session id of the current process, or nullopt if it cannot be
// resolved (in which case the pipe server must not start).
std::optional<DWORD> CurrentProcessSessionId();

// The deterministic per-session endpoint: \\.\pipe\ClawHUD.Control.<sessionId>.
// No random component, no PID, no protocol version. nullopt on session-id
// failure (no unscoped fallback).
std::optional<std::wstring> ControlPipeName();

// String SID of the current process user, e.g. "S-1-5-21-...". Used to build
// the DACL and asserted by tests.
std::optional<std::wstring> CurrentUserSidString();

// Same-session check for a validated client session id.
inline bool SessionsMatch(DWORD serverSession, DWORD clientSession) noexcept
{
    return serverSession == clientSession;
}

// Owns a protected, current-user-only security descriptor and the
// SECURITY_ATTRIBUTES that reference it for CreateNamedPipe.
class ControlPipeSecurity
{
public:
    ControlPipeSecurity() = default;
    ~ControlPipeSecurity();

    ControlPipeSecurity(const ControlPipeSecurity&) = delete;
    ControlPipeSecurity& operator=(const ControlPipeSecurity&) = delete;

    // Builds  D:P(A;;GA;;;<current-user-sid>)  — protected DACL, current user
    // gets full access, nothing else. Returns false on failure.
    bool Build();

    // Valid only after Build() returned true.
    SECURITY_ATTRIBUTES* Attributes() noexcept { return descriptor_ ? &attributes_ : nullptr; }
    const std::wstring& Sddl() const noexcept { return sddl_; }

private:
    PSECURITY_DESCRIPTOR descriptor_{};
    SECURITY_ATTRIBUTES attributes_{};
    std::wstring sddl_;
};
}
