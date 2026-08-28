#include "SteamRunningAppId.h"
#include "SteamGameSession.h"

#include <string>

namespace
{
constexpr REGSAM kReadNotify = KEY_QUERY_VALUE | KEY_NOTIFY;
constexpr wchar_t kSteamPath[] = L"Software\\Valve\\Steam";
constexpr wchar_t kValvePath[] = L"Software\\Valve";
constexpr wchar_t kSoftwarePath[] = L"Software";

enum class WatchKind { Steam, Valve, Software };

HKEY OpenKey(const wchar_t* path)
{
    HKEY key{};
    return RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, kReadNotify, &key) == ERROR_SUCCESS
        ? key : nullptr;
}

HKEY OpenWatchKey(WatchKind& kind)
{
    if (const auto key = OpenKey(kSteamPath))
    {
        kind = WatchKind::Steam;
        return key;
    }
    if (const auto key = OpenKey(kValvePath))
    {
        kind = WatchKind::Valve;
        return key;
    }
    if (const auto key = OpenKey(kSoftwarePath))
    {
        kind = WatchKind::Software;
        return key;
    }
    return nullptr;
}

std::uint32_t ReadAppId(HKEY steamKey) noexcept
{
    if (!steamKey)
        return 0;
    DWORD value{};
    DWORD size = sizeof(value);
    DWORD type{};
    if (RegQueryValueExW(steamKey, L"RunningAppID", nullptr, &type,
        reinterpret_cast<LPBYTE>(&value), &size) != ERROR_SUCCESS ||
        type != REG_DWORD || size != sizeof(value))
        return 0;
    return DecodeSteamRunningAppId(value);
}
}

SteamRunningAppIdSource::~SteamRunningAppIdSource()
{
    Stop();
}

bool SteamRunningAppIdSource::Start(HWND dispatchWindow, UINT changedMessage)
{
    Stop();
    if (!dispatchWindow || !changedMessage)
        return false;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_)
        return false;
    dispatchWindow_ = dispatchWindow;
    changedMessage_ = changedMessage;
    try
    {
        worker_ = std::thread(&SteamRunningAppIdSource::WatchLoop, this);
    }
    catch (...)
    {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
        dispatchWindow_ = nullptr;
        changedMessage_ = 0;
        return false;
    }
    return true;
}

void SteamRunningAppIdSource::Stop() noexcept
{
    if (stopEvent_)
        SetEvent(stopEvent_);
    if (worker_.joinable())
        worker_.join();
    if (stopEvent_)
        CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
    dispatchWindow_ = nullptr;
    changedMessage_ = 0;
}

std::uint32_t SteamRunningAppIdSource::ReadCurrentAppId() const noexcept
{
    const auto key = OpenKey(kSteamPath);
    if (!key)
        return 0;
    const auto value = ReadAppId(key);
    RegCloseKey(key);
    return value;
}

void SteamRunningAppIdSource::WatchLoop()
{
    WatchKind previousKind{};
    bool hadPreviousKind = false;
    for (;;)
    {
        WatchKind kind{};
        HKEY key = OpenWatchKey(kind);
        if (!key)
        {
            if (WaitForSingleObject(stopEvent_, 500) == WAIT_OBJECT_0)
                return;
            continue;
        }

        const bool steamBecameAvailable = kind == WatchKind::Steam &&
            hadPreviousKind && previousKind != WatchKind::Steam;
        previousKind = kind;
        hadPreviousKind = true;
        if (steamBecameAvailable && !PostMessageW(dispatchWindow_, changedMessage_,
            static_cast<WPARAM>(ReadAppId(key)), 0))
        {
            RegCloseKey(key);
            return;
        }

        const HANDLE changed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!changed)
        {
            RegCloseKey(key);
            return;
        }
        const DWORD filter = kind == WatchKind::Steam
            ? REG_NOTIFY_CHANGE_LAST_SET : REG_NOTIFY_CHANGE_NAME;
        const LONG notify = RegNotifyChangeKeyValue(key, FALSE, filter,
            changed, TRUE);
        if (notify != ERROR_SUCCESS)
        {
            RegCloseKey(key);
            CloseHandle(changed);
            return;
        }
        if (kind == WatchKind::Steam)
        {
            if (!PostMessageW(dispatchWindow_, changedMessage_,
                static_cast<WPARAM>(ReadAppId(key)), 0))
            {
                RegCloseKey(key);
                CloseHandle(changed);
                return;
            }
        }
        else
        {
            WatchKind latestKind{};
            if (const auto latestKey = OpenWatchKey(latestKind))
            {
                RegCloseKey(latestKey);
                if (latestKind == WatchKind::Steam)
                {
                    RegCloseKey(key);
                    CloseHandle(changed);
                    continue;
                }
            }
        }
        const HANDLE handles[] = { stopEvent_, changed };
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        const auto appId = wait == WAIT_OBJECT_0 + 1 && kind == WatchKind::Steam
            ? ReadAppId(key) : 0;
        RegCloseKey(key);
        CloseHandle(changed);
        if (wait == WAIT_OBJECT_0)
            return;
        if (wait != WAIT_OBJECT_0 + 1)
            return;
        if (kind == WatchKind::Steam && !PostMessageW(dispatchWindow_, changedMessage_,
            static_cast<WPARAM>(appId), 0))
            return;
    }
}
