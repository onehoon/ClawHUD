#include "SettingsWindowGeometry.h"

#include <iostream>

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    const RECT work{ 0, 0, 1920, 1080 };
    bool ok = true;
    const auto same = [](RECT actual, RECT expected)
    {
        return EqualRect(&actual, &expected) != FALSE;
    };
    ok &= Check(same(ClampSettingsWindowRectToWorkArea(
        RECT{ 100, 100, 900, 700 }, work, 600, 420), RECT{ 100, 100, 900, 700 }),
        "valid window unchanged");
    ok &= Check(same(ClampSettingsWindowRectToWorkArea(
        RECT{ 1500, 800, 2300, 1400 }, work, 600, 420), RECT{ 1120, 480, 1920, 1080 }),
        "off-screen right and bottom clamped");
    ok &= Check(same(ClampSettingsWindowRectToWorkArea(
        RECT{ -500, -200, 300, 500 }, work, 600, 420), RECT{ 0, 0, 800, 700 }),
        "off-screen left and top clamped");
    ok &= Check(same(ClampSettingsWindowRectToWorkArea(
        RECT{ 0, 0, 2400, 1400 }, work, 600, 420), RECT{ 0, 0, 1920, 1080 }),
        "oversized window shrunk to work area");
    ok &= Check(same(ClampSettingsWindowRectToWorkArea(
        RECT{ 10, 10, 400, 300 }, work, 600, 420), RECT{ 10, 10, 610, 430 }),
        "undersized window enlarged to minimum");
    const RECT smallWork{ 0, 0, 500, 300 };
    ok &= Check(same(ClampSettingsWindowRectToWorkArea(
        RECT{ 10, 10, 200, 150 }, smallWork, 600, 420), RECT{ 0, 0, 500, 300 }),
        "small work area bounds minimum");
    return ok ? 0 : 1;
}
