#include "DebugObservationController.h"

#include "../RuntimeLogger.h"

namespace clawhud
{
DebugObservationController::DebugObservationController(
    PresentMonTelemetryProvider& provider)
    : provider_(provider)
{
}

DebugObservationController::~DebugObservationController()
{
    Stop();
}

void DebugObservationController::Start()
{
    if (!processLifecycleSource_.Start())
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"Process lifecycle diagnostic source failed to start; continuing");
    if (!windowLifecycleSource_.Start())
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            L"Window lifecycle diagnostic source failed to start; continuing");
    presentActivitySource_.Start(provider_);
}

void DebugObservationController::OnForegroundChanged(HWND window, DWORD processId) noexcept
{
    windowsGameIdentitySource_.QueueInspect(window, processId);
    presentActivitySource_.Watch(processId);
}

void DebugObservationController::Stop() noexcept
{
    windowLifecycleSource_.Stop();
    presentActivitySource_.Stop();
    processLifecycleSource_.Stop();
}
}
