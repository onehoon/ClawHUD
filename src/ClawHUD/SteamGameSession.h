#pragma once

#include <windows.h>

#include <cstdint>

enum class SteamGameState
{
    None,
    Resolving,
    Active
};

struct SteamAppIdTransition
{
    bool shouldHandle{};
    bool firstObservation{};
    bool freshLaunch{};
    bool allowBaselineRenderer{};
    SteamGameState state{ SteamGameState::None };
};

constexpr bool SteamOwnsGameSession(std::uint32_t runningAppId) noexcept
{
    return runningAppId != 0;
}

constexpr SteamGameState ResolveSteamGameState(
    std::uint32_t runningAppId, DWORD rendererPid) noexcept
{
    if (!runningAppId)
        return SteamGameState::None;
    return rendererPid ? SteamGameState::Active : SteamGameState::Resolving;
}

constexpr std::uint32_t DecodeSteamRunningAppId(DWORD rawValue) noexcept
{
    return static_cast<std::uint32_t>(rawValue);
}

constexpr SteamAppIdTransition EvaluateSteamAppIdTransition(
    bool initialized, std::uint32_t currentAppId, std::uint32_t nextAppId) noexcept
{
    if (initialized && currentAppId == nextAppId)
        return {};
    const bool firstObservation = !initialized;
    const bool freshLaunch = initialized && nextAppId != 0 &&
        currentAppId != nextAppId;
    return { true, firstObservation, freshLaunch,
        firstObservation || !freshLaunch,
        nextAppId ? SteamGameState::Resolving : SteamGameState::None };
}

constexpr bool ShouldRunSteamRendererResolution(
    bool hudEnabled, SteamGameState state, std::uint32_t appId,
    bool suspended, bool diagnosticRunning) noexcept
{
    return hudEnabled && state == SteamGameState::Resolving && appId != 0 &&
        !suspended && !diagnosticRunning;
}

constexpr bool ShouldWaitForSteamCandidateProbe(
    DWORD currentCandidatePid, DWORD bestCandidatePid, bool probeExpired) noexcept
{
    return currentCandidatePid != 0 && currentCandidatePid == bestCandidatePid &&
        !probeExpired;
}
