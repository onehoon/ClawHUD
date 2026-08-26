#include "UninstallCleanup.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

void WriteFile(const std::filesystem::path& path)
{
    std::ofstream file(path);
    file << "test";
}
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
        (L"ClawHUD.UninstallCleanupTests." +
            std::to_wstring(GetCurrentProcessId()));
    const auto startup = root / L"Startup";
    const auto localAppData = root / L"LocalAppData";
    const auto userData = localAppData / L"ClawHUD";
    const auto shortcut = startup / L"ClawHUD.lnk";
    const auto otherShortcut = startup / L"OtherApp.lnk";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(startup);
    std::filesystem::create_directories(userData / L"logs");
    WriteFile(shortcut);
    WriteFile(otherShortcut);
    WriteFile(userData / L"settings.ini");
    WriteFile(userData / L"tweaks-intel-vrr-result.ini");
    WriteFile(userData / L"logs" / L"intel-vrr-range-fix.log");

    clawhud::testing::CleanupForUninstall(startup, localAppData);

    bool ok = true;
    ok &= Check(!std::filesystem::exists(shortcut),
        "ClawHUD startup shortcut is removed");
    ok &= Check(std::filesystem::exists(otherShortcut),
        "unrelated startup shortcut is preserved");
    ok &= Check(!std::filesystem::exists(userData),
        "ClawHUD local app data is removed");

    clawhud::testing::CleanupForUninstall(startup, localAppData);
    ok &= Check(std::filesystem::exists(otherShortcut),
        "cleanup is idempotent and preserves unrelated shortcut");

    std::filesystem::remove_all(root, ec);
    return ok ? 0 : 1;
}
