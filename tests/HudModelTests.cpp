#include "HudModel.h"

#include <algorithm>
#include <iostream>

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
    ok &= Check(defaults.visibilityMode == HudVisibilityMode::InGameOnly, "default visibility");
    ok &= Check(defaults.alignment == HudAlignment::Center, "default alignment");
    ok &= Check(defaults.backgroundMode == HudBackgroundMode::FullWidth, "default background");
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
    ok &= Check(JoinHudRuns(dc) == L"60FPS | CPU 36% 67\u00B0C | GPU 98% | TDP 18W | FAN 3540RPM | BAT 72% 2.5h", "game DC formatting");
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
        L"60FPS | CPU 36% 67\u00B0C | GPU 98% | TDP 18W | FAN 3540RPM | BAT 72%", "game AC formatting");
    ok &= Check(JoinHudRuns(FormatHud(MakeNoGameAlwaysSample())) ==
        L"CPU 36% 67\u00B0C | GPU 98% | TDP 18W | FAN 3540RPM | BAT 72% 2.5h", "no-game formatting");
    ok &= Check(ShouldShowHud(HudVisibilityMode::Always, false), "always visibility");
    ok &= Check(!ShouldShowHud(HudVisibilityMode::InGameOnly, false), "in-game-only visibility");
    ok &= Check(ShouldShowHud(HudVisibilityMode::InGameOnly, true), "foreground game visibility");
    ok &= Check(ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::Always, false), false, false),
        "always mode keeps global telemetry alive after game exit");
    ok &= Check(!ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::InGameOnly, false), false, false),
        "in-game-only mode stops global telemetry with no game");
    ok &= Check(ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::InGameOnly, true), false, false),
        "in-game-only mode starts global telemetry on game entry");
    ok &= Check(!ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::InGameOnly, false), false, false),
        "in-game-only mode stops global telemetry on Alt-Tab");
    ok &= Check(ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::InGameOnly, true), false, false),
        "in-game-only mode resumes global telemetry on return");
    ok &= Check(!ShouldSampleProductionTelemetry(
        ShouldShowHud(HudVisibilityMode::InGameOnly, false), false, false),
        "in-game-only mode stops global telemetry after game exit");
    ok &= Check(!ShouldSampleProductionTelemetry(true, true, false),
        "diagnostic mode owns telemetry lifecycle");
    ok &= Check(!ShouldSampleProductionTelemetry(true, false, true),
        "suspend pauses telemetry lifecycle");

    HudTelemetrySnapshot missing{};
    missing.graphicsApi = L"DX12";
    missing.cpuUsagePercent = 0.0;
    missing.cpuTemperatureC = 0;
    missing.fan1Rpm = 3200;
    ok &= Check(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0\u00B0C | FAN 3200RPM", "zero values stay explicit");
    missing.fan1Rpm.reset();
    missing.fan2Rpm = 3600;
    ok &= Check(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0\u00B0C | FAN 3600RPM", "single fan formatting");
    missing.fan2Rpm.reset();
    ok &= Check(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0\u00B0C", "missing fans omitted");

    HudTelemetrySnapshot temperaturesOnly{};
    temperaturesOnly.cpuTemperatureC = 48;
    temperaturesOnly.gpuTemperatureC = 44;
    ok &= Check(JoinHudRuns(FormatHud(temperaturesOnly)) == L"CPU 48\u00B0C", "GPU temperature omitted");

    HudTelemetrySnapshot usage{};
    usage.cpuUsagePercent = 33.0;
    usage.cpuTemperatureC = 33;
    usage.gpuUsagePercent = 44.0;
    usage.gpuTemperatureC = 67;
    ok &= Check(JoinHudRuns(FormatHud(usage)) == L"CPU 33% 33\u00B0C | GPU 44%",
        "CPU usage and GPU usage formatting");
    usage.gpuClockMHz = 2300.0;
    ok &= Check(JoinHudRuns(FormatHud(usage)) == L"CPU 33% 33\u00B0C | GPU 44% 2300MHz",
        "GPU usage and clock formatting");
    usage.gpuUsagePercent.reset();
    ok &= Check(JoinHudRuns(FormatHud(usage)) == L"CPU 33% 33\u00B0C | GPU 2300MHz",
        "GPU clock-only formatting");
    usage.gpuClockMHz.reset();
    ok &= Check(JoinHudRuns(FormatHud(usage)) == L"CPU 33% 33\u00B0C",
        "GPU metrics omitted when unavailable");
    ok &= Check(FormatHud(HudTelemetrySnapshot{}).empty(), "empty snapshot omitted");

    HudTelemetrySnapshot displayed{};
    displayed.presentMonDisplayedFps = 120.0;
    auto displayedRuns = FormatHud(displayed);
    ok &= Check(displayedRuns.size() == 1 && displayedRuns[0].label.empty() &&
        displayedRuns[0].value == L"120FPS",
        "PresentMon FPS without API uses unit-formatted value");
    displayed.graphicsApi = L"DX12";
    displayedRuns = FormatHud(displayed);
    ok &= Check(displayedRuns.size() == 1 && displayedRuns[0].label.empty() &&
        displayedRuns[0].value == L"120FPS",
        "DX12 is hidden from displayed FPS formatting");
    displayed.graphicsApi = L"DX11";
    displayedRuns = FormatHud(displayed);
    ok &= Check(displayedRuns.size() == 1 && displayedRuns[0].label.empty() &&
        displayedRuns[0].value == L"120FPS",
        "DX11 API is hidden from FPS formatting");
    displayed.graphicsApi = L"Vulkan";
    displayedRuns = FormatHud(displayed);
    ok &= Check(displayedRuns.size() == 1 && displayedRuns[0].label.empty() &&
        displayedRuns[0].value == L"120FPS",
        "non-DX12 API is hidden from FPS formatting");
    displayed.graphicsApi = L"DX11+DX12";
    displayedRuns = FormatHud(displayed);
    ok &= Check(displayedRuns.size() == 1 && displayedRuns[0].label.empty() &&
        displayedRuns[0].value == L"120FPS",
        "mixed API value is hidden from FPS formatting");

    HudTelemetrySnapshot rendered{};
    rendered.graphicsApi = L"DX12";
    rendered.renderFps = 120.0;
    auto renderedRuns = FormatHud(rendered);
    ok &= Check(renderedRuns.size() == 1 && renderedRuns[0].label.empty() &&
        renderedRuns[0].value == L"120FPS",
        "render FPS hides the graphics API label");
    rendered.graphicsApi = L"DX11";
    renderedRuns = FormatHud(rendered);
    ok &= Check(renderedRuns.size() == 1 && renderedRuns[0].label.empty() &&
        renderedRuns[0].value == L"120FPS",
        "render FPS hides a non-DX12 API");
    HudTelemetrySnapshot renderedWithoutApi{};
    renderedWithoutApi.renderFps = 120.0;
    renderedRuns = FormatHud(renderedWithoutApi);
    ok &= Check(renderedRuns.size() == 1 && renderedRuns[0].label.empty() &&
        renderedRuns[0].value == L"120FPS",
        "render FPS without API remains visible");

    HudTelemetrySnapshot unavailableApi{};
    unavailableApi.presentMonDisplayedFps = 120.0;
    unavailableApi.cpuUsagePercent = 33.0;
    unavailableApi.gpuUsagePercent = 44.0;
    ok &= Check(JoinHudRuns(FormatHud(unavailableApi)) ==
        L"120FPS | CPU 33% | GPU 44%",
        "missing graphics API preserves other telemetry");

    HudTelemetrySnapshot vram{};
    vram.gpuUsagePercent = 0.0;
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"GPU 0%",
        "zero GPU usage without VRAM formatting");
    vram.gpuUsagePercent = 87.0;
    vram.gpuMemoryUsedBytes = static_cast<std::uint64_t>(3.4 * 1024.0 * 1024.0 * 1024.0);
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"GPU 87% | VRAM 3.4GB",
        "GPU usage and VRAM formatting");
    vram.gpuUsagePercent.reset();
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"VRAM 3.4GB",
        "VRAM-only formatting");
    vram.gpuMemoryUsedBytes.reset();
    vram.gpuUsagePercent = 87.0;
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"GPU 87%",
        "GPU without VRAM omits the slot");
    vram.gpuUsagePercent.reset();
    vram.gpuMemoryUsedBytes = static_cast<std::uint64_t>(3.4 * 1024.0 * 1024.0 * 1024.0);
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"VRAM 3.4GB",
        "VRAM without GPU usage remains visible");

    HudTelemetrySnapshot all{};
    all.graphicsApi = L"DX12";
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
        L"999FPS | CPU 21% 42\u00B0C | GPU 24% | TDP 7W | RAM 15.2GB | VRAM 3.4GB | FAN 4050RPM | BAT 80%",
        "RAM formatting and final HUD order");
    ok &= Check(allRuns.size() == 8 && allRuns[4].kind == HudSegmentKind::Ram &&
        allRuns[5].kind == HudSegmentKind::Vram, "RAM precedes VRAM");
    all.systemPowerW = 24.0;
    all.onBattery = true;
    ok &= Check(JoinHudRuns(FormatHud(all)).find(L"SYS") == std::wstring::npos,
        "system power is omitted from HUD");
    all.systemMemoryUsedBytes.reset();
    const auto withoutRam = FormatHud(all);
    ok &= Check(std::all_of(withoutRam.begin(), withoutRam.end(),
            [](const auto& run) { return run.kind != HudSegmentKind::Ram; }),
        "unavailable RAM omits HUD group");

    return ok ? 0 : 1;
}
