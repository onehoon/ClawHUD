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
    HudVisibilityMode visibilityMode{HudVisibilityMode::InGameOnly};
    HudAlignment alignment{HudAlignment::Center};
    HudBackgroundMode backgroundMode{HudBackgroundMode::FullWidth};
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
// The mock/production HUD visibility decision used by App::ReconcileHudVisibility
// after its suspended / resume-recovery early-out: a manual override (F8 toggle,
// a diagnostic request) wins, otherwise the configured mode and the
// foreground-tracked-process state decide. Always hidden when the HUD is off.
bool ResolveHudVisible(bool mockHudEnabled, std::optional<bool> manualOverride,
    HudVisibilityMode mode, bool foregroundActive) noexcept;
std::uint8_t HudOpacityByte(float opacityPercent) noexcept;
bool ShouldSampleProductionTelemetry(bool resolvedShow, bool diagnosticMode,
    bool suspended) noexcept;
std::vector<HudTextRun> FormatHud(const HudTelemetrySnapshot& snapshot);
std::wstring JoinHudRuns(const std::vector<HudTextRun>& runs);

HudTelemetrySnapshot MakeGameDcSample();
HudTelemetrySnapshot MakeGameAcSample();
HudTelemetrySnapshot MakeNoGameAlwaysSample();
HudTelemetrySnapshot MakeNoGameInGameOnlySample();
}
