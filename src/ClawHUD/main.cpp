#include "App.h"
#include "LaunchMode.h"
#include "UninstallCleanup.h"
#include "StartupTaskRegistration.h"

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
    // or an owner-driven `--managed` launch after a restart=false Managed
    // update). The PresentMon shared-runtime prerequisite is now gated inside
    // App::Run() -- after the single-instance and supported-hardware gates -- so
    // an unsupported device or a losing second instance never triggers the
    // elevated MSI path.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return 1;

    std::vector<std::wstring_view> arguments;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc))
    {
        for (int i = 1; i < argc; ++i)
            arguments.emplace_back(argv[i]);
        LocalFree(argv);
    }

    // Private Task Scheduler helper commands (--ensure-startup-task /
    // --remove-startup-task) are dispatched here, before App exists: the
    // elevated helper child performs one fixed task mutation and exits
    // without initializing App, update checking, the hardware gate, the
    // PresentMon runtime, the tray, the HUD, telemetry, the EC helper, or
    // Control IPC. See StartupTaskRegistration.h.
    if (auto helperExit = clawhud::TryRunStartupTaskHelperCommand(arguments))
        return *helperExit;

    const clawhud::LaunchMode launchMode = clawhud::ResolveLaunchMode(arguments);

    App app(instance, launchMode);
    return app.Run();
}
