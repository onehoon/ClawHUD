#include "LaunchMode.h"
#include "RuntimeLifecyclePolicy.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

using clawhud::LaunchMode;
using clawhud::ResolveLaunchMode;
using clawhud::ShouldReconcileStartupRegistration;
using clawhud::ShouldRestartAfterVelopackUpdate;
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

    // Velopack restart-after-apply: Standalone only. The same predicate drives
    // both the pending-update and newly-downloaded-update paths.
    Check(ShouldRestartAfterVelopackUpdate(LaunchMode::Standalone),
        "Standalone update restarts ClawHUD after apply");
    Check(!ShouldRestartAfterVelopackUpdate(LaunchMode::Managed),
        "Managed update applies with restart=false");

    static_assert(ShouldReconcileStartupRegistration(LaunchMode::Standalone));
    static_assert(!ShouldRestartAfterVelopackUpdate(LaunchMode::Managed));
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
