#include "App.h"
#include "UninstallCleanup.h"

#include <Velopack.hpp>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    Velopack::VelopackApp::Build()
        .SetAutoApplyOnStartup(false)
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
