#pragma once

#include <windows.h>

#include <cstdint>

enum class SteamGameState
{
    None,
    Resolving,
    Active
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
