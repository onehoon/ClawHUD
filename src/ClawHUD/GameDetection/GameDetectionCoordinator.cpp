#include "GameDetectionCoordinator.h"

namespace clawhud
{
namespace
{
bool HasCandidate(const GameDetectionContext& context) noexcept
{
    return context.candidateProcessId != 0;
}
}

GameDetectionTransitionResult GameDetectionCoordinator::Result(
    GameDetectionTransition transition) const noexcept
{
    return {transition, context_.generation, context_.candidateProcessId};
}

void GameDetectionCoordinator::MergeEvidence(
    GameDetectionTrigger trigger, bool microsoftGameIdentity) noexcept
{
    switch (trigger)
    {
    case GameDetectionTrigger::GenericForeground:
        context_.evidence.genericForeground = true;
        break;
    case GameDetectionTrigger::SteamRunningAppId:
        context_.evidence.steamSession = true;
        break;
    case GameDetectionTrigger::MicrosoftGameIdentity:
        context_.evidence.microsoftGameIdentity = true;
        break;
    }
    if (microsoftGameIdentity)
    {
        context_.microsoftGameIdentity = true;
        context_.evidence.microsoftGameIdentity = true;
    }
}

void GameDetectionCoordinator::StartCandidate(
    DWORD processId, HWND window, GameDetectionTrigger trigger) noexcept
{
    ++context_.generation;
    context_.candidateProcessId = processId;
    context_.candidateWindow = window;
    context_.rendererObserved = false;
    context_.state = GameDetectionState::Verifying;
    context_.primaryTrigger = trigger;
}

GameDetectionTransitionResult GameDetectionCoordinator::ObserveWake(
    const GameDetectionWake& wake) noexcept
{
    if (wake.steamAppId != 0)
        context_.steamAppId = wake.steamAppId;
    MergeEvidence(wake.trigger, wake.microsoftGameIdentity);

    if (wake.processId == 0)
    {
        if (wake.trigger == GameDetectionTrigger::SteamRunningAppId &&
            context_.state == GameDetectionState::Idle)
        {
            context_.state = GameDetectionState::Armed;
            context_.primaryTrigger = wake.trigger;
            return Result(GameDetectionTransition::Armed);
        }
        return Result(GameDetectionTransition::None);
    }
    return ObserveCandidate(wake.processId, wake.window, wake.trigger);
}

GameDetectionTransitionResult GameDetectionCoordinator::ObserveCandidate(
    DWORD processId, HWND window, GameDetectionTrigger trigger) noexcept
{
    if (processId == 0)
        return Result(GameDetectionTransition::None);
    MergeEvidence(trigger, trigger == GameDetectionTrigger::MicrosoftGameIdentity);

    if (!HasCandidate(context_))
    {
        StartCandidate(processId, window, trigger);
        return Result(GameDetectionTransition::CandidateStarted);
    }

    if (context_.candidateProcessId != processId)
    {
        StartCandidate(processId, window, trigger);
        return Result(GameDetectionTransition::CandidateReplaced);
    }

    if (window != nullptr)
        context_.candidateWindow = window;
    if (context_.state == GameDetectionState::Armed)
        context_.state = GameDetectionState::Verifying;
    return Result(GameDetectionTransition::CandidateUpdated);
}

bool GameDetectionCoordinator::MarkRendererReady(
    DWORD processId, std::uint64_t generation) noexcept
{
    if (context_.candidateProcessId != processId || context_.generation != generation)
        return false;
    if (context_.state == GameDetectionState::Ready)
        return true;
    if (context_.state != GameDetectionState::Verifying)
        return false;
    context_.rendererObserved = true;
    context_.state = GameDetectionState::Ready;
    return true;
}

bool GameDetectionCoordinator::CommitCandidate(
    DWORD processId, std::uint64_t generation) noexcept
{
    if (context_.state != GameDetectionState::Ready ||
        context_.candidateProcessId != processId || context_.generation != generation)
        return false;
    context_.state = GameDetectionState::Committed;
    return true;
}

void GameDetectionCoordinator::ClearSteamSession(std::uint32_t appId) noexcept
{
    if (appId == 0 || context_.steamAppId == appId)
        context_.steamAppId = 0;
}

GameDetectionTransitionResult GameDetectionCoordinator::Reset() noexcept
{
    ++context_.generation;
    context_.state = GameDetectionState::Idle;
    context_.candidateProcessId = 0;
    context_.candidateWindow = nullptr;
    context_.steamAppId = 0;
    context_.microsoftGameIdentity = false;
    context_.rendererObserved = false;
    context_.primaryTrigger = GameDetectionTrigger::GenericForeground;
    context_.evidence = {};
    return Result(GameDetectionTransition::Reset);
}
}
