#include "MicrosoftGameTrigger.h"
#include "RuntimeLogger.h"

#include <utility>

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

MicrosoftGameTrigger::MicrosoftGameTrigger(
    KnownGameProcessCache& knownGames) noexcept
    : knownGames_(knownGames), probeFunction_([this](DWORD processId)
        {
            return probe_.Inspect(processId);
        }),
      instanceQuery_(QueryGameProcessInstance)
{
}

MicrosoftGameTrigger::MicrosoftGameTrigger(
    KnownGameProcessCache& knownGames, ProbeFunction probe,
    ProcessInstanceQuery instanceQuery)
    : knownGames_(knownGames), probeFunction_(std::move(probe)),
      instanceQuery_(std::move(instanceQuery))
{
}

std::optional<MicrosoftGameTriggerEvidence>
MicrosoftGameTrigger::InspectWindowEvent(const ProductionWindowEvent& event) noexcept
{
    if (!ShouldInspectMicrosoftGameWindowEvent(event))
        return std::nullopt;
    std::optional<GameProcessInstance> process;
    try
    {
        if (instanceQuery_)
            process = instanceQuery_(event.processId);
    }
    catch (...)
    {
        // Cache identity is an optimization; retain the full-probe fallback.
    }
    try
    {
        if (process)
        {
            const auto cached = knownGames_.Lookup(*process);
            if (cached && cached->microsoftGameIdentity)
            {
                RuntimeLogger::Log(RuntimeLogLevel::Debug,
                    L"[GameDetection] microsoft.identity-cache-hit pid=" +
                    std::to_wstring(event.processId));
                return MicrosoftGameTriggerEvidence{
                    event.sequence, event.window, event.processId};
            }
        }
        const auto result = probeFunction_(event.processId);
        if (!ShouldEmitMicrosoftGameTrigger(result))
            return std::nullopt;
        if (process)
        {
            knownGames_.MarkMicrosoftGame(*process);
            RuntimeLogger::Log(RuntimeLogLevel::Debug,
                L"[GameDetection] microsoft.identity-cache-store pid=" +
                std::to_wstring(event.processId));
        }
        return MicrosoftGameTriggerEvidence{
            event.sequence, event.window, event.processId};
    }
    catch (...)
    {
        return std::nullopt;
    }
}
}
