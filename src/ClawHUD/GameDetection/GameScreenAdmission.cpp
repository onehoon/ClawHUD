#include "GameScreenAdmission.h"

#include "ProductionTargetPolicy.h"

#include <dwmapi.h>

#include <cstdlib>

namespace clawhud
{
namespace
{
bool WithinTolerance(LONG left, LONG right, LONG tolerance) noexcept
{
    const auto difference = static_cast<long long>(left) - static_cast<long long>(right);
    return std::llabs(difference) <= tolerance;
}
}

bool CoversMonitorBounds(const RECT& windowBounds, const RECT& monitorBounds,
    LONG tolerancePx) noexcept
{
    return tolerancePx >= 0 &&
        WithinTolerance(windowBounds.left, monitorBounds.left, tolerancePx) &&
        WithinTolerance(windowBounds.top, monitorBounds.top, tolerancePx) &&
        WithinTolerance(windowBounds.right, monitorBounds.right, tolerancePx) &&
        WithinTolerance(windowBounds.bottom, monitorBounds.bottom, tolerancePx);
}

GameScreenAdmissionResult EvaluateGameScreenAdmission(
    const GameScreenObservation& observation) noexcept
{
    const auto reject = [](GameScreenAdmissionReason reason)
        { return GameScreenAdmissionResult{false, reason}; };
    if (!observation.window || !observation.windowExists) return reject(GameScreenAdmissionReason::NoWindow);
    if (!observation.topLevel) return reject(GameScreenAdmissionReason::NotTopLevel);
    if (!observation.visible) return reject(GameScreenAdmissionReason::NotVisible);
    if (observation.minimized) return reject(GameScreenAdmissionReason::Minimized);
    // A failed DWM query is unknown visibility evidence, not known admission.
    if (!observation.cloakKnown) return reject(GameScreenAdmissionReason::CloakUnavailable);
    if (observation.cloaked) return reject(GameScreenAdmissionReason::Cloaked);
    if (!observation.processInspected) return reject(GameScreenAdmissionReason::ProcessUnavailable);
    if (observation.executableExcluded) return reject(GameScreenAdmissionReason::ExcludedExecutable);
    if (!observation.monitorResolved) return reject(GameScreenAdmissionReason::NoMonitor);
    if (!observation.boundsResolved) return reject(GameScreenAdmissionReason::BoundsUnavailable);
    if (!CoversMonitorBounds(observation.windowBounds, observation.monitorBounds))
        return reject(GameScreenAdmissionReason::NotFullscreenLike);
    return {true, GameScreenAdmissionReason::Admitted};
}

GameScreenObservation ObserveGameScreen(HWND window, DWORD processId) noexcept
{
    GameScreenObservation result;
    result.window = window;
    result.processId = processId;
    result.windowExists = window && IsWindow(window);
    if (!result.windowExists) return result;
    result.topLevel = GetAncestor(window, GA_ROOT) == window;
    result.visible = IsWindowVisible(window) != FALSE;
    result.minimized = IsIconic(window) != FALSE;
    DWORD cloaked{};
    result.cloakKnown = SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED,
        &cloaked, sizeof(cloaked)));
    result.cloaked = cloaked != 0;

    const auto process = InspectProductionTargetProcessDetailed(processId);
    result.processInspected = process.status != ProductionTargetInspectionStatus::Unavailable;
    result.executableExcluded = process.status == ProductionTargetInspectionStatus::Excluded;

    RECT bounds{};
    result.boundsResolved = SUCCEEDED(DwmGetWindowAttribute(window,
        DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))) || GetWindowRect(window, &bounds);
    result.windowBounds = bounds;
    MONITORINFO monitor{sizeof(monitor)};
    const auto handle = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    result.monitorResolved = handle && GetMonitorInfoW(handle, &monitor);
    result.monitorBounds = monitor.rcMonitor;
    return result;
}
}
