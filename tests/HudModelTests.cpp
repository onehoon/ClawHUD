#include "HudModel.h"

#include <cassert>

using namespace clawhud;

int main()
{
    const HudLayoutOptions defaults{};
    assert(defaults.visibilityMode == HudVisibilityMode::InGameOnly);
    assert(defaults.alignment == HudAlignment::Center);
    assert(defaults.backgroundMode == HudBackgroundMode::FullWidth);
    assert(defaults.backgroundOpacity == 0.5f);

    const auto dc = FormatHud(MakeGameDcSample());
    assert(JoinHudRuns(dc) == L"DX11 60 FPS | CPU 36% 67°C | GPU 98% 72°C | TDP 18 W | SYS 24 W | FAN 3540 RPM | BAT 72% 2.5h");
    assert(dc.size() == 7);

    assert(JoinHudRuns(FormatHud(MakeGameAcSample())) ==
        L"DX11 60 FPS | CPU 36% 67°C | GPU 98% 72°C | TDP 18 W | FAN 3540 RPM | BAT 72%");
    assert(JoinHudRuns(FormatHud(MakeNoGameAlwaysSample())) ==
        L"CPU 36% 67°C | GPU 98% 72°C | TDP 18 W | SYS 24 W | FAN 3540 RPM | BAT 72% 2.5h");
    assert(ShouldShowHud(HudVisibilityMode::Always, false));
    assert(!ShouldShowHud(HudVisibilityMode::InGameOnly, false));

    HudTelemetrySnapshot missing{};
    missing.graphicsApi = L"DX12";
    missing.cpuUsagePercent = 0.0;
    missing.cpuTemperatureC = 0;
    missing.fan1Rpm = 3200;
    assert(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0°C | FAN 3200 RPM");
    missing.fan1Rpm.reset();
    missing.fan2Rpm = 3600;
    assert(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0°C | FAN 3600 RPM");
    missing.fan2Rpm.reset();
    assert(JoinHudRuns(FormatHud(missing)) == L"CPU 0% 0°C");
    assert(FormatHud(HudTelemetrySnapshot{}).empty());

    return 0;
}
