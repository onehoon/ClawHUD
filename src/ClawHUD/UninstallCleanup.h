#pragma once

#include <filesystem>

namespace clawhud
{
std::filesystem::path StartupShortcutPath();
void CleanupForUninstall() noexcept;

namespace testing
{
void CleanupForUninstall(
    const std::filesystem::path& startupDirectory) noexcept;
}
}
