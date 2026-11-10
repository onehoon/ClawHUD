#include "HudModel.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace clawhud
{
namespace
{
std::wstring Number(double value)
{
    std::wostringstream output;
    if (std::abs(value - std::round(value)) < 0.001)
        output << static_cast<long long>(std::llround(value));
    else
        output << std::fixed << std::setprecision(1) << value;
    return output.str();
}

std::wstring Integer(double value)
{
    return std::to_wstring(static_cast<long long>(std::lround(value)));
}

std::wstring Gigabytes(std::uint64_t bytes)
{
    std::wostringstream output;
    output << std::fixed << std::setprecision(1)
        << (static_cast<double>(bytes) / 1024.0 / 1024.0 / 1024.0);
    return output.str();
}

void Add(std::vector<HudTextRun>& runs, HudSegmentKind kind,
    std::wstring label, std::wstring value)
{
    runs.push_back({kind, std::move(label), std::move(value)});
}

std::wstring CpuValue(const HudTelemetrySnapshot& snapshot)
{
    std::wstring value;
    if (snapshot.cpuUsagePercent)
        value = Integer(*snapshot.cpuUsagePercent) + L"%";
    if (snapshot.cpuClockMHz && std::isfinite(*snapshot.cpuClockMHz) &&
        *snapshot.cpuClockMHz >= 0.0 &&
        *snapshot.cpuClockMHz < static_cast<double>(std::numeric_limits<long>::max()))
    {
        if (!value.empty())
            value += L" ";
        value += Integer(*snapshot.cpuClockMHz) + L"MHz";
    }
    if (snapshot.cpuTemperatureC)
    {
        if (!value.empty())
            value += L" ";
        value += std::to_wstring(*snapshot.cpuTemperatureC) + L"\u00B0C";
    }
    return value;
}

std::wstring GpuValue(const HudTelemetrySnapshot& snapshot)
{
    std::wstring value;
    if (snapshot.gpuUsagePercent)
        value = Integer(*snapshot.gpuUsagePercent) + L"%";
    if (snapshot.gpuClockMHz)
    {
        if (!value.empty())
            value += L" ";
        value += Integer(*snapshot.gpuClockMHz) + L"MHz";
    }
    if (!value.empty())
        value += L" ";
    if (snapshot.gpuPowerW && std::isfinite(*snapshot.gpuPowerW) &&
        *snapshot.gpuPowerW >= 0.0 &&
        *snapshot.gpuPowerW < static_cast<double>(std::numeric_limits<long>::max()))
        value += Integer(*snapshot.gpuPowerW) + L"W";
    else
        value += L"--W";
    return value;
}

bool IsValidUsage(const std::optional<double>& value) noexcept
{
    return value && std::isfinite(*value) && *value >= 0.0 && *value <= 100.0;
}

bool IsValidNonnegativeMetric(const std::optional<double>& value) noexcept
{
    return value && std::isfinite(*value) && *value >= 0.0 &&
        *value < static_cast<double>(std::numeric_limits<long>::max());
}

std::wstring A8CpuValue(const HudTelemetrySnapshot& snapshot)
{
    const auto usage = IsValidUsage(snapshot.cpuUsagePercent)
        ? Integer(*snapshot.cpuUsagePercent) + L"%" : L"--%";
    const auto clock = IsValidNonnegativeMetric(snapshot.cpuClockMHz)
        ? Integer(*snapshot.cpuClockMHz) + L"MHz" : L"--MHz";
    const auto temperature = snapshot.cpuTemperatureC && *snapshot.cpuTemperatureC >= 0
        ? std::to_wstring(*snapshot.cpuTemperatureC) + L"\u00B0C" : L"--\u00B0C";
    return usage + L" " + clock + L" " + temperature;
}

std::wstring A8GpuValue(const HudTelemetrySnapshot& snapshot)
{
    const auto usage = IsValidUsage(snapshot.gpuUsagePercent)
        ? Integer(*snapshot.gpuUsagePercent) + L"%" : L"--%";
    const auto clock = IsValidNonnegativeMetric(snapshot.gpuClockMHz)
        ? Integer(*snapshot.gpuClockMHz) + L"MHz" : L"--MHz";
    const auto power = IsValidNonnegativeMetric(snapshot.gpuPowerW)
        ? Integer(*snapshot.gpuPowerW) + L"W" : L"--W";
    return usage + L" " + clock + L" " + power;
}

std::wstring A8BatteryValue(const HudTelemetrySnapshot& snapshot)
{
    std::wstring value = snapshot.batteryPercent &&
        *snapshot.batteryPercent >= 0 && *snapshot.batteryPercent <= 100
        ? std::to_wstring(*snapshot.batteryPercent) + L"%" : L"--%";
    if (snapshot.onBattery)
    {
        if (snapshot.remainingMinutes && *snapshot.remainingMinutes >= 0)
        {
            const int minutes = *snapshot.remainingMinutes;
            if (minutes >= 60)
                value += L" " + Number(minutes / 60.0) + L"h";
            else
                value += L" " + std::to_wstring(minutes) + L"m";
        }
        else
            value += L" --";
    }
    return value;
}
}

bool ShouldShowHud(HudVisibilityMode mode, bool foregroundGameActive) noexcept
{
    return mode == HudVisibilityMode::Always || foregroundGameActive;
}

bool ResolveHudVisible(bool hudEnabled, std::optional<bool> manualOverride,
    HudVisibilityMode mode, bool foregroundActive) noexcept
{
    return hudEnabled &&
        (manualOverride ? *manualOverride : ShouldShowHud(mode, foregroundActive));
}

std::optional<bool> ResolveHudHotkeyOverride(bool hudEnabled,
    bool currentlyVisible) noexcept
{
    if (!hudEnabled)
        return std::nullopt;
    return !currentlyVisible;
}

bool ShouldSampleProductionTelemetry(bool resolvedShow, bool suspended) noexcept
{
    return resolvedShow && !suspended;
}

float HudOpacityFractionFromPercent(long percent) noexcept
{
    return static_cast<float>(std::clamp(percent, 0L, 100L)) / 100.0f;
}

long HudOpacityPercentFromFraction(float opacity) noexcept
{
    return static_cast<long>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 100.0f));
}

long ClampHudOpacityPercent(long percent) noexcept
{
    return std::clamp(percent, kHudOpacityMinimumPercent, kHudOpacityMaximumPercent);
}

long HudOpacityPercentFromText(std::wstring_view text) noexcept
{
    if (text.empty())
        return kDefaultHudOpacityPercent;
    std::size_t index = 0;
    bool negative = false;
    if (text.front() == L'+' || text.front() == L'-')
    {
        negative = text.front() == L'-';
        index = 1;
    }
    if (index == text.size())
        return kDefaultHudOpacityPercent;
    long value = 0;
    for (; index < text.size(); ++index)
    {
        if (text[index] < L'0' || text[index] > L'9')
            return kDefaultHudOpacityPercent;
        value = value * 10 + (text[index] - L'0');
        if (value > kHudOpacityMaximumPercent)
            value = kHudOpacityMaximumPercent;
    }
    return ClampHudOpacityPercent(negative ? -value : value);
}

long HudOpacityPercentFromSettings(std::wstring_view hudOpacity,
    bool hasHudOpacity, std::wstring_view legacyOpacity) noexcept
{
    return HudOpacityPercentFromText(hasHudOpacity ? hudOpacity : legacyOpacity);
}

std::uint8_t HudOpacityByte(float opacityPercent) noexcept
{
    return static_cast<std::uint8_t>(std::lround(
        std::clamp(opacityPercent, 0.0f, 100.0f) * 255.0f / 100.0f));
}

std::vector<HudTextRun> FormatHud(const HudTelemetrySnapshot& snapshot)
{
    std::vector<HudTextRun> runs;

    std::optional<double> fps;
    if (snapshot.presentMonDisplayedFps)
        fps = snapshot.presentMonDisplayedFps;
    else if (snapshot.renderFps)
        fps = snapshot.renderFps;

    const bool usableA8Fps = fps && std::isfinite(*fps) && std::round(*fps) > 0.0 &&
        *fps < static_cast<double>(std::numeric_limits<long>::max());
    if ((!snapshot.showUnavailableMetrics && fps) ||
        (snapshot.showUnavailableMetrics && usableA8Fps))
        Add(runs, HudSegmentKind::Graphics,
            L"",
            Integer(*fps) + L"FPS");
    else if (snapshot.showUnavailableMetrics && snapshot.fpsSamplingTargetActive)
        Add(runs, HudSegmentKind::Graphics, L"", L"--FPS");

    const auto cpu = snapshot.showUnavailableMetrics
        ? A8CpuValue(snapshot) : CpuValue(snapshot);
    if (snapshot.showUnavailableMetrics || !cpu.empty())
        Add(runs, HudSegmentKind::Cpu, L"CPU", cpu);

    const auto gpu = snapshot.showUnavailableMetrics
        ? A8GpuValue(snapshot) : GpuValue(snapshot);
    if (snapshot.showUnavailableMetrics || !gpu.empty())
        Add(runs, HudSegmentKind::Gpu, L"GPU", gpu);

    if (snapshot.showUnavailableMetrics)
        Add(runs, HudSegmentKind::Tdp, L"TDP",
            snapshot.apuPowerW && *snapshot.apuPowerW >= 0
                ? std::to_wstring(*snapshot.apuPowerW) + L"W" : L"--W");
    else if (snapshot.cpuPackagePowerW)
        Add(runs, HudSegmentKind::Tdp, L"TDP", Integer(*snapshot.cpuPackagePowerW) + L"W");

    if (snapshot.showUnavailableMetrics || snapshot.systemMemoryUsedBytes)
        Add(runs, HudSegmentKind::Ram, L"RAM", snapshot.systemMemoryUsedBytes
            ? Gigabytes(*snapshot.systemMemoryUsedBytes) + L"GB" : L"--GB");

    if (snapshot.showUnavailableMetrics || snapshot.gpuMemoryUsedBytes)
        Add(runs, HudSegmentKind::Vram, L"VRAM", snapshot.gpuMemoryUsedBytes
            ? Gigabytes(*snapshot.gpuMemoryUsedBytes) + L"GB" : L"--GB");

    const auto fan1 = snapshot.fan1Rpm && *snapshot.fan1Rpm >= 0 ? snapshot.fan1Rpm : std::nullopt;
    const auto fan2 = snapshot.fan2Rpm && *snapshot.fan2Rpm >= 0 ? snapshot.fan2Rpm : std::nullopt;
    if (snapshot.showUnavailableMetrics || fan1 || fan2)
    {
        if (fan1 || fan2)
        {
            const double average = fan1 && fan2
                ? (static_cast<double>(*fan1) + *fan2) / 2.0
                : static_cast<double>(fan1 ? *fan1 : *fan2);
            Add(runs, HudSegmentKind::Fan, L"FAN", Integer(average) + L"RPM");
        }
        else
            Add(runs, HudSegmentKind::Fan, L"FAN", L"--RPM");
    }

    if (snapshot.showUnavailableMetrics)
        Add(runs, HudSegmentKind::Battery, L"BAT", A8BatteryValue(snapshot));
    else if (snapshot.batteryPercent)
    {
        std::wstring value = std::to_wstring(*snapshot.batteryPercent) + L"%";
        if (snapshot.onBattery && snapshot.remainingMinutes)
        {
            const int minutes = *snapshot.remainingMinutes;
            if (minutes >= 60)
                value += L" " + Number(minutes / 60.0) + L"h";
            else if (minutes >= 0)
                value += L" " + std::to_wstring(minutes) + L"m";
        }
        Add(runs, HudSegmentKind::Battery, L"BAT", std::move(value));
    }

    return runs;
}

std::wstring JoinHudRuns(const std::vector<HudTextRun>& runs)
{
    std::wstring result;
    for (const auto& run : runs)
    {
        if (!result.empty())
            result += L" | ";
        result += run.label.empty() ? run.value : run.label + L" " + run.value;
    }
    return result;
}

HudTelemetrySnapshot MakeGameDcSample()
{
    HudTelemetrySnapshot sample{};
    sample.renderFps = 60.0;
    sample.cpuUsagePercent = 36.0;
    sample.cpuTemperatureC = 67;
    sample.cpuPackagePowerW = 18.0;
    sample.gpuUsagePercent = 98.0;
    sample.fan1Rpm = 3520;
    sample.fan2Rpm = 3560;
    sample.batteryPercent = 72;
    sample.remainingMinutes = 150;
    sample.onBattery = true;
    sample.foregroundGameActive = true;
    return sample;
}

HudTelemetrySnapshot MakeGameAcSample()
{
    auto sample = MakeGameDcSample();
    sample.onBattery = false;
    return sample;
}

HudTelemetrySnapshot MakeNoGameAlwaysSample()
{
    auto sample = MakeGameDcSample();
    sample.renderFps.reset();
    sample.foregroundGameActive = false;
    return sample;
}

HudTelemetrySnapshot MakeNoGameInGameOnlySample()
{
    auto sample = MakeNoGameAlwaysSample();
    return sample;
}
}
