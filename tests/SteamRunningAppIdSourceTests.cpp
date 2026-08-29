#include "SteamRunningAppIdSource.h"

#include <iostream>

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool ok = true;
    const DWORD zero = 0;
    const DWORD normal = 2344520;
    const DWORD highBit = 0xF1234567;
    ok &= Check(RunningAppIdFromRegistryValue(REG_DWORD,
        reinterpret_cast<const BYTE*>(&zero), sizeof(zero)) == 0,
        "REG_DWORD zero returns zero");
    ok &= Check(RunningAppIdFromRegistryValue(REG_DWORD,
        reinterpret_cast<const BYTE*>(&normal), sizeof(normal)) == normal,
        "normal REG_DWORD preserves value");
    ok &= Check(RunningAppIdFromRegistryValue(REG_DWORD,
        reinterpret_cast<const BYTE*>(&highBit), sizeof(highBit)) == highBit,
        "high-bit REG_DWORD preserves bit pattern");
    ok &= Check(RunningAppIdFromRegistryValue(REG_SZ,
        reinterpret_cast<const BYTE*>(&normal), sizeof(normal)) == 0,
        "non-DWORD is ignored");
    ok &= Check(!RunningAppIdChanged(42, 42) && RunningAppIdChanged(42, 43),
        "unchanged value has no state transition");
    ok &= Check(SelectSteamRunningAppIdWatchTarget(true, true, true) ==
        SteamRunningAppIdWatchTarget::Steam &&
        SteamRunningAppIdWatchFilter(SteamRunningAppIdWatchTarget::Steam) ==
            REG_NOTIFY_CHANGE_LAST_SET,
        "Steam key uses LAST_SET");
    ok &= Check(SelectSteamRunningAppIdWatchTarget(false, true, true) ==
        SteamRunningAppIdWatchTarget::Valve &&
        SteamRunningAppIdWatchFilter(SteamRunningAppIdWatchTarget::Valve) ==
            REG_NOTIFY_CHANGE_NAME,
        "Valve fallback uses NAME");
    ok &= Check(SelectSteamRunningAppIdWatchTarget(false, false, true) ==
        SteamRunningAppIdWatchTarget::Software &&
        SteamRunningAppIdWatchFilter(SteamRunningAppIdWatchTarget::Software) ==
            REG_NOTIFY_CHANGE_NAME,
        "Software fallback uses NAME");
    ok &= Check(SelectSteamRunningAppIdWatchTarget(false, false, false) ==
        SteamRunningAppIdWatchTarget::None,
        "missing registry hierarchy has no key target");

    SteamRunningAppIdSource source;
    ok &= Check(source.Start(reinterpret_cast<HWND>(static_cast<ULONG_PTR>(1)), WM_APP + 1),
        "watcher starts");
    const auto firstRead = source.GetRunningAppId();
    const auto secondRead = source.GetRunningAppId();
    ok &= Check(firstRead == secondRead,
        "consumer reads the current registry value");
    source.Stop();
    source.Stop();
    ok &= Check(!RunningAppIdChanged(firstRead, firstRead),
        "duplicate notification is harmless");
    return ok ? 0 : 1;
}
