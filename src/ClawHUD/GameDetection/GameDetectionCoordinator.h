#pragma once

#include <windows.h>

#include <cstdint>

namespace clawhud
{
enum class GameDetectionTrigger
{
    GenericForeground,
    SteamRunningAppId,
    MicrosoftGameIdentity
};

enum class GameDetectionState
{
    Idle,
    Armed,
    Verifying,
    Ready,
    Committed
};

struct GameDetectionWake
{
    GameDetectionTrigger trigger{GameDetectionTrigger::GenericForeground};
    DWORD processId{};
    HWND window{};
    std::uint32_t steamAppId{};
    bool microsoftGameIdentity{};
};

struct GameDetectionEvidence
{
    bool genericForeground{};
    bool steamSession{};
    bool microsoftGameIdentity{};
};

struct GameDetectionContext
{
    GameDetectionState state{GameDetectionState::Idle};
    std::uint64_t generation{};
    DWORD candidateProcessId{};
    HWND candidateWindow{};
    std::uint32_t steamAppId{};
    bool microsoftGameIdentity{};
    bool rendererObserved{};
    GameDetectionTrigger primaryTrigger{GameDetectionTrigger::GenericForeground};
    GameDetectionEvidence evidence{};
};

enum class GameDetectionTransition
{
    None,
    Armed,
    CandidateStarted,
    CandidateUpdated,
    CandidateReplaced,
    RendererReady,
    Committed,
    Reset
};

struct GameDetectionTransitionResult
{
    GameDetectionTransition transition{GameDetectionTransition::None};
    std::uint64_t generation{};
    DWORD processId{};
};

class GameDetectionCoordinator
{
public:
    GameDetectionTransitionResult ObserveWake(const GameDetectionWake& wake) noexcept;
    GameDetectionTransitionResult ObserveCandidate(
        DWORD processId, HWND window, GameDetectionTrigger trigger) noexcept;
    bool MarkRendererReady(DWORD processId, std::uint64_t generation) noexcept;
    bool CommitCandidate(DWORD processId, std::uint64_t generation) noexcept;
    void ClearSteamSession(std::uint32_t appId = 0) noexcept;
    GameDetectionTransitionResult Reset() noexcept;

    const GameDetectionContext& Context() const noexcept { return context_; }

private:
    void MergeEvidence(GameDetectionTrigger trigger, bool microsoftGameIdentity) noexcept;
    GameDetectionTransitionResult Result(
        GameDetectionTransition transition) const noexcept;
    void StartCandidate(DWORD processId, HWND window,
        GameDetectionTrigger trigger) noexcept;

    GameDetectionContext context_{};
};
}
