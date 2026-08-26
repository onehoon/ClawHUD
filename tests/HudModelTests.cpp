#include "HudModel.h"

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
    ok &= Check(defaults.backgroundOpacity == 0.5f, "default opacity");

    const auto dc = FormatHud(MakeGameDcSample());
    ok &= Check(JoinHudRuns(dc) == L"DX11 60 FPS | CPU 36% 67\u00B0C | GPU 98% | TDP 18 W | SYS 24 W | FAN 3540 RPM | BAT 72% 2.5h", "game DC formatting");
    ok &= Check(dc.size() == 7, "game DC segment count");

    ok &= Check(JoinHudRuns(FormatHud(MakeGameAcSample())) ==
        L"DX11 60 FPS | CPU 36% 67\u00B0C | GPU 98% | TDP 18 W | FAN 3540 RPM | BAT 72%", "game AC formatting");
    ok &= Check(JoinHudRuns(FormatHud(MakeNoGameAlwaysSample())) ==
        L"CPU 36% 67\u00B0C | GPU 98% | TDP 18 W | SYS 24 W | FAN 3540 RPM | BAT 72% 2.5h", "no-game formatting");
    ok &= Check(ShouldShowHud(HudVisibilityMode::Always, false), "always visibility");
    ok &= Check(!ShouldShowHud(HudVisibilityMode::InGameOnly, false), "in-game-only visibility");
    ok &= Check(ShouldShowHud(HudVisibilityMode::InGameOnly, true), "foreground game visibility");

    HudTelemetrySnapshot missing{};
    missing.graphicsApi = L"DX12";
    missing.cpuUsagePercent = 0.0;
    missing.cpuTemperatureC = 0;
    missing.fan1Rpm = 3200;
    ok &= Check(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0\u00B0C | FAN 3200 RPM", "zero values stay explicit");
    missing.fan1Rpm.reset();
    missing.fan2Rpm = 3600;
    ok &= Check(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0\u00B0C | FAN 3600 RPM", "single fan formatting");
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
    ok &= Check(FormatHud(HudTelemetrySnapshot{}).empty(), "empty snapshot omitted");

    HudTelemetrySnapshot displayed{};
    displayed.presentMonDisplayedFps = 120.0;
    ok &= Check(JoinHudRuns(FormatHud(displayed)) == L"FPS 120",
        "PresentMon displayed FPS formatting");
    displayed.graphicsApi = L"DX12";
    ok &= Check(JoinHudRuns(FormatHud(displayed)) == L"DX12 120 FPS",
        "graphics API and displayed FPS formatting");

    HudTelemetrySnapshot unavailableApi{};
    unavailableApi.presentMonDisplayedFps = 120.0;
    unavailableApi.cpuUsagePercent = 33.0;
    unavailableApi.gpuUsagePercent = 44.0;
    ok &= Check(JoinHudRuns(FormatHud(unavailableApi)) ==
        L"FPS 120 | CPU 33% | GPU 44%",
        "missing graphics API preserves other telemetry");

    HudTelemetrySnapshot vram{};
    vram.gpuUsagePercent = 0.0;
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"GPU 0%",
        "zero GPU usage without VRAM formatting");
    vram.gpuUsagePercent = 87.0;
    vram.gpuMemoryUsedBytes = static_cast<std::uint64_t>(3.4 * 1024.0 * 1024.0 * 1024.0);
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"GPU 87% VRAM 3.4 GB",
        "GPU usage and VRAM formatting");
    vram.gpuUsagePercent.reset();
    ok &= Check(JoinHudRuns(FormatHud(vram)) == L"GPU VRAM 3.4 GB",
        "VRAM-only formatting");

    return ok ? 0 : 1;
}
