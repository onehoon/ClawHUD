#include "GenericForegroundTrigger.h"

#include "../ProductionTargetPolicy.h"

namespace clawhud
{
std::optional<GenericForegroundEvidence> GenericForegroundTrigger::Inspect(
    HWND window, DWORD processId) const noexcept
{
    if (window == nullptr)
        return std::nullopt;

    if (!InspectProductionTargetProcess(processId))
        return std::nullopt;

    return GenericForegroundEvidence{window, processId};
}

GameDetectionTransitionResult GenericForegroundTrigger::ApplyEvidence(
    GameDetectionCoordinator& coordinator,
    const GenericForegroundEvidence& evidence) noexcept
{
    return coordinator.ObserveCandidate(
        evidence.processId, evidence.window,
        GameDetectionTrigger::GenericForeground);
}
}
