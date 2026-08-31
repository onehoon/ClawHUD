#include "HudSettingsStore.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdlib>
#include <cwchar>

#include "HudSize.h"
#include "RuntimeLogger.h"

namespace clawhud
{
namespace
{
std::wstring DefaultSettingsPath()
{
    PWSTR localAppData{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr,
        &localAppData)))
        return {};
    std::wstring path(localAppData);
    CoTaskMemFree(localAppData);
    return path + L"\\ClawHUD\\settings.ini";
}
}

HudSettingsStore::HudSettingsStore() : path_(DefaultSettingsPath()) {}

HudSettingsStore::HudSettingsStore(std::wstring iniPath) : path_(std::move(iniPath)) {}

void HudSettingsStore::EnsureParentDirectory() const
{
    const auto separator = path_.find_last_of(L'\\');
    if (separator != std::wstring::npos)
        CreateDirectoryW(path_.substr(0, separator).c_str(), nullptr);
}

std::wstring HudSettingsStore::ReadString(const wchar_t* key, const wchar_t* fallback) const
{
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"HUD", key, fallback, value, ARRAYSIZE(value), path_.c_str());
    return value;
}

bool HudSettingsStore::ReadBool(const wchar_t* section, const wchar_t* key, bool fallback) const
{
    wchar_t value[16]{};
    GetPrivateProfileStringW(section, key, fallback ? L"1" : L"0", value, ARRAYSIZE(value),
        path_.c_str());
    if (_wcsicmp(value, L"true") == 0) return true;
    if (_wcsicmp(value, L"false") == 0) return false;
    return wcstol(value, nullptr, 10) != 0;
}

HudSettings HudSettingsStore::Load() const
{
    HudSettings settings;
    if (path_.empty())
        return settings;

    settings.hudEnabled = ReadBool(L"HUD", L"Enabled", true);
    settings.debugLoggingEnabled = ReadBool(L"Developer", L"DebugLoggingEnabled", false);
    wchar_t startup[8]{};
    GetPrivateProfileStringW(L"General", L"StartWithWindows", L"1", startup,
        ARRAYSIZE(startup), path_.c_str());
    settings.startWithWindows = std::wcstol(startup, nullptr, 10) != 0;
    const auto alignment = ReadString(L"Alignment", L"Center");
    if (alignment == L"Left") settings.alignment = HudAlignment::Left;
    else if (alignment == L"Right") settings.alignment = HudAlignment::Right;
    settings.font = ParseHudFont(ReadString(L"Font", L"SegoeUIVariable"));
    const auto background = ReadString(L"BackgroundWidth", L"ContentWidth");
    if (background == L"ContentWidth") settings.backgroundMode = HudBackgroundMode::ContentWidth;
    else if (background == L"FullWidth") settings.backgroundMode = HudBackgroundMode::FullWidth;
    const auto visibility = ReadString(L"VisibilityMode", L"Always");
    if (visibility == L"Always") settings.visibilityMode = HudVisibilityMode::Always;
    else if (visibility == L"InGameOnly") settings.visibilityMode = HudVisibilityMode::InGameOnly;
    settings.sizeOffset = ParseHudSizeOffset(ReadString(L"Size", L"0"));
    const auto configuredOpacity = ReadString(L"HudOpacity", L"__missing__");
    const auto legacyOpacity = ReadString(L"BackgroundOpacity", L"");
    settings.backgroundOpacity = HudOpacityFractionFromPercent(
        HudOpacityPercentFromSettings(configuredOpacity,
            configuredOpacity != L"__missing__", legacyOpacity));
    settings.intelVrrRangeFixEnabled = ReadBool(L"Tweaks", L"IntelVrrRangeFixEnabled", true);
    return settings;
}

void HudSettingsStore::Save(const HudSettings& settings) const
{
    if (path_.empty())
        return;
    EnsureParentDirectory();
    const wchar_t* alignment = settings.alignment == HudAlignment::Left ? L"Left" :
        settings.alignment == HudAlignment::Right ? L"Right" : L"Center";
    const wchar_t* background = settings.backgroundMode == HudBackgroundMode::ContentWidth
        ? L"ContentWidth" : L"FullWidth";
    const wchar_t* visibility = settings.visibilityMode == HudVisibilityMode::Always
        ? L"Always" : L"InGameOnly";
    const wchar_t* font = HudFontIniToken(settings.font);
    wchar_t opacity[8]{};
    swprintf_s(opacity, L"%ld", HudOpacityPercentFromFraction(settings.backgroundOpacity));
    bool saved = WritePrivateProfileStringW(L"HUD", L"Alignment", alignment, path_.c_str()) != FALSE;
    saved = WritePrivateProfileStringW(L"HUD", L"Font", font, path_.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"HUD", L"BackgroundWidth", background, path_.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"HUD", L"HudOpacity", opacity, path_.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"HUD", L"VisibilityMode", visibility, path_.c_str()) != FALSE && saved;
    wchar_t size[8]{};
    swprintf_s(size, L"%d", ClampHudSizeOffset(settings.sizeOffset));
    saved = WritePrivateProfileStringW(L"HUD", L"Size", size, path_.c_str()) != FALSE && saved;
    saved = WritePrivateProfileStringW(L"General", L"StartWithWindows",
        settings.startWithWindows ? L"1" : L"0", path_.c_str()) != FALSE && saved;
    if (!saved)
        RuntimeLogger::Log(RuntimeLogLevel::Error, L"Settings save failed");
}

void HudSettingsStore::SaveEnabled(bool enabled) const
{
    if (path_.empty())
        return;
    EnsureParentDirectory();
    if (!WritePrivateProfileStringW(L"HUD", L"Enabled", enabled ? L"1" : L"0", path_.c_str()))
    {
        RuntimeLogger::Log(RuntimeLogLevel::Error, L"Settings save failed key=Enabled");
    }
}

void HudSettingsStore::SaveIntelVrrRangeFixEnabled(bool enabled) const
{
    if (path_.empty())
        return;
    EnsureParentDirectory();
    if (!WritePrivateProfileStringW(L"Tweaks", L"IntelVrrRangeFixEnabled",
        enabled ? L"1" : L"0", path_.c_str()))
        RuntimeLogger::Log(RuntimeLogLevel::Error,
            L"Settings save failed key=IntelVrrRangeFixEnabled");
}
}
