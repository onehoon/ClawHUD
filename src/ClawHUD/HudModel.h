#pragma once

#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace clawhud
{
enum class HudVisibilityMode
{
    Always,
    InGameOnly
};

enum class HudAlignment
{
    Left,
    Center,
    Right
};

enum class HudFont
{
    Unispace,
    SegoeUiVariable,
};

enum class HudBackgroundMode
{
    FullWidth,
    ContentWidth
};

struct HudLayoutOptions
{
    HudVisibilityMode visibilityMode{HudVisibilityMode::Always};
    HudAlignment alignment{HudAlignment::Center};
    HudBackgroundMode backgroundMode{HudBackgroundMode::ContentWidth};
    float backgroundOpacity{0.7f};
};

constexpr long kHudOpacityMinimumPercent = 50;
constexpr long kHudOpacityMaximumPercent = 100;
constexpr long kDefaultHudOpacityPercent = 70;
constexpr long kHudOpacityStepPercent = 5;

float HudOpacityFractionFromPercent(long percent) noexcept;
long HudOpacityPercentFromFraction(float opacity) noexcept;
long ClampHudOpacityPercent(long percent) noexcept;
long HudOpacityPercentFromText(std::wstring_view text) noexcept;
long HudOpacityPercentFromSettings(std::wstring_view hudOpacity,
    bool hasHudOpacity, std::wstring_view legacyOpacity) noexcept;

struct HudTelemetrySnapshot
{
    std::optional<std::wstring> graphicsApi;
    std::optional<double> renderFps;
    std::optional<double> trueDisplayedFps;
    std::optional<double> frameTimeMs;

    std::optional<double> cpuUsagePercent;
    std::optional<int> cpuTemperatureC;
    std::optional<double> cpuPackagePowerW;
    std::optional<double> gpuUsagePercent;
    std::optional<double> gpuClockMHz;
    std::optional<int> fan1Rpm;
    std::optional<int> fan2Rpm;
    std::optional<int> batteryPercent;
    std::optional<int> remainingMinutes;
    bool onBattery{};
    bool foregroundGameActive{};
    std::optional<double> presentMonDisplayedFps;
    std::optional<std::uint64_t> gpuMemoryUsedBytes;
    std::optional<std::uint64_t> systemMemoryUsedBytes;
};

enum class HudSegmentKind
{
    Graphics,
    Cpu,
    Gpu,
    Tdp,
    Ram,
    Vram,
    Fan,
    Battery
};

struct HudTextRun
{
    HudSegmentKind kind;
    std::wstring label;
    std::wstring value;
};

bool ShouldShowHud(HudVisibilityMode mode, bool foregroundGameActive) noexcept;
// The production HUD visibility decision used by App::ReconcileHudVisibility
// after its suspended / resume-recovery early-out: a manual override (the F8
// toggle) wins, otherwise the configured mode and the foreground-tracked-process
// state decide. Always hidden when the HUD is off.
bool ResolveHudVisible(bool hudEnabled, std::optional<bool> manualOverride,
    HudVisibilityMode mode, bool foregroundActive) noexcept;
// F8 test-override policy. When the persisted master (Enable HUD) is off the
// hotkey is ignored (std::nullopt) and must not revive the HUD. Otherwise it is
// a non-persistent two-direction toggle keyed off the current visibility:
// visible -> force hide (false), hidden -> force show (true).
std::optional<bool> ResolveHudHotkeyOverride(bool hudEnabled,
    bool currentlyVisible) noexcept;
std::uint8_t HudOpacityByte(float opacityPercent) noexcept;
bool ShouldSampleProductionTelemetry(bool resolvedShow, bool suspended) noexcept;
std::vector<HudTextRun> FormatHud(const HudTelemetrySnapshot& snapshot);
std::wstring JoinHudRuns(const std::vector<HudTextRun>& runs);

HudTelemetrySnapshot MakeGameDcSample();
HudTelemetrySnapshot MakeGameAcSample();
HudTelemetrySnapshot MakeNoGameAlwaysSample();
HudTelemetrySnapshot MakeNoGameInGameOnlySample();
}
