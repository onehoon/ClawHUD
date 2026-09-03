#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace clawhud
{
struct ProductionTargetProcess
{
    DWORD processId{};
    std::wstring imageName;
};

enum class ProductionTargetInspectionStatus
{
    Unavailable,
    Excluded,
    Eligible,
};

struct ProductionTargetInspection
{
    ProductionTargetInspectionStatus status{ProductionTargetInspectionStatus::Unavailable};
    ProductionTargetProcess process;
};

// Centralized executable exclusion policy. The demonstrated false-positive set
// (windowsterminal.exe, runtimebroker.exe, crashreportclient.exe, ...) plus
// launcher/shell/overlay images that must never be treated as a game. The
// ClawHUD frontend (clawhud.settings.exe) is included: it is product
// infrastructure and can never be a supported game target.
bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept;
bool IsEligibleProductionTargetImage(std::wstring_view image) noexcept;

// ClawHUD-owned production images: the native runtime (clawhud.exe) and the WPF
// Settings frontend (clawhud.settings.exe). Accepts a bare image name or a full
// path; matching is case-insensitive. ClawHUD must never measure either of its
// own processes as an Always-mode FPS target.
bool IsClawHudOwnedImage(std::wstring_view image) noexcept;

// PID form of IsClawHudOwnedImage. PID 0 is not owned; ownProcessId (the
// caller's own PID by default) is always owned; any other PID is inspected with
// PROCESS_QUERY_LIMITED_INFORMATION. A process that cannot be inspected is
// reported as *not* owned so an uninspectable external app is not suppressed.
bool IsClawHudOwnedProcess(DWORD processId,
    DWORD ownProcessId = GetCurrentProcessId()) noexcept;

// Sanitizes a foreground PID before it is adopted as the Always-mode FPS target:
// a ClawHUD-owned foreground process yields PID 0 (FPS unavailable); every
// external foreground process is passed through unchanged.
DWORD ResolveAlwaysFpsForegroundTarget(DWORD foregroundProcessId,
    DWORD ownProcessId = GetCurrentProcessId()) noexcept;

// Opens the process, resolves its image name, and classifies it. Used by
// GameScreenAdmission before any positive game evidence is accepted.
ProductionTargetInspection InspectProductionTargetProcessDetailed(
    DWORD processId, DWORD ownProcessId = GetCurrentProcessId()) noexcept;

// Resume recovery: after a completed recovery attempt, re-adopt the current
// foreground rather than a pre-suspend game.
bool ShouldReevaluateForegroundAfterResume(bool hudEnabled,
    bool recoveryCompleted) noexcept;
}
