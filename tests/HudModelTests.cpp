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
    ok &= Check(JoinHudRuns(dc) == L"DX11 60 FPS | CPU 36% 67°C | GPU 98% 72°C | TDP 18 W | SYS 24 W | FAN 3540 RPM | BAT 72% 2.5h", "game DC formatting");
    ok &= Check(dc.size() == 7, "game DC segment count");

    ok &= Check(JoinHudRuns(FormatHud(MakeGameAcSample())) ==
        L"DX11 60 FPS | CPU 36% 67°C | GPU 98% 72°C | TDP 18 W | FAN 3540 RPM | BAT 72%", "game AC formatting");
    ok &= Check(JoinHudRuns(FormatHud(MakeNoGameAlwaysSample())) ==
        L"CPU 36% 67°C | GPU 98% 72°C | TDP 18 W | SYS 24 W | FAN 3540 RPM | BAT 72% 2.5h", "no-game formatting");
    ok &= Check(ShouldShowHud(HudVisibilityMode::Always, false), "always visibility");
    ok &= Check(!ShouldShowHud(HudVisibilityMode::InGameOnly, false), "in-game-only visibility");

    HudTelemetrySnapshot missing{};
    missing.graphicsApi = L"DX12";
    missing.cpuUsagePercent = 0.0;
    missing.cpuTemperatureC = 0;
    missing.fan1Rpm = 3200;
    ok &= Check(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0°C | FAN 3200 RPM", "zero values stay explicit");
    missing.fan1Rpm.reset();
    missing.fan2Rpm = 3600;
    ok &= Check(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0°C | FAN 3600 RPM", "single fan formatting");
    missing.fan2Rpm.reset();
    ok &= Check(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0°C", "missing fans omitted");

    HudTelemetrySnapshot temperaturesOnly{};
    temperaturesOnly.cpuTemperatureC = 48;
    temperaturesOnly.gpuTemperatureC = 44;
    ok &= Check(JoinHudRuns(FormatHud(temperaturesOnly)) == L"CPU 48°C | GPU 44°C", "temperature-only formatting");
    ok &= Check(FormatHud(HudTelemetrySnapshot{}).empty(), "empty snapshot omitted");

    return ok ? 0 : 1;
}
