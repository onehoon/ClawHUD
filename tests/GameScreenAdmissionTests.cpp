#include "GameDetection/GameScreenAdmission.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}

clawhud::GameScreenObservation Valid()
{
    clawhud::GameScreenObservation observation;
    observation.window = reinterpret_cast<HWND>(0x1234);
    observation.windowExists = true;
    observation.topLevel = true;
    observation.visible = true;
    observation.cloakKnown = true;
    observation.processInspected = true;
    observation.monitorResolved = true;
    observation.boundsResolved = true;
    observation.windowBounds = {0, 0, 1920, 1200};
    observation.monitorBounds = {0, 0, 1920, 1200};
    return observation;
}

void CheckReason(clawhud::GameScreenObservation observation,
    clawhud::GameScreenAdmissionReason expected, const char* message)
{
    Check(clawhud::EvaluateGameScreenAdmission(observation).reason == expected, message);
}
}

int main()
{
    using namespace clawhud;
    auto observation = Valid();
    Check(EvaluateGameScreenAdmission(observation).admitted, "valid observation admitted");
    observation.window = nullptr; CheckReason(observation, GameScreenAdmissionReason::NoWindow, "no HWND");
    observation = Valid(); observation.topLevel = false; CheckReason(observation, GameScreenAdmissionReason::NotTopLevel, "not top-level");
    observation = Valid(); observation.visible = false; CheckReason(observation, GameScreenAdmissionReason::NotVisible, "not visible");
    observation = Valid(); observation.minimized = true; CheckReason(observation, GameScreenAdmissionReason::Minimized, "minimized");
    observation = Valid(); observation.cloakKnown = false; CheckReason(observation, GameScreenAdmissionReason::CloakUnavailable, "unknown cloak is conservative");
    observation = Valid(); observation.cloaked = true; CheckReason(observation, GameScreenAdmissionReason::Cloaked, "cloaked");
    observation = Valid(); observation.processInspected = false; CheckReason(observation, GameScreenAdmissionReason::ProcessUnavailable, "process unavailable");
    observation = Valid(); observation.executableExcluded = true; CheckReason(observation, GameScreenAdmissionReason::ExcludedExecutable, "excluded executable");
    observation = Valid(); observation.monitorResolved = false; CheckReason(observation, GameScreenAdmissionReason::NoMonitor, "monitor unavailable");
    observation = Valid(); observation.boundsResolved = false; CheckReason(observation, GameScreenAdmissionReason::BoundsUnavailable, "bounds unavailable");
    observation = Valid(); observation.windowBounds.bottom = 1128; CheckReason(observation, GameScreenAdmissionReason::NotFullscreenLike, "work area is not fullscreen");

    const RECT monitor{0, 0, 1920, 1200};
    Check(CoversMonitorBounds({0, 0, 1920, 1200}, monitor), "exact bounds");
    Check(CoversMonitorBounds({-3, -3, 1923, 1203}, monitor), "three pixel overscan");
    Check(CoversMonitorBounds({-8, -8, 1928, 1208}, monitor), "eight pixel tolerance");
    Check(!CoversMonitorBounds({-9, -9, 1929, 1209}, monitor), "nine pixel mismatch");
    Check(!CoversMonitorBounds({0, 0, 1920, 1128}, monitor), "work area mismatch");
    const RECT rightMonitor{1920, 0, 3840, 1200};
    Check(CoversMonitorBounds({1917, -3, 3843, 1203}, rightMonitor), "non-zero monitor origin");
    const RECT leftMonitor{-1920, 0, 0, 1200};
    Check(CoversMonitorBounds({-1923, -3, 3, 1203}, leftMonitor), "negative monitor origin");
    std::cout << "PASS\n";
}
