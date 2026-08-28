#include "HudModel.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
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
    return value;
}
}

bool ShouldShowHud(HudVisibilityMode mode, bool foregroundGameActive) noexcept
{
    return mode == HudVisibilityMode::Always || foregroundGameActive;
}

bool ShouldSampleProductionTelemetry(bool resolvedShow, bool diagnosticMode,
    bool suspended) noexcept
{
    return resolvedShow && !diagnosticMode && !suspended;
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

    if (snapshot.graphicsApi && snapshot.presentMonDisplayedFps)
        Add(runs, HudSegmentKind::Graphics, *snapshot.graphicsApi,
            Integer(*snapshot.presentMonDisplayedFps) + L"FPS");
    else if (snapshot.presentMonDisplayedFps)
        Add(runs, HudSegmentKind::Graphics, L"FPS",
            Integer(*snapshot.presentMonDisplayedFps));
    else if (snapshot.graphicsApi && snapshot.renderFps)
        Add(runs, HudSegmentKind::Graphics, *snapshot.graphicsApi,
            Integer(*snapshot.renderFps) + L"FPS");

    const auto cpu = CpuValue(snapshot);
    if (!cpu.empty())
        Add(runs, HudSegmentKind::Cpu, L"CPU", cpu);

    const auto gpu = GpuValue(snapshot);
    if (!gpu.empty())
        Add(runs, HudSegmentKind::Gpu, L"GPU", gpu);

    if (snapshot.cpuPackagePowerW)
        Add(runs, HudSegmentKind::Tdp, L"TDP", Integer(*snapshot.cpuPackagePowerW) + L"W");

    if (snapshot.systemMemoryUsedBytes)
        Add(runs, HudSegmentKind::Ram, L"RAM", Gigabytes(*snapshot.systemMemoryUsedBytes) + L"GB");

    if (snapshot.gpuMemoryUsedBytes)
        Add(runs, HudSegmentKind::Vram, L"VRAM", Gigabytes(*snapshot.gpuMemoryUsedBytes) + L"GB");

    const auto fan1 = snapshot.fan1Rpm && *snapshot.fan1Rpm >= 0 ? snapshot.fan1Rpm : std::nullopt;
    const auto fan2 = snapshot.fan2Rpm && *snapshot.fan2Rpm >= 0 ? snapshot.fan2Rpm : std::nullopt;
    if (fan1 || fan2)
    {
        const double average = fan1 && fan2
            ? (static_cast<double>(*fan1) + *fan2) / 2.0
            : static_cast<double>(fan1 ? *fan1 : *fan2);
        Add(runs, HudSegmentKind::Fan, L"FAN", Integer(average) + L"RPM");
    }

    if (snapshot.batteryPercent)
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
        result += run.label + L" " + run.value;
    }
    return result;
}

HudTelemetrySnapshot MakeGameDcSample()
{
    return {L"DX11", 60.0, std::nullopt, std::nullopt, 36.0, 67, 18.0,
        98.0, std::nullopt, std::nullopt, 3520, 3560, 72, 24.0, 150, true, true};
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
    sample.graphicsApi.reset();
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
