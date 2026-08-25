#pragma once

#include <optional>
#include <string>
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
    float backgroundOpacity{0.5f};
};

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
    std::optional<int> gpuTemperatureC;
    std::optional<int> fan1Rpm;
    std::optional<int> fan2Rpm;
    std::optional<int> batteryPercent;
    std::optional<double> systemPowerW;
    std::optional<int> remainingMinutes;
    bool onBattery{};
    bool foregroundGameActive{};
    std::optional<double> presentMonDisplayedFps;
};

enum class HudSegmentKind
{
    Graphics,
    Cpu,
    Gpu,
    Tdp,
    SystemPower,
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
std::vector<HudTextRun> FormatHud(const HudTelemetrySnapshot& snapshot);
std::wstring JoinHudRuns(const std::vector<HudTextRun>& runs);

HudTelemetrySnapshot MakeGameDcSample();
HudTelemetrySnapshot MakeGameAcSample();
HudTelemetrySnapshot MakeNoGameAlwaysSample();
HudTelemetrySnapshot MakeNoGameInGameOnlySample();
}
