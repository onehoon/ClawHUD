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
// launcher/shell/overlay images that must never be treated as a game.
bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept;
bool IsEligibleProductionTargetImage(std::wstring_view image) noexcept;

// Opens the process, resolves its image name, and classifies it. Used by
// GameScreenAdmission before any positive game evidence is accepted.
ProductionTargetInspection InspectProductionTargetProcessDetailed(
    DWORD processId, DWORD ownProcessId = GetCurrentProcessId()) noexcept;

// Resume recovery: after a completed recovery attempt, re-adopt the current
// foreground rather than a pre-suspend game.
bool ShouldReevaluateForegroundAfterResume(bool hudEnabled,
    bool recoveryCompleted) noexcept;
}
