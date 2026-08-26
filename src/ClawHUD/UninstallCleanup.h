#pragma once

#include <filesystem>

namespace clawhud
{
std::filesystem::path StartupShortcutPath();
void CleanupForUninstall() noexcept;

namespace testing
{
void CleanupForUninstall(
    const std::filesystem::path& startupDirectory,
    const std::filesystem::path& localAppDataDirectory) noexcept;
}
}
