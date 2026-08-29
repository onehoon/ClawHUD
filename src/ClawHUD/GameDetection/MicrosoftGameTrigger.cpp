#include "MicrosoftGameTrigger.h"

namespace clawhud
{
bool ShouldInspectMicrosoftGameWindowEvent(
    const ProductionWindowEvent& event) noexcept
{
    return (event.type == ProductionWindowEventType::Create ||
        event.type == ProductionWindowEventType::Show) &&
        event.processId != 0 && event.immediateTopLevel;
}

bool ShouldEmitMicrosoftGameTrigger(
    const WindowsGameIdentityProbeResult& result) noexcept
{
    return HasReadableMicrosoftGameExecutableMatch(result);
}

std::optional<MicrosoftGameTriggerEvidence>
MicrosoftGameTrigger::InspectWindowEvent(const ProductionWindowEvent& event) noexcept
{
    if (!ShouldInspectMicrosoftGameWindowEvent(event))
        return std::nullopt;
    try
    {
        const auto result = probe_.Inspect(event.processId);
        if (!ShouldEmitMicrosoftGameTrigger(result))
            return std::nullopt;
        return MicrosoftGameTriggerEvidence{
            event.sequence, event.window, event.processId};
    }
    catch (...)
    {
        return std::nullopt;
    }
}

GameDetectionTransitionResult MicrosoftGameTrigger::ApplyEvidence(
    GameDetectionCoordinator& coordinator,
    const MicrosoftGameTriggerEvidence& evidence) noexcept
{
    return coordinator.ObserveWake({
        GameDetectionTrigger::MicrosoftGameIdentity,
        evidence.processId,
        evidence.window,
        0,
        true});
}
}
