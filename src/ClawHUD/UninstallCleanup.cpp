#include "UninstallCleanup.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace
{
constexpr wchar_t kStartupShortcutName[] = L"ClawHUD.lnk";
constexpr wchar_t kUserDataDirectoryName[] = L"ClawHUD";

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

void RemoveUserData(const std::filesystem::path& localAppDataDirectory) noexcept
{
    try
    {
        if (localAppDataDirectory.empty())
            return;

        std::error_code ec;
        std::filesystem::remove_all(
            localAppDataDirectory / kUserDataDirectoryName, ec);
    }
    catch (...)
    {
    }
}

void CleanupForUninstallAtPaths(
    const std::filesystem::path& startupDirectory,
    const std::filesystem::path& localAppDataDirectory) noexcept
{
    RemoveStartupShortcut(startupDirectory);
    RemoveUserData(localAppDataDirectory);
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

    try
    {
        RemoveUserData(KnownFolderPath(FOLDERID_LocalAppData));
    }
    catch (...)
    {
    }
}

namespace testing
{
void CleanupForUninstall(
    const std::filesystem::path& startupDirectory,
    const std::filesystem::path& localAppDataDirectory) noexcept
{
    CleanupForUninstallAtPaths(startupDirectory, localAppDataDirectory);
}
}
}
