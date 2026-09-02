#include "App.h"
#include "LaunchMode.h"
#include "PresentMonRuntimeBootstrap.h"
#include "UninstallCleanup.h"

#include <Velopack.hpp>

#include <string>
#include <string_view>
#include <vector>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // Velopack fast-exit lifecycle hooks (install / uninstall / update apply)
    // may terminate inside Run(); this must be the first application code.
    Velopack::VelopackApp::Build()
        .SetAutoApplyOnStartup(false)
        .OnBeforeUninstall(
            [](void*, const char*) noexcept
            {
                clawhud::CleanupForUninstall();
            })
        .Run();

    // Reached only for a normal ClawHUD launch (Standalone, a Velopack restart,
    // or a future owner-driven `--managed` launch after a restart=false Managed
    // update). EnsurePresentMonRuntime() is a cheap readiness check that only
    // reaches the elevated MSI path when the runtime is actually missing or
    // incompatible, so it no longer depends on Velopack's OnFirstRun /
    // OnRestarted hooks.
    clawhud::EnsurePresentMonRuntime();

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return 1;

    clawhud::LaunchMode launchMode = clawhud::LaunchMode::Standalone;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc))
    {
        std::vector<std::wstring_view> arguments;
        for (int i = 1; i < argc; ++i)
            arguments.emplace_back(argv[i]);
        launchMode = clawhud::ResolveLaunchMode(arguments);
        LocalFree(argv);
    }

    App app(instance, launchMode);
    return app.Run();
}
