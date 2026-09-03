#include "SettingsFrontendLauncher.h"

#include <windows.h>

#include <cassert>
#include <filesystem>

// PR6: the WPF Settings frontend always ships beside ClawHUD.exe, so it is
// resolved as a sibling of the running runtime executable — never via %PATH% or
// the working directory. Only the pure path resolver is exercised here; the
// actual ShellExecuteExW launch is covered by manual smoke.

namespace
{
namespace fs = std::filesystem;
}

int main()
{
    // Normal installed layout.
    assert(clawhud::SettingsFrontendPath(LR"(C:\Apps\ClawHUD\ClawHUD.exe)") ==
        fs::path(LR"(C:\Apps\ClawHUD\ClawHUD.Settings.exe)"));

    // VeloPack `current` layout — sibling of ClawHUD.exe, not the root stub.
    assert(clawhud::SettingsFrontendPath(LR"(C:\Apps\ClawHUD\current\ClawHUD.exe)") ==
        fs::path(LR"(C:\Apps\ClawHUD\current\ClawHUD.Settings.exe)"));

    // Path containing spaces.
    assert(clawhud::SettingsFrontendPath(LR"(C:\Path With Spaces\ClawHUD.exe)") ==
        fs::path(LR"(C:\Path With Spaces\ClawHUD.Settings.exe)"));

    // Resolved filename is exactly ClawHUD.Settings.exe.
    assert(clawhud::SettingsFrontendPath(LR"(D:\x\ClawHUD.exe)").filename() ==
        L"ClawHUD.Settings.exe");

    // The current working directory must not affect resolution.
    {
        wchar_t original[MAX_PATH]{};
        GetCurrentDirectoryW(MAX_PATH, original);
        SetCurrentDirectoryW(L"C:\\Windows");
        const auto resolved =
            clawhud::SettingsFrontendPath(LR"(E:\install\ClawHUD.exe)");
        SetCurrentDirectoryW(original);
        assert(resolved == fs::path(LR"(E:\install\ClawHUD.Settings.exe)"));
    }

    // LaunchSettingsFrontend is deliberately not exercised here: it shows a
    // Win32 message box for a missing frontend and calls ShellExecuteExW on
    // success, both of which belong to manual smoke, not headless CI.

    return 0;
}
