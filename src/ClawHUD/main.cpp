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
