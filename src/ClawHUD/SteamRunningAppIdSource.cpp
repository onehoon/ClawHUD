#include "SteamRunningAppIdSource.h"

namespace
{
constexpr wchar_t kSteamPath[] = L"Software\\Valve\\Steam";
constexpr wchar_t kValvePath[] = L"Software\\Valve";
constexpr wchar_t kSoftwarePath[] = L"Software";
constexpr wchar_t kRunningAppIdValue[] = L"RunningAppID";

HKEY OpenWatchKey(SteamRunningAppIdWatchTarget& target)
{
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSteamPath, 0, KEY_NOTIFY, &key) == ERROR_SUCCESS)
    {
        target = SteamRunningAppIdWatchTarget::Steam;
        return key;
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kValvePath, 0, KEY_NOTIFY, &key) == ERROR_SUCCESS)
    {
        target = SteamRunningAppIdWatchTarget::Valve;
        return key;
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSoftwarePath, 0, KEY_NOTIFY, &key) == ERROR_SUCCESS)
    {
        target = SteamRunningAppIdWatchTarget::Software;
        return key;
    }
    target = SteamRunningAppIdWatchTarget::None;
    return nullptr;
}
}

SteamRunningAppIdSource::~SteamRunningAppIdSource()
{
    Stop();
}

bool SteamRunningAppIdSource::Start(HWND notifyWindow, UINT notifyMessage)
{
    if (!notifyWindow || notifyMessage == 0 || worker_.joinable()) return false;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) return false;
    notifyWindow_ = notifyWindow;
    notifyMessage_ = notifyMessage;
    try
    {
        worker_ = std::thread(&SteamRunningAppIdSource::WatchLoop, this);
    }
    catch (...)
    {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
        notifyWindow_ = nullptr;
        notifyMessage_ = 0;
        return false;
    }
    return true;
}

void SteamRunningAppIdSource::Stop() noexcept
{
    if (stopEvent_) SetEvent(stopEvent_);
    if (worker_.joinable()) worker_.join();
    if (stopEvent_)
    {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    notifyWindow_ = nullptr;
    notifyMessage_ = 0;
}

std::uint32_t SteamRunningAppIdSource::GetRunningAppId() const noexcept
{
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSteamPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return 0;
    DWORD type{};
    DWORD size = sizeof(DWORD);
    DWORD raw{};
    const auto result = RegQueryValueExW(key, kRunningAppIdValue, nullptr,
        &type, reinterpret_cast<BYTE*>(&raw), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) return 0;
    return RunningAppIdFromRegistryValue(type, reinterpret_cast<const BYTE*>(&raw), size);
}

void SteamRunningAppIdSource::WatchLoop()
{
    HANDLE changeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!changeEvent) return;

    for (;;)
    {
        SteamRunningAppIdWatchTarget target{};
        HKEY key = OpenWatchKey(target);
        if (!key)
        {
            WaitForSingleObject(stopEvent_, INFINITE);
            break;
        }
        ResetEvent(changeEvent);
        const auto notifyResult = RegNotifyChangeKeyValue(key, FALSE,
            SteamRunningAppIdWatchFilter(target), changeEvent, TRUE);
        if (notifyResult != ERROR_SUCCESS)
        {
            RegCloseKey(key);
            break;
        }
        const HANDLE waits[] = { stopEvent_, changeEvent };
        const auto waitResult = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        RegCloseKey(key);
        if (waitResult == WAIT_OBJECT_0) break;
        if (waitResult != WAIT_OBJECT_0 + 1) break;
        PostMessageW(notifyWindow_, notifyMessage_, 0, 0);
    }
    CloseHandle(changeEvent);
}
