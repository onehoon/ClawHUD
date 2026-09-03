#include "SettingsFrontendLauncher.h"

#include "RuntimeLogger.h"

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <system_error>

namespace clawhud
{
std::filesystem::path SettingsFrontendPath(
    const std::filesystem::path& runtimeExecutable)
{
    return runtimeExecutable.parent_path() / L"ClawHUD.Settings.exe";
}

bool LaunchSettingsFrontend(const std::filesystem::path& runtimeExecutable)
{
    const std::filesystem::path frontend = SettingsFrontendPath(runtimeExecutable);

    std::error_code ec;
    if (!std::filesystem::exists(frontend, ec))
    {
        RuntimeLogger::Log(RuntimeLogLevel::Error,
            L"Settings frontend not found beside ClawHUD: " + frontend.wstring());
        MessageBoxW(nullptr,
            L"ClawHUD Settings could not be found next to ClawHUD.\n\n"
            L"Reinstall ClawHUD to restore the Settings window.",
            L"ClawHUD", MB_OK | MB_ICONERROR);
        return false;
    }

    const std::wstring file = frontend.wstring();
    const std::wstring directory = frontend.parent_path().wstring();

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = file.c_str();
    info.lpDirectory = directory.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info))
    {
        RuntimeLogger::Log(RuntimeLogLevel::Error,
            L"ShellExecuteExW failed for Settings frontend, error " +
            std::to_wstring(GetLastError()));
        return false;
    }

    RuntimeLogger::Log(RuntimeLogLevel::Info, L"Launched Settings frontend");
    return true;
}
}
