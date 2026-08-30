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

bool IsSameMicrosoftGameProcessInstance(
    const MicrosoftGameProcessIdentity& left,
    const MicrosoftGameProcessIdentity& right) noexcept
{
    return left.processId == right.processId &&
        left.creationTime == right.creationTime;
}

std::optional<MicrosoftGameProcessIdentity>
QueryMicrosoftGameProcessIdentity(DWORD processId) noexcept
{
    if (!processId)
        return std::nullopt;
    const HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return std::nullopt;

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    const bool queried = GetProcessTimes(
        process, &creation, &exit, &kernel, &user) != FALSE;
    CloseHandle(process);
    if (!queried)
        return std::nullopt;

    ULARGE_INTEGER creationValue{};
    creationValue.LowPart = creation.dwLowDateTime;
    creationValue.HighPart = creation.dwHighDateTime;
    return MicrosoftGameProcessIdentity{processId, creationValue.QuadPart};
}

MicrosoftGameTrigger::MicrosoftGameTrigger()
    : probeFunction_([this](DWORD processId)
        {
            return probe_.Inspect(processId);
        }),
      identityQuery_(QueryMicrosoftGameProcessIdentity)
{
}

MicrosoftGameTrigger::MicrosoftGameTrigger(
    ProbeFunction probe, ProcessIdentityQuery identityQuery)
    : probeFunction_(std::move(probe)),
      identityQuery_(std::move(identityQuery))
{
}

std::optional<MicrosoftGameTriggerEvidence>
MicrosoftGameTrigger::InspectWindowEvent(const ProductionWindowEvent& event) noexcept
{
    if (!ShouldInspectMicrosoftGameWindowEvent(event))
        return std::nullopt;
    std::optional<MicrosoftGameProcessIdentity> identity;
    try
    {
        if (identityQuery_)
            identity = identityQuery_(event.processId);
    }
    catch (...)
    {
        // Cache identity is an optimization; retain the full-probe fallback.
    }
    try
    {
        if (identity)
        {
            const auto cached = positiveProcessCache_.find(identity->processId);
            if (cached != positiveProcessCache_.end() &&
                cached->second == identity->creationTime)
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
        if (identity)
        {
            positiveProcessCache_[identity->processId] = identity->creationTime;
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
