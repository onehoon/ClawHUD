#pragma once

#include <string>

#include "HudModel.h"

namespace clawhud
{
// The values persisted in %LOCALAPPDATA%\ClawHUD\settings.ini that the HUD reads
// at startup. Defaults match a missing ini file / missing key.
struct HudSettings
{
    bool hudEnabled = true;
    // [Developer] DebugLoggingEnabled. Developer-only; read at startup, never written back.
    bool debugLoggingEnabled = false;
    bool startWithWindows = true;
    bool intelVrrRangeFixEnabled = true;
    HudAlignment alignment = HudAlignment::Center;
    HudFont font = HudFont::SegoeUiVariable;
    HudBackgroundMode backgroundMode = HudBackgroundMode::ContentWidth;
    HudVisibilityMode visibilityMode = HudVisibilityMode::Always;
    float backgroundOpacity = 0.7f;
    int sizeOffset = 0;
};

// Reads and writes settings.ini. The default constructor targets the real
// per-user path; the path-taking constructor is for tests.
class HudSettingsStore
{
public:
    HudSettingsStore();
    explicit HudSettingsStore(std::wstring iniPath);

    bool Available() const noexcept { return !path_.empty(); }
    const std::wstring& Path() const noexcept { return path_; }

    // Returns defaults when Available() is false; otherwise every field is read
    // from the ini with its documented fallback.
    HudSettings Load() const;

    // Writes the layout / general subset. hudEnabled and the developer-only
    // [Developer] DebugLoggingEnabled key are not written here. No-op when unavailable.
    void Save(const HudSettings& settings) const;

    void SaveEnabled(bool enabled) const;
    void SaveIntelVrrRangeFixEnabled(bool enabled) const;

private:
    void EnsureParentDirectory() const;
    std::wstring ReadString(const wchar_t* key, const wchar_t* fallback) const;
    bool ReadBool(const wchar_t* section, const wchar_t* key, bool fallback) const;

    std::wstring path_;
};
}
