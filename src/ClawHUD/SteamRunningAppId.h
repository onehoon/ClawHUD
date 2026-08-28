#pragma once

#include <windows.h>

#include <cstdint>
#include <thread>

class SteamRunningAppIdSource
{
public:
    ~SteamRunningAppIdSource();

    bool Start(HWND dispatchWindow, UINT changedMessage);
    void Stop() noexcept;
    std::uint32_t ReadCurrentAppId() const noexcept;
    bool Running() const noexcept { return stopEvent_ != nullptr; }

private:
    void WatchLoop();

    HWND dispatchWindow_{};
    UINT changedMessage_{};
    HANDLE stopEvent_{};
    std::thread worker_;
};
