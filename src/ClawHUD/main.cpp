#include "App.h"
#include "PresentMonRuntimeBootstrap.h"
#include "UninstallCleanup.h"

#include <Velopack.hpp>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    Velopack::VelopackApp::Build()
        .SetAutoApplyOnStartup(false)
        .OnFirstRun(
            [](void*, const char*)
            {
                clawhud::EnsurePresentMonRuntime();
            })
        .OnRestarted(
            [](void*, const char*)
            {
                clawhud::EnsurePresentMonRuntime();
            })
        .OnBeforeUninstall(
            [](void*, const char*) noexcept
            {
                clawhud::CleanupForUninstall();
            })
        .Run();

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return 1;

    App app(instance);
    return app.Run();
}
