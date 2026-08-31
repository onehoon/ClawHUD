#pragma once

#include <windows.h>

#include "WindowsGameIdentitySource.h"
#include "ProcessLifecycleSource.h"
#include "WindowLifecycleSource.h"
#include "PresentActivitySource.h"

namespace clawhud
{
class PresentMonTelemetryProvider;

// Owns the four debug-only observation sources that used to sit directly on App.
// App holds it through a std::unique_ptr and constructs it only when the
// developer [Developer] DebugLoggingEnabled switch is on, so a normal
// DebugLoggingEnabled=false startup never builds any of these objects -- in
// particular WindowsGameIdentitySource, which starts a worker thread in its
// constructor.
//
// Observation only: nothing here ever feeds production game detection,
// telemetry, or HUD state. The controller holds a non-owning reference to the
// one shared PresentMonTelemetryProvider (PresentActivitySource lease only).
class DebugObservationController
{
public:
    explicit DebugObservationController(PresentMonTelemetryProvider& provider);
    ~DebugObservationController();

    DebugObservationController(const DebugObservationController&) = delete;
    DebugObservationController& operator=(const DebugObservationController&) = delete;

    // Best-effort start of the explicitly-started sources, in the same order and
    // with the same non-fatal warning behavior as the old App::Run debug block.
    // WindowsGameIdentitySource has no Start(); its worker already runs from
    // construction.
    void Start();

    // Foreground changed: queue a Windows-identity inspection, then repoint the
    // PresentActivity watch -- same order as the old App foreground hook tail.
    void OnForegroundChanged(HWND window, DWORD processId) noexcept;

    // Stop order matches the old App shutdown block.
    void Stop() noexcept;

private:
    PresentMonTelemetryProvider& provider_;
    WindowsGameIdentitySource windowsGameIdentitySource_;
    ProcessLifecycleSource processLifecycleSource_;
    PresentActivitySource presentActivitySource_;
    WindowLifecycleSource windowLifecycleSource_;
};
}
