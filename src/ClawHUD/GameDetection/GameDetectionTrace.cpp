#include "GameDetectionTrace.h"

namespace clawhud
{
std::wstring_view GameDetectionStateName(GameDetectionState state) noexcept
{
    switch (state)
    {
    case GameDetectionState::Idle: return L"Idle";
    case GameDetectionState::Armed: return L"Armed";
    case GameDetectionState::Verifying: return L"Verifying";
    case GameDetectionState::Ready: return L"Ready";
    case GameDetectionState::Committed: return L"Committed";
    }
    return L"Unknown";
}

std::wstring_view GameDetectionTriggerName(GameDetectionTrigger trigger) noexcept
{
    switch (trigger)
    {
    case GameDetectionTrigger::GenericForeground: return L"Generic";
    case GameDetectionTrigger::SteamRunningAppId: return L"Steam";
    case GameDetectionTrigger::MicrosoftGameIdentity: return L"MicrosoftGame";
    }
    return L"Unknown";
}

std::wstring_view GameDetectionTransitionName(
    GameDetectionTransition transition) noexcept
{
    switch (transition)
    {
    case GameDetectionTransition::None: return L"None";
    case GameDetectionTransition::Armed: return L"Armed";
    case GameDetectionTransition::CandidateStarted: return L"CandidateStarted";
    case GameDetectionTransition::CandidateUpdated: return L"CandidateUpdated";
    case GameDetectionTransition::CandidateReplaced: return L"CandidateReplaced";
    case GameDetectionTransition::CandidateCleared: return L"CandidateCleared";
    case GameDetectionTransition::RendererReady: return L"RendererReady";
    case GameDetectionTransition::Committed: return L"Committed";
    case GameDetectionTransition::Reset: return L"Reset";
    }
    return L"Unknown";
}
}
