#pragma once

#include "GameDetection/GameDetectionCoordinator.h"

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace clawhud
{
enum class CandidateDisposition
{
    Ignore,
    Merge,
    Replace
};

enum class GlobalTelemetryAction
{
    Keep,
    Start,
    Stop,
};

struct CommittedTargetReleasePlan
{
    bool stopPresentMon{ true };
    bool stopGraphicsApiProbe{ true };
    bool clearTrackedProcess{ true };
    bool reconcileHudVisibility{ true };
    GlobalTelemetryAction globalTelemetry{ GlobalTelemetryAction::Keep };
};

struct ProductionTargetProcess
{
    DWORD processId{};
    std::wstring imageName;
};

constexpr CommittedTargetReleasePlan PlanCommittedTargetRelease() noexcept
{
    return {};
}

struct CommittedTargetReleaseOps
{
    std::function<void()> stopPresentMon;
    std::function<void()> stopGraphicsApiProbe;
    std::function<void()> clearTrackedProcess;
    std::function<void()> startGlobalTelemetry;
    std::function<void()> stopGlobalTelemetry;
    std::function<void()> reconcileHudVisibility;
};

inline void ApplyCommittedTargetReleasePlan(
    const CommittedTargetReleasePlan& plan,
    const CommittedTargetReleaseOps& ops)
{
    if (plan.stopPresentMon)
        ops.stopPresentMon();
    if (plan.stopGraphicsApiProbe)
        ops.stopGraphicsApiProbe();
    if (plan.clearTrackedProcess)
        ops.clearTrackedProcess();

    switch (plan.globalTelemetry)
    {
    case GlobalTelemetryAction::Keep:
        break;
    case GlobalTelemetryAction::Start:
        ops.startGlobalTelemetry();
        break;
    case GlobalTelemetryAction::Stop:
        ops.stopGlobalTelemetry();
        break;
    }

    if (plan.reconcileHudVisibility)
        ops.reconcileHudVisibility();
}

bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept;
bool IsEligibleProductionTargetImage(std::wstring_view image) noexcept;
std::optional<ProductionTargetProcess> InspectProductionTargetProcess(
    DWORD processId, DWORD ownProcessId = GetCurrentProcessId()) noexcept;
CandidateDisposition DecideCandidateDisposition(
    const GameDetectionContext& context,
    GameDetectionTrigger incomingTrigger, DWORD incomingProcessId) noexcept;
bool ShouldCommitReadyCandidate(
    const GameDetectionContext& context,
    DWORD foregroundProcessId, bool candidateProcessAlive) noexcept;
bool ShouldRetainCommittedProductionTarget(DWORD committedProcessId,
    bool processAlive) noexcept;
bool ShouldRetryProductionPresentMon(DWORD retryProcessId,
    unsigned retryAttempts, DWORD processId) noexcept;
bool ShouldAllowProductionPresentMonStart(DWORD committedProcessId,
    DWORD processId, DWORD retryProcessId, unsigned retryAttempts,
    bool recoveryStart) noexcept;
bool ShouldReevaluateForegroundAfterResume(bool hudEnabled,
    bool recoveryCompleted) noexcept;
bool ShouldReevaluateForegroundAfterDiagnostic(bool hudEnabled,
    bool diagnosticRunning, bool suspended) noexcept;
bool ShouldRestartGraphicsApiProbe(DWORD probedProcessId,
    DWORD committedProcessId) noexcept;
bool ShouldConsiderForegroundProductionTarget(bool hudEnabled, bool diagnosticRunning,
    bool suspended) noexcept;
}
