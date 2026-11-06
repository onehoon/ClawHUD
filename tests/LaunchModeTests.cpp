#include "LaunchMode.h"
#include "RuntimeLifecyclePolicy.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

using clawhud::LaunchMode;
using clawhud::ResolveLaunchMode;
using clawhud::ManagedStartupExitCode;
using clawhud::ManagedStartupExitCodeForSingleInstanceResult;
using clawhud::SingleInstanceResult;
using clawhud::ShouldAllowStartWithWindowsMutation;
using clawhud::ShouldFailStartupWhenControlIpcUnavailable;
using clawhud::ShouldReconcileStartupRegistration;
using clawhud::ShouldRegisterGlobalHudHotkey;
using clawhud::ShouldRestartAfterVelopackUpdate;
using clawhud::ShouldRunSelfUpdate;
using clawhud::ShouldShowStartupFailureUi;
using clawhud::ShouldInitializeFreshManagedSettings;
using clawhud::ToWireLaunchMode;
namespace ctl = clawhud::control;

namespace
{
int g_failures = 0;

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

LaunchMode Resolve(std::initializer_list<std::wstring_view> args)
{
    std::vector<std::wstring_view> tokens(args);
    return ResolveLaunchMode(std::span<const std::wstring_view>(tokens));
}

void ParserRules()
{
    Check(Resolve({}) == LaunchMode::Standalone, "no arguments -> Standalone");
    Check(Resolve({L"--managed"}) == LaunchMode::Managed, "--managed -> Managed");
    Check(Resolve({L"--unknown"}) == LaunchMode::Standalone,
        "unknown argument only -> Standalone");
    Check(Resolve({L"--unknown", L"--managed"}) == LaunchMode::Managed,
        "unknown + --managed -> Managed");
    Check(Resolve({L"--managed", L"--unknown"}) == LaunchMode::Managed,
        "--managed + unknown -> Managed");

    // Case-sensitive, exact token only. No aliases.
    Check(Resolve({L"--Managed"}) == LaunchMode::Standalone, "--Managed is not the token");
    Check(Resolve({L"-managed"}) == LaunchMode::Standalone, "-managed is not the token");
    Check(Resolve({L"/managed"}) == LaunchMode::Standalone, "/managed is not the token");
    Check(Resolve({L"--managed=1"}) == LaunchMode::Standalone, "--managed=1 is not the token");
    Check(Resolve({L"--headless"}) == LaunchMode::Standalone, "--headless is not the token");
    Check(Resolve({L" --managed"}) == LaunchMode::Standalone, "leading space is not the token");
}

void WireMapping()
{
    Check(ToWireLaunchMode(LaunchMode::Standalone) == ctl::WireLaunchMode::Standalone,
        "Standalone -> WireLaunchMode::Standalone");
    Check(ToWireLaunchMode(LaunchMode::Managed) == ctl::WireLaunchMode::Managed,
        "Managed -> WireLaunchMode::Managed");
}

void LifecyclePolicy()
{
    // Startup shortcut reconciliation at launch: Standalone only.
    Check(ShouldReconcileStartupRegistration(LaunchMode::Standalone),
        "Standalone reconciles the startup shortcut at launch");
    Check(!ShouldReconcileStartupRegistration(LaunchMode::Managed),
        "Managed launch does not touch the startup shortcut");
    Check(ShouldAllowStartWithWindowsMutation(LaunchMode::Standalone),
        "Standalone accepts Start with Windows mutations");
    Check(!ShouldAllowStartWithWindowsMutation(LaunchMode::Managed),
        "Managed rejects Start with Windows mutations");

    Check(ShouldRunSelfUpdate(LaunchMode::Standalone),
        "Standalone runs the ClawHUD self-update path");
    Check(!ShouldRunSelfUpdate(LaunchMode::Managed),
        "Managed does not run the ClawHUD self-update path");
    Check(ShouldShowStartupFailureUi(LaunchMode::Standalone),
        "Standalone owns startup failure UI");
    Check(!ShouldShowStartupFailureUi(LaunchMode::Managed),
        "Managed does not show ClawHUD startup failure UI");
    Check(!ShouldFailStartupWhenControlIpcUnavailable(LaunchMode::Standalone),
        "Standalone tolerates Control IPC startup failure");
    Check(ShouldFailStartupWhenControlIpcUnavailable(LaunchMode::Managed),
        "Managed treats Control IPC startup failure as fatal");
    Check(ShouldRegisterGlobalHudHotkey(LaunchMode::Standalone),
        "Standalone registers the global HUD hotkey");
    Check(!ShouldRegisterGlobalHudHotkey(LaunchMode::Managed),
        "Managed does not register the global HUD hotkey");
    Check(!ShouldInitializeFreshManagedSettings(LaunchMode::Standalone),
        "Standalone does not eagerly create settings.ini");
    Check(ShouldInitializeFreshManagedSettings(LaunchMode::Managed),
        "Managed creates fresh settings only after IPC readiness");

    Check(!ManagedStartupExitCodeForSingleInstanceResult(SingleInstanceResult::Acquired),
        "an acquired mutex does not map to a failure");
    Check(ManagedStartupExitCodeForSingleInstanceResult(
        SingleInstanceResult::AlreadyRunning) == ManagedStartupExitCode::AlreadyRunning,
        "an existing mutex maps to Managed exit 20");
    Check(ManagedStartupExitCodeForSingleInstanceResult(SingleInstanceResult::Failed) ==
        ManagedStartupExitCode::RuntimeInitializationFailed,
        "a mutex failure maps to Managed exit 40");

    // Velopack restart-after-apply: Standalone only. The same predicate drives
    // both the pending-update and newly-downloaded-update paths.
    Check(ShouldRestartAfterVelopackUpdate(LaunchMode::Standalone),
        "Standalone update restarts ClawHUD after apply");
    Check(!ShouldRestartAfterVelopackUpdate(LaunchMode::Managed),
        "Managed update applies with restart=false");

    static_assert(ShouldReconcileStartupRegistration(LaunchMode::Standalone));
    static_assert(!ShouldRestartAfterVelopackUpdate(LaunchMode::Managed));
    static_assert(!ShouldAllowStartWithWindowsMutation(LaunchMode::Managed));
    static_assert(!ShouldRunSelfUpdate(LaunchMode::Managed));
    static_assert(!ShouldShowStartupFailureUi(LaunchMode::Managed));
    static_assert(ShouldFailStartupWhenControlIpcUnavailable(LaunchMode::Managed));
    static_assert(!ShouldRegisterGlobalHudHotkey(LaunchMode::Managed));
    static_assert(ShouldInitializeFreshManagedSettings(LaunchMode::Managed));
}

void EmptySpan()
{
    // A null / empty span must not read out of bounds.
    Check(ResolveLaunchMode(std::span<const std::wstring_view>{}) == LaunchMode::Standalone,
        "empty span -> Standalone");
}
}

int main()
{
    ParserRules();
    WireMapping();
    LifecyclePolicy();
    EmptySpan();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "LaunchModeTests: all checks passed\n";
    return EXIT_SUCCESS;
}
