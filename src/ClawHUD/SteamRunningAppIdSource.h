#pragma once

#include <windows.h>

#include <cstdint>
#include <atomic>
#include <thread>

enum class SteamRunningAppIdWatchTarget
{
    Steam,
    Valve,
    Software,
    None
};

constexpr SteamRunningAppIdWatchTarget SelectSteamRunningAppIdWatchTarget(
    bool steamKeyAvailable, bool valveKeyAvailable, bool softwareKeyAvailable) noexcept
{
    if (steamKeyAvailable) return SteamRunningAppIdWatchTarget::Steam;
    if (valveKeyAvailable) return SteamRunningAppIdWatchTarget::Valve;
    if (softwareKeyAvailable) return SteamRunningAppIdWatchTarget::Software;
    return SteamRunningAppIdWatchTarget::None;
}

constexpr DWORD SteamRunningAppIdWatchFilter(SteamRunningAppIdWatchTarget target) noexcept
{
    return target == SteamRunningAppIdWatchTarget::Steam
        ? REG_NOTIFY_CHANGE_LAST_SET
        : target == SteamRunningAppIdWatchTarget::None ? 0 : REG_NOTIFY_CHANGE_NAME;
}

constexpr bool IsMoreSpecificSteamRunningAppIdWatchTarget(
    SteamRunningAppIdWatchTarget current, SteamRunningAppIdWatchTarget latest) noexcept
{
    return (current == SteamRunningAppIdWatchTarget::Software &&
        (latest == SteamRunningAppIdWatchTarget::Valve ||
            latest == SteamRunningAppIdWatchTarget::Steam)) ||
        (current == SteamRunningAppIdWatchTarget::Valve &&
            latest == SteamRunningAppIdWatchTarget::Steam);
}

constexpr std::uint32_t RunningAppIdFromRegistryValue(
    DWORD type, const BYTE* data, DWORD size) noexcept
{
    if (type != REG_DWORD || data == nullptr || size != sizeof(DWORD)) return 0;
    DWORD raw{};
    for (DWORD i = 0; i < sizeof(raw); ++i)
        reinterpret_cast<BYTE*>(&raw)[i] = data[i];
    return static_cast<std::uint32_t>(raw);
}

constexpr bool RunningAppIdChanged(std::uint32_t previous, std::uint32_t current) noexcept
{
    return previous != current;
}

class SteamRunningAppIdSource
{
public:
    ~SteamRunningAppIdSource();

    bool Start(HWND notifyWindow, UINT notifyMessage);
    void Stop() noexcept;
    std::uint32_t GetRunningAppId() const noexcept;

private:
    void WatchLoop();

    HWND notifyWindow_{};
    UINT notifyMessage_{};
    HANDLE stopEvent_{};
    HANDLE readyEvent_{};
    std::atomic_bool watchArmed_{};
    std::thread worker_;
};
