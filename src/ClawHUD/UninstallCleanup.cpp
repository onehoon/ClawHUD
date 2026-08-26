#include "UninstallCleanup.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace
{
constexpr wchar_t kStartupShortcutName[] = L"ClawHUD.lnk";

std::filesystem::path KnownFolderPath(REFKNOWNFOLDERID folderId)
{
    PWSTR value{};
    if (FAILED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &value)))
        return {};

    std::filesystem::path path(value);
    CoTaskMemFree(value);
    return path;
}

void RemoveStartupShortcut(const std::filesystem::path& startupDirectory) noexcept
{
    try
    {
        if (startupDirectory.empty())
            return;

        std::error_code ec;
        std::filesystem::remove(
            startupDirectory / kStartupShortcutName, ec);
    }
    catch (...)
    {
    }
}

}

namespace clawhud
{
std::filesystem::path StartupShortcutPath()
{
    const auto startupDirectory = KnownFolderPath(FOLDERID_Startup);
    return startupDirectory.empty()
        ? std::filesystem::path{}
        : startupDirectory / kStartupShortcutName;
}

void CleanupForUninstall() noexcept
{
    try
    {
        const auto shortcut = StartupShortcutPath();
        RemoveStartupShortcut(shortcut.parent_path());
    }
    catch (...)
    {
    }
}

namespace testing
{
void CleanupForUninstall(
    const std::filesystem::path& startupDirectory) noexcept
{
    RemoveStartupShortcut(startupDirectory);
}
}
}
