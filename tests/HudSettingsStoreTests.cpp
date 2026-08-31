#include "HudSettingsStore.h"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

// A settings.ini path inside a fresh temp directory that is removed on scope exit.
class TempIni
{
public:
    TempIni()
    {
        wchar_t base[MAX_PATH]{};
        GetTempPathW(MAX_PATH, base);
        dir_ = std::filesystem::path(base) /
            (L"clawhud-settings-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(dir_);
        path_ = (dir_ / L"settings.ini").wstring();
    }
    ~TempIni()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    const std::wstring& path() const { return path_; }
    void writeRaw(const wchar_t* section, const wchar_t* key, const wchar_t* value) const
    {
        WritePrivateProfileStringW(section, key, value, path_.c_str());
    }

private:
    std::filesystem::path dir_;
    std::wstring path_;
};

void AvailabilityReflectsPath(bool& ok)
{
    ok &= Check(!HudSettingsStore(L"").Available(), "an empty path is unavailable");
    TempIni ini;
    ok &= Check(HudSettingsStore(ini.path()).Available(), "a real path is available");
}

void LoadDefaultsWhenUnavailable(bool& ok)
{
    const auto s = HudSettingsStore(L"").Load();
    ok &= Check(s.hudEnabled, "default hudEnabled is true");
    ok &= Check(!s.debugLoggingEnabled, "default debugLoggingEnabled is false");
    ok &= Check(s.startWithWindows, "default startWithWindows is true");
    ok &= Check(s.intelVrrRangeFixEnabled, "default intelVrrRangeFixEnabled is true");
    ok &= Check(s.alignment == HudAlignment::Center, "default alignment is Center");
    ok &= Check(s.font == HudFont::Unispace, "default font is Unispace");
    ok &= Check(s.backgroundMode == HudBackgroundMode::FullWidth, "default background is FullWidth");
    ok &= Check(s.visibilityMode == HudVisibilityMode::InGameOnly, "default visibility is InGameOnly");
    ok &= Check(s.sizeOffset == 0, "default size offset is 0");
}

void LoadDefaultsWhenFileMissing(bool& ok)
{
    TempIni ini; // no file written
    const auto s = HudSettingsStore(ini.path()).Load();
    ok &= Check(s.hudEnabled && s.startWithWindows && s.intelVrrRangeFixEnabled,
        "missing keys fall back to their defaults");
    ok &= Check(s.alignment == HudAlignment::Center &&
        s.visibilityMode == HudVisibilityMode::InGameOnly,
        "missing enum keys fall back to their defaults");
}

void SaveThenLoadRoundTrips(bool& ok)
{
    TempIni ini;
    HudSettingsStore store(ini.path());

    HudSettings out;
    out.alignment = HudAlignment::Right;
    out.font = HudFont::SegoeUiVariable;
    out.backgroundMode = HudBackgroundMode::ContentWidth;
    out.visibilityMode = HudVisibilityMode::Always;
    out.backgroundOpacity = HudOpacityFractionFromPercent(85);
    out.sizeOffset = 2;
    out.startWithWindows = false;
    store.Save(out);

    const auto in = store.Load();
    ok &= Check(in.alignment == HudAlignment::Right, "alignment round-trips");
    ok &= Check(in.font == HudFont::SegoeUiVariable, "font round-trips");
    ok &= Check(in.backgroundMode == HudBackgroundMode::ContentWidth, "background round-trips");
    ok &= Check(in.visibilityMode == HudVisibilityMode::Always, "visibility round-trips");
    ok &= Check(HudOpacityPercentFromFraction(in.backgroundOpacity) == 85, "opacity round-trips");
    ok &= Check(in.sizeOffset == 2, "size offset round-trips");
    ok &= Check(!in.startWithWindows, "startWithWindows round-trips");
}

void SaveDoesNotTouchEnabledOrDebugLogging(bool& ok)
{
    TempIni ini;
    HudSettingsStore store(ini.path());
    ini.writeRaw(L"HUD", L"Enabled", L"0");
    ini.writeRaw(L"Developer", L"DebugLoggingEnabled", L"true");

    HudSettings out;
    out.debugLoggingEnabled = false; // Save() must not write this back
    store.Save(out);

    const auto in = store.Load();
    ok &= Check(!in.hudEnabled, "Save() leaves HUD/Enabled untouched");
    ok &= Check(in.debugLoggingEnabled, "Save() leaves Developer/DebugLoggingEnabled untouched");
}

void DebugLoggingEnabledAcceptsTrueFalseText(bool& ok)
{
    TempIni ini;
    HudSettingsStore store(ini.path());
    ini.writeRaw(L"Developer", L"DebugLoggingEnabled", L"true");
    ok &= Check(store.Load().debugLoggingEnabled, "DebugLoggingEnabled=true (text) reads true");
    ini.writeRaw(L"Developer", L"DebugLoggingEnabled", L"FALSE");
    ok &= Check(!store.Load().debugLoggingEnabled, "DebugLoggingEnabled=FALSE (text) reads false");
    ini.writeRaw(L"Developer", L"DebugLoggingEnabled", L"1");
    ok &= Check(store.Load().debugLoggingEnabled, "DebugLoggingEnabled=1 still reads true");
}

void SaveEnabledWritesOnlyEnabled(bool& ok)
{
    TempIni ini;
    HudSettingsStore store(ini.path());
    store.SaveEnabled(false);
    ok &= Check(!store.Load().hudEnabled, "SaveEnabled(false) persists");
    store.SaveEnabled(true);
    ok &= Check(store.Load().hudEnabled, "SaveEnabled(true) persists");
}

void SaveIntelVrrRangeFixPersists(bool& ok)
{
    TempIni ini;
    HudSettingsStore store(ini.path());
    store.SaveIntelVrrRangeFixEnabled(false);
    ok &= Check(!store.Load().intelVrrRangeFixEnabled, "SaveIntelVrrRangeFixEnabled(false) persists");
    store.SaveIntelVrrRangeFixEnabled(true);
    ok &= Check(store.Load().intelVrrRangeFixEnabled, "SaveIntelVrrRangeFixEnabled(true) persists");
}

void LegacyOpacityKeyIsHonored(bool& ok)
{
    TempIni ini;
    HudSettingsStore store(ini.path());
    ini.writeRaw(L"HUD", L"BackgroundOpacity", L"60");
    ok &= Check(HudOpacityPercentFromFraction(store.Load().backgroundOpacity) == 60,
        "legacy BackgroundOpacity is used when HudOpacity is absent");

    ini.writeRaw(L"HUD", L"HudOpacity", L"90");
    ok &= Check(HudOpacityPercentFromFraction(store.Load().backgroundOpacity) == 90,
        "HudOpacity takes precedence over the legacy key");
}

void ReadsExplicitKeys(bool& ok)
{
    TempIni ini;
    HudSettingsStore store(ini.path());
    ini.writeRaw(L"HUD", L"Enabled", L"0");
    ini.writeRaw(L"Developer", L"DebugLoggingEnabled", L"1");
    ini.writeRaw(L"General", L"StartWithWindows", L"0");
    ini.writeRaw(L"Tweaks", L"IntelVrrRangeFixEnabled", L"0");
    ini.writeRaw(L"HUD", L"Alignment", L"Left");

    const auto s = store.Load();
    ok &= Check(!s.hudEnabled, "Enabled=0 read");
    ok &= Check(s.debugLoggingEnabled, "DebugLoggingEnabled=1 read");
    ok &= Check(!s.startWithWindows, "StartWithWindows=0 read");
    ok &= Check(!s.intelVrrRangeFixEnabled, "IntelVrrRangeFixEnabled=0 read");
    ok &= Check(s.alignment == HudAlignment::Left, "Alignment=Left read");
}
}

int main()
{
    bool ok = true;
    AvailabilityReflectsPath(ok);
    LoadDefaultsWhenUnavailable(ok);
    LoadDefaultsWhenFileMissing(ok);
    SaveThenLoadRoundTrips(ok);
    SaveDoesNotTouchEnabledOrDebugLogging(ok);
    DebugLoggingEnabledAcceptsTrueFalseText(ok);
    SaveEnabledWritesOnlyEnabled(ok);
    SaveIntelVrrRangeFixPersists(ok);
    LegacyOpacityKeyIsHonored(ok);
    ReadsExplicitKeys(ok);
    return ok ? 0 : 1;
}
