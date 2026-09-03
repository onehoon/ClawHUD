#include "StartupExecutablePath.h"

#include <windows.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

// Cleanup 3 (work order 15): Start-with-Windows targets the stable VeloPack root
// execution stub only when the full installed layout is present; every other
// shape falls back to the running executable.

namespace
{
namespace fs = std::filesystem;

fs::path TempRoot()
{
    wchar_t buffer[MAX_PATH]{};
    GetTempPathW(MAX_PATH, buffer);
    const auto root = fs::path(buffer) /
        (L"ClawHUD.StartupExecutablePathTests." +
            std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

void Touch(const fs::path& file)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file) << "x";
}
}

int main()
{
    const auto root = TempRoot();

    // Full installed layout -> root stub.
    {
        const auto appRoot = root / "installed";
        Touch(appRoot / "current" / "ClawHUD.exe");
        Touch(appRoot / "current" / "sq.version");
        Touch(appRoot / "Update.exe");
        Touch(appRoot / "ClawHUD.exe");
        const auto r = clawhud::ResolveStartupExecutable(
            appRoot / "current" / "ClawHUD.exe");
        assert(r.velopackRootStub);
        assert(r.path == appRoot / "ClawHUD.exe");
    }

    // Portable / dev: parent not named "current".
    {
        const auto portable = root / "portable";
        Touch(portable / "ClawHUD.exe");
        const auto r = clawhud::ResolveStartupExecutable(portable / "ClawHUD.exe");
        assert(!r.velopackRootStub);
        assert(r.path == portable / "ClawHUD.exe");
        assert(r.fallbackReason == L"not-under-current");
    }

    // Parent named "current" but no root Update.exe.
    {
        const auto appRoot = root / "no-update";
        Touch(appRoot / "current" / "ClawHUD.exe");
        Touch(appRoot / "current" / "sq.version");
        Touch(appRoot / "ClawHUD.exe");
        const auto r = clawhud::ResolveStartupExecutable(
            appRoot / "current" / "ClawHUD.exe");
        assert(!r.velopackRootStub);
        assert(r.path == appRoot / "current" / "ClawHUD.exe");
        assert(r.fallbackReason == L"no-update-exe");
    }

    // Root Update.exe present but no root ClawHUD.exe stub.
    {
        const auto appRoot = root / "no-stub";
        Touch(appRoot / "current" / "ClawHUD.exe");
        Touch(appRoot / "current" / "sq.version");
        Touch(appRoot / "Update.exe");
        const auto r = clawhud::ResolveStartupExecutable(
            appRoot / "current" / "ClawHUD.exe");
        assert(!r.velopackRootStub);
        assert(r.fallbackReason == L"no-root-stub");
    }

    // sq.version missing.
    {
        const auto appRoot = root / "no-sqversion";
        Touch(appRoot / "current" / "ClawHUD.exe");
        Touch(appRoot / "Update.exe");
        Touch(appRoot / "ClawHUD.exe");
        const auto r = clawhud::ResolveStartupExecutable(
            appRoot / "current" / "ClawHUD.exe");
        assert(!r.velopackRootStub);
        assert(r.fallbackReason == L"no-sq-version");
    }

    fs::remove_all(root);
    return 0;
}
