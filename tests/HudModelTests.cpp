#include "HudModel.h"

#include <algorithm>
#include <iostream>
#include <limits>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool ok = true;
    const HudLayoutOptions defaults{};
    ok &= Check(defaults.visibilityMode == HudVisibilityMode::Always, "default visibility");
    ok &= Check(defaults.alignment == HudAlignment::Center, "default alignment");
    ok &= Check(defaults.backgroundMode == HudBackgroundMode::ContentWidth, "default background");
    ok &= Check(defaults.backgroundOpacity == 0.7f, "default opacity");
    ok &= Check(ClampHudOpacityPercent(49) == 50 &&
        ClampHudOpacityPercent(70) == 70 && ClampHudOpacityPercent(101) == 100,
        "HUD opacity range");
    ok &= Check(HudOpacityPercentFromText(L"") == 70 &&
        HudOpacityPercentFromText(L"invalid") == 70 &&
        HudOpacityPercentFromText(L"49") == 50 &&
        HudOpacityPercentFromText(L"101") == 100 &&
        HudOpacityPercentFromText(L"70") == 70,
        "HUD opacity settings parsing");
    ok &= Check(HudOpacityPercentFromSettings(L"70", true, L"50") == 70 &&
        HudOpacityPercentFromSettings(L"", false, L"75") == 75 &&
        HudOpacityPercentFromSettings(L"invalid", true, L"80") == 70,
        "HUD opacity legacy setting precedence");
    ok &= Check(HudOpacityByte(50.0f) == 128 && HudOpacityByte(65.0f) == 166 &&
        HudOpacityByte(70.0f) == 179 && HudOpacityByte(75.0f) == 191 &&
        HudOpacityByte(100.0f) == 255, "HUD opacity byte conversion");
    ok &= Check(HudOpacityFractionFromPercent(0) == 0.0f &&
        HudOpacityFractionFromPercent(37) == 0.37f &&
        HudOpacityFractionFromPercent(50) == 0.5f &&
        HudOpacityFractionFromPercent(100) == 1.0f,
        "settings opacity percent to runtime");
    ok &= Check(HudOpacityPercentFromFraction(HudOpacityFractionFromPercent(37)) == 37,
        "settings opacity runtime to percent round trip");

    const auto dc = FormatHud(MakeGameDcSample());
    ok &= Check(JoinHudRuns(dc) == L"60FPS | CPU 36% 67\u00B0C | GPU 98% --W | TDP 18W | FAN 3540RPM | BAT 72% 2.5h", "game DC formatting");
    auto fractionalTdp = MakeGameDcSample();
    fractionalTdp.cpuPackagePowerW = 35.3;
    const auto fractionalTdpText = JoinHudRuns(FormatHud(fractionalTdp));
    ok &= Check(fractionalTdpText.find(L"TDP 35W") != std::wstring::npos &&
        fractionalTdpText.find(L"35.3W") == std::wstring::npos,
        "TDP rounds to integer watts");
    fractionalTdp.cpuPackagePowerW = 18.7;
    ok &= Check(JoinHudRuns(FormatHud(fractionalTdp)).find(L"TDP 19W") != std::wstring::npos,
        "TDP uses nearest integer rounding");
    ok &= Check(dc.size() == 6, "game DC segment count");

    ok &= Check(JoinHudRuns(FormatHud(MakeGameAcSample())) ==
        L"60FPS | CPU 36% 67\u00B0C | GPU 98% --W | TDP 18W | FAN 3540RPM | BAT 72%", "game AC formatting");
    ok &= Check(JoinHudRuns(FormatHud(MakeNoGameAlwaysSample())) ==
        L"CPU 36% 67\u00B0C | GPU 98% --W | TDP 18W | FAN 3540RPM | BAT 72% 2.5h", "no-game formatting");
    HudTelemetrySnapshot globalTelemetry{};
    globalTelemetry.cpuUsagePercent = 13.0;
    globalTelemetry.gpuUsagePercent = 15.0;
    globalTelemetry.systemMemoryUsedBytes =
        static_cast<std::uint64_t>(8.0 * 1024.0 * 1024.0 * 1024.0);
    globalTelemetry.gpuMemoryUsedBytes =
        static_cast<std::uint64_t>(2.4 * 1024.0 * 1024.0 * 1024.0);
    const auto beforeFps = JoinHudRuns(FormatHud(globalTelemetry));
    globalTelemetry.presentMonDisplayedFps = 60.0;
    const auto afterFps = JoinHudRuns(FormatHud(globalTelemetry));
    ok &= Check(beforeFps == L"CPU 13% | GPU 15% --W | RAM 8.0GB | VRAM 2.4GB" &&
        afterFps == L"60FPS | CPU 13% | GPU 15% --W | RAM 8.0GB | VRAM 2.4GB",
        "global telemetry remains visible before and after FPS");
    globalTelemetry.presentMonDisplayedFps.reset();
    const auto afterGameExit = JoinHudRuns(FormatHud(globalTelemetry));
    ok &= Check(afterGameExit == beforeFps,
        "game-scoped FPS removal preserves global telemetry");
    ok &= Check(ShouldShowHud(HudVisibilityMode::Always, false), "always visibility");
    ok &= Check(!ShouldShowHud(HudVisibilityMode::InGameOnly, false), "in-game-only visibility");
    ok &= Check(ShouldShowHud(HudVisibilityMode::InGameOnly, true), "foreground game visibility");

    ok &= Check(!ResolveHudVisible(false, std::nullopt, HudVisibilityMode::Always, true),
        "HUD off resolves hidden regardless of mode or foreground");
    ok &= Check(!ResolveHudVisible(false, std::optional<bool>(true),
        HudVisibilityMode::Always, true),
        "HUD off resolves hidden even with a show override");
    ok &= Check(ResolveHudVisible(true, std::nullopt, HudVisibilityMode::Always, false),
        "Always mode shows with no foreground game");
    ok &= Check(!ResolveHudVisible(true, std::nullopt, HudVisibilityMode::InGameOnly, false),
        "InGameOnly hides with no foreground game");
    ok &= Check(ResolveHudVisible(true, std::nullopt, HudVisibilityMode::InGameOnly, true),
        "InGameOnly shows with a foreground game");
    ok &= Check(!ResolveHudVisible(true, std::optional<bool>(false),
        HudVisibilityMode::Always, true),
        "a hide override beats Always mode and foreground");
    ok &= Check(ResolveHudVisible(true, std::optional<bool>(true),
        HudVisibilityMode::InGameOnly, false),
        "a show override beats InGameOnly with no game");
    ok &= Check(!ResolveHudHotkeyOverride(false, false).has_value(),
        "F8 ignored when HUD master disabled (hidden)");
    ok &= Check(!ResolveHudHotkeyOverride(false, true).has_value(),
        "F8 ignored when HUD master disabled (visible)");
    ok &= Check(ResolveHudHotkeyOverride(true, true) == std::optional<bool>(false),
        "F8 while enabled+visible resolves to force hide");
    ok &= Check(ResolveHudHotkeyOverride(true, false) == std::optional<bool>(true),
        "F8 while enabled+hidden resolves to force show");

    ok &= Check(ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::Always, false), false),
        "always mode keeps global telemetry alive after game exit");
    ok &= Check(!ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::InGameOnly, false), false),
        "in-game-only mode stops global telemetry with no game");
    ok &= Check(ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::InGameOnly, true), false),
        "in-game-only mode starts global telemetry on game entry");
    ok &= Check(!ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::InGameOnly, false), false),
        "in-game-only mode stops global telemetry on Alt-Tab");
    ok &= Check(ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::InGameOnly, true), false),
        "in-game-only mode resumes global telemetry on return");
    ok &= Check(!ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::InGameOnly, false), false),
        "in-game-only mode stops global telemetry after game exit");
    ok &= Check(!ShouldSampleProductionTelemetry(true, true),
        "suspend pauses telemetry lifecycle");

    HudTelemetrySnapshot missing{};
    missing.cpuUsagePercent = 0.0;
    missing.cpuTemperatureC = 0;
    missing.fan1Rpm = 3200;
    ok &= Check(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0\u00B0C | GPU --W | FAN 3200RPM", "zero values stay explicit");
    missing.fan1Rpm.reset();
    missing.fan2Rpm = 3600;
    ok &= Check(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0\u00B0C | GPU --W | FAN 3600RPM", "single fan formatting");
    missing.fan2Rpm.reset();
    ok &= Check(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0\u00B0C | GPU --W", "missing fans omitted");

    HudTelemetrySnapshot temperaturesOnly{};
    temperaturesOnly.cpuTemperatureC = 48;
    ok &= Check(JoinHudRuns(FormatHud(temperaturesOnly)) == L"CPU 48\u00B0C | GPU --W", "CPU temperature formatting");

    HudTelemetrySnapshot cpuFrequency{};
    cpuFrequency.cpuUsagePercent = 42.0;
    cpuFrequency.cpuClockMHz = 4200.4;
    cpuFrequency.cpuTemperatureC = 67;
    ok &= Check(JoinHudRuns(FormatHud(cpuFrequency)) == L"CPU 42% 4200MHz 67\u00B0C | GPU --W",
        "CPU usage, frequency and temperature formatting order");
    cpuFrequency.cpuClockMHz.reset();
    ok &= Check(JoinHudRuns(FormatHud(cpuFrequency)) == L"CPU 42% 67\u00B0C | GPU --W",
        "missing CPU frequency preserves usage and temperature");
    cpuFrequency.cpuUsagePercent.reset();
    cpuFrequency.cpuClockMHz = 4200.0;
    ok &= Check(JoinHudRuns(FormatHud(cpuFrequency)) == L"CPU 4200MHz 67\u00B0C | GPU --W",
        "CPU frequency formats when usage is missing");
    cpuFrequency.cpuTemperatureC.reset();
    ok &= Check(JoinHudRuns(FormatHud(cpuFrequency)) == L"CPU 4200MHz | GPU --W",
        "CPU frequency-only formatting");
    for (const double invalidClock : {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -1.0,
        std::numeric_limits<double>::max()})
    {
        cpuFrequency.cpuClockMHz = invalidClock;
        ok &= Check(JoinHudRuns(FormatHud(cpuFrequency)) == L"GPU --W",
            "invalid or unrenderable CPU frequency is omitted");
    }

    HudTelemetrySnapshot usage{};
    usage.cpuUsagePercent = 33.0;
    usage.cpuTemperatureC = 33;
    usage.gpuUsagePercent = 44.0;
    ok &= Check(JoinHudRuns(FormatHud(usage)) == L"CPU 33% 33\u00B0C | GPU 44% --W",
        "CPU usage and GPU usage formatting");
    usage.gpuClockMHz = 2300.0;
    ok &= Check(JoinHudRuns(FormatHud(usage)) == L"CPU 33% 33\u00B0C | GPU 44% 2300MHz --W",
        "GPU usage and clock formatting");
    usage.gpuPowerW = 12.4;
    ok &= Check(JoinHudRuns(FormatHud(usage)) == L"CPU 33% 33\u00B0C | GPU 44% 2300MHz 12W",
        "GPU power rounds 12.4 W to 12 W after usage and clock");
    usage.gpuPowerW = 12.6;
    ok &= Check(JoinHudRuns(FormatHud(usage)) == L"CPU 33% 33\u00B0C | GPU 44% 2300MHz 13W",
        "GPU power rounds 12.6 W to 13 W");
    usage.gpuUsagePercent.reset();
    ok &= Check(JoinHudRuns(FormatHud(usage)) == L"CPU 33% 33\u00B0C | GPU 2300MHz 13W",
        "GPU clock-only formatting");
    usage.gpuClockMHz.reset();
    usage.gpuPowerW.reset();
    ok &= Check(JoinHudRuns(FormatHud(usage)) == L"CPU 33% 33\u00B0C | GPU --W",
        "GPU power cell remains visible when power and other GPU metrics are unavailable");
    ok &= Check(JoinHudRuns(FormatHud(HudTelemetrySnapshot{})) == L"GPU --W",
        "empty telemetry snapshot still exposes the GPU power cell");

    // FPS presentation: a value shows an FPS segment, no value shows none.
    // There is no synthetic "0FPS" fallback.
    HudTelemetrySnapshot fpsAvailable{};
    fpsAvailable.presentMonDisplayedFps = 98.7;
    auto fpsAvailableRuns = FormatHud(fpsAvailable);
    ok &= Check(fpsAvailableRuns.size() == 2 &&
        fpsAvailableRuns[0].kind == HudSegmentKind::Graphics &&
        fpsAvailableRuns[0].value == L"99FPS" &&
        fpsAvailableRuns[1].kind == HudSegmentKind::Gpu &&
        fpsAvailableRuns[1].value == L"--W",
        "available FPS renders before the always-present GPU power cell");

    HudTelemetrySnapshot fpsUnavailable{};
    fpsUnavailable.presentMonDisplayedFps = std::nullopt;
    fpsUnavailable.cpuUsagePercent = 20.0;
    const auto fpsUnavailableText = JoinHudRuns(FormatHud(fpsUnavailable));
    ok &= Check(fpsUnavailableText == L"CPU 20% | GPU --W" &&
        fpsUnavailableText.find(L"FPS") == std::wstring::npos,
        "unavailable FPS omits the segment entirely (no 0FPS fallback)");

    HudTelemetrySnapshot displayed{};
    displayed.presentMonDisplayedFps = 120.0;
    auto displayedRuns = FormatHud(displayed);
    ok &= Check(displayedRuns.size() == 2 && displayedRuns[0].label.empty() &&
        displayedRuns[0].value == L"120FPS" && displayedRuns[1].value == L"--W",
        "PresentMon FPS uses unit-formatted value before GPU power");

    HudTelemetrySnapshot rendered{};
    rendered.renderFps = 120.0;
    auto renderedRuns = FormatHud(rendered);
    ok &= Check(renderedRuns.size() == 2 && renderedRuns[0].label.empty() &&
        renderedRuns[0].value == L"120FPS" && renderedRuns[1].value == L"--W",
        "render FPS renders before GPU power");

    HudTelemetrySnapshot unavailableApi{};
    unavailableApi.presentMonDisplayedFps = 120.0;
    unavailableApi.cpuUsagePercent = 33.0;
    unavailableApi.gpuUsagePercent = 44.0;
    ok &= Check(JoinHudRuns(FormatHud(unavailableApi)) ==
        L"120FPS | CPU 33% | GPU 44% --W",
        "missing graphics API preserves other telemetry");

    HudTelemetrySnapshot vram{};
    vram.gpuUsagePercent = 0.0;
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"GPU 0% --W",
        "zero GPU usage without VRAM formatting");
    vram.gpuUsagePercent = 87.0;
    vram.gpuMemoryUsedBytes = static_cast<std::uint64_t>(3.4 * 1024.0 * 1024.0 * 1024.0);
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"GPU 87% --W | VRAM 3.4GB",
        "GPU usage and VRAM formatting");
    vram.gpuUsagePercent.reset();
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"GPU --W | VRAM 3.4GB",
        "VRAM-only formatting");
    vram.gpuMemoryUsedBytes.reset();
    vram.gpuUsagePercent = 87.0;
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"GPU 87% --W",
        "GPU without VRAM omits the slot");
    vram.gpuUsagePercent.reset();
    vram.gpuMemoryUsedBytes = static_cast<std::uint64_t>(3.4 * 1024.0 * 1024.0 * 1024.0);
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"GPU --W | VRAM 3.4GB",
        "VRAM without GPU usage remains visible");

    HudTelemetrySnapshot all{};
    all.presentMonDisplayedFps = 999.0;
    all.cpuUsagePercent = 21.0;
    all.cpuTemperatureC = 42;
    all.gpuUsagePercent = 24.0;
    all.cpuPackagePowerW = 7.0;
    all.systemMemoryUsedBytes = static_cast<std::uint64_t>(15.2 * 1024.0 * 1024.0 * 1024.0);
    all.gpuMemoryUsedBytes = static_cast<std::uint64_t>(3.4 * 1024.0 * 1024.0 * 1024.0);
    all.fan1Rpm = 4050;
    all.batteryPercent = 80;
    const auto allRuns = FormatHud(all);
    ok &= Check(JoinHudRuns(allRuns) ==
        L"999FPS | CPU 21% 42\u00B0C | GPU 24% --W | TDP 7W | RAM 15.2GB | VRAM 3.4GB | FAN 4050RPM | BAT 80%",
        "RAM formatting and final HUD order");
    ok &= Check(allRuns.size() == 8 && allRuns[4].kind == HudSegmentKind::Ram &&
        allRuns[5].kind == HudSegmentKind::Vram, "RAM precedes VRAM");
    all.systemMemoryUsedBytes.reset();
    const auto withoutRam = FormatHud(all);
    ok &= Check(std::all_of(withoutRam.begin(), withoutRam.end(),
            [](const auto& run) { return run.kind != HudSegmentKind::Ram; }),
        "unavailable RAM omits HUD group");

    HudTelemetrySnapshot a8AllSystemMetrics{};
    a8AllSystemMetrics.showUnavailableMetrics = true;
    a8AllSystemMetrics.fpsSamplingTargetActive = true;
    a8AllSystemMetrics.presentMonDisplayedFps = 119.6;
    a8AllSystemMetrics.cpuUsagePercent = 42.0;
    a8AllSystemMetrics.cpuClockMHz = 4200.4;
    a8AllSystemMetrics.gpuUsagePercent = 75.0;
    a8AllSystemMetrics.gpuClockMHz = 2200.0;
    a8AllSystemMetrics.gpuPowerW = 12.6;
    a8AllSystemMetrics.cpuPackagePowerW = 18.0;
    a8AllSystemMetrics.apuPowerW = 24;
    a8AllSystemMetrics.systemMemoryUsedBytes = 8ULL * 1024 * 1024 * 1024;
    a8AllSystemMetrics.gpuMemoryUsedBytes = 4ULL * 1024 * 1024 * 1024;
    a8AllSystemMetrics.onBattery = true;
    a8AllSystemMetrics.batteryPercent = 75;
    a8AllSystemMetrics.remainingMinutes = 90;
    ok &= Check(JoinHudRuns(FormatHud(a8AllSystemMetrics)) ==
        L"120FPS | CPU 42% 4200MHz --\u00B0C | GPU 75% 2200MHz 13W | TDP 24W | RAM 8.0GB | VRAM 4.0GB | FAN --RPM | BAT 75% 1.5h",
        "A8 TDP uses ADLX power independently of EC package and PresentMon GPU power");

    HudTelemetrySnapshot a8Unavailable{};
    a8Unavailable.showUnavailableMetrics = true;
    const auto a8UnavailableText = JoinHudRuns(FormatHud(a8Unavailable));
    ok &= Check(a8UnavailableText ==
        L"CPU --% --MHz --\u00B0C | GPU --% --MHz --W | TDP --W | RAM --GB | VRAM --GB | FAN --RPM | BAT --%",
        "A8 exposes placeholders when all telemetry is unavailable and no FPS target exists");
    ok &= Check(a8UnavailableText.find(L"0FPS") == std::wstring::npos,
        "A8 never fabricates zero FPS");

    HudTelemetrySnapshot a8Partial{};
    a8Partial.showUnavailableMetrics = true;
    a8Partial.cpuUsagePercent = 30.0;
    a8Partial.cpuTemperatureC = 61;
    a8Partial.gpuUsagePercent = 55.0;
    a8Partial.gpuClockMHz = 1900.0;
    a8Partial.systemMemoryUsedBytes = 0;
    ok &= Check(JoinHudRuns(FormatHud(a8Partial)) ==
        L"CPU 30% --MHz 61\u00B0C | GPU 55% 1900MHz --W | TDP --W | RAM 0.0GB | VRAM --GB | FAN --RPM | BAT --%",
        "A8 partial telemetry keeps per-metric placeholders and accepts zero-byte RAM");

    HudTelemetrySnapshot a8EcUnavailable{};
    a8EcUnavailable.showUnavailableMetrics = true;
    a8EcUnavailable.cpuUsagePercent = 50.0;
    a8EcUnavailable.cpuClockMHz = 3600.0;
    a8EcUnavailable.gpuUsagePercent = 60.0;
    a8EcUnavailable.gpuClockMHz = 2000.0;
    a8EcUnavailable.gpuPowerW = 10.0;
    a8EcUnavailable.cpuPackagePowerW = 18.0;
    ok &= Check(JoinHudRuns(FormatHud(a8EcUnavailable)).find(
            L"CPU 50% 3600MHz --\u00B0C | GPU 60% 2000MHz 10W | TDP --W") == 0 &&
        JoinHudRuns(FormatHud(a8EcUnavailable)).find(L"FAN --RPM") != std::wstring::npos,
        "A8 leaves EC temperature, package power, and fan readings unavailable");

    HudTelemetrySnapshot a8ZeroValues{};
    a8ZeroValues.showUnavailableMetrics = true;
    a8ZeroValues.cpuUsagePercent = 0.0;
    a8ZeroValues.cpuClockMHz = 0.0;
    a8ZeroValues.gpuUsagePercent = 0.0;
    a8ZeroValues.gpuClockMHz = 0.0;
    a8ZeroValues.gpuPowerW = 0.0;
    a8ZeroValues.cpuPackagePowerW = 18.0;
    a8ZeroValues.apuPowerW = 0;
    a8ZeroValues.systemMemoryUsedBytes = 0;
    a8ZeroValues.gpuMemoryUsedBytes = 0;
    a8ZeroValues.fan1Rpm = 0;
    a8ZeroValues.batteryPercent = 0;
    ok &= Check(JoinHudRuns(FormatHud(a8ZeroValues)) ==
        L"CPU 0% 0MHz --\u00B0C | GPU 0% 0MHz 0W | TDP 0W | RAM 0.0GB | VRAM 0.0GB | FAN 0RPM | BAT 0%",
        "A8 preserves legitimate zero utilization, power, memory, fan, and battery values");

    HudTelemetrySnapshot a8DcWithoutEta{};
    a8DcWithoutEta.showUnavailableMetrics = true;
    a8DcWithoutEta.onBattery = true;
    a8DcWithoutEta.batteryPercent = 55;
    ok &= Check(JoinHudRuns(FormatHud(a8DcWithoutEta)).find(L"BAT 55% --") != std::wstring::npos,
        "known DC appends an unavailable ETA placeholder");
    a8DcWithoutEta.remainingMinutes = 120;
    ok &= Check(JoinHudRuns(FormatHud(a8DcWithoutEta)).find(L"BAT 55% 2h") != std::wstring::npos,
        "known DC formats a valid ETA");
    a8DcWithoutEta.batteryPercent.reset();
    a8DcWithoutEta.remainingMinutes.reset();
    ok &= Check(JoinHudRuns(FormatHud(a8DcWithoutEta)).find(L"BAT --% --") != std::wstring::npos,
        "known DC with no battery level or ETA shows both unavailable values");
    HudTelemetrySnapshot a8AcOrUnknown{};
    a8AcOrUnknown.showUnavailableMetrics = true;
    a8AcOrUnknown.batteryPercent = 55;
    a8AcOrUnknown.remainingMinutes = 120;
    ok &= Check(JoinHudRuns(FormatHud(a8AcOrUnknown)).find(L"BAT 55%") != std::wstring::npos &&
        JoinHudRuns(FormatHud(a8AcOrUnknown)).find(L"BAT 55% 2h") == std::wstring::npos,
        "AC or unknown power state does not claim DC or display an ETA");

    HudTelemetrySnapshot a8FpsTarget{};
    a8FpsTarget.showUnavailableMetrics = true;
    a8FpsTarget.fpsSamplingTargetActive = true;
    const auto a8TargetMissingFps = JoinHudRuns(FormatHud(a8FpsTarget));
    ok &= Check(a8TargetMissingFps.starts_with(L"--FPS | ") &&
        a8TargetMissingFps.find(L"0FPS") == std::wstring::npos,
        "A8 shows --FPS only for an active sampling target with unavailable FPS");
    a8FpsTarget.presentMonDisplayedFps = 0.0;
    const auto a8ZeroFps = JoinHudRuns(FormatHud(a8FpsTarget));
    ok &= Check(a8ZeroFps.starts_with(L"--FPS | ") &&
        a8ZeroFps.find(L"0FPS") == std::wstring::npos,
        "A8 treats zero FPS as unavailable instead of fabricating a 0FPS reading");
    a8FpsTarget.presentMonDisplayedFps = 0.2;
    const auto a8RoundedZeroFps = JoinHudRuns(FormatHud(a8FpsTarget));
    ok &= Check(a8RoundedZeroFps.starts_with(L"--FPS | ") &&
        a8RoundedZeroFps.find(L"0FPS") == std::wstring::npos,
        "A8 treats a value that rounds to zero as unavailable FPS");
    a8FpsTarget.fpsSamplingTargetActive = false;
    a8FpsTarget.presentMonDisplayedFps.reset();
    const auto a8NoTargetFps = JoinHudRuns(FormatHud(a8FpsTarget));
    ok &= Check(a8NoTargetFps.find(L"FPS") == std::wstring::npos,
        "A8 does not show a permanent FPS placeholder without an active target");

    HudTelemetrySnapshot a8Invalid{};
    a8Invalid.showUnavailableMetrics = true;
    a8Invalid.cpuUsagePercent = std::numeric_limits<double>::quiet_NaN();
    a8Invalid.cpuClockMHz = std::numeric_limits<double>::infinity();
    a8Invalid.cpuTemperatureC = -1;
    a8Invalid.gpuUsagePercent = 101.0;
    a8Invalid.gpuClockMHz = std::numeric_limits<double>::infinity();
    a8Invalid.gpuPowerW = -1.0;
    ok &= Check(JoinHudRuns(FormatHud(a8Invalid)).find(
            L"CPU --% --MHz --\u00B0C | GPU --% --MHz --W") == 0,
        "A8 invalid values use unavailable placeholders instead of invalid formatting");

    return ok ? 0 : 1;
}
