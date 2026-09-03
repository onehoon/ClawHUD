#include "StartupExecutablePath.h"

#include <system_error>

namespace clawhud
{
namespace
{
bool RegularFileExists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}
}

StartupExecutable ResolveStartupExecutable(
    const std::filesystem::path& processExecutable)
{
    StartupExecutable result;
    result.path = processExecutable;

    const auto currentDir = processExecutable.parent_path();
    if (currentDir.filename() != L"current")
    {
        result.fallbackReason = L"not-under-current";
        return result;
    }
    const auto rootDir = currentDir.parent_path();
    if (rootDir.empty())
    {
        result.fallbackReason = L"no-root";
        return result;
    }
    if (!RegularFileExists(rootDir / L"Update.exe"))
    {
        result.fallbackReason = L"no-update-exe";
        return result;
    }
    const auto rootStub = rootDir / L"ClawHUD.exe";
    if (!RegularFileExists(rootStub))
    {
        result.fallbackReason = L"no-root-stub";
        return result;
    }
    if (!RegularFileExists(currentDir / L"sq.version"))
    {
        result.fallbackReason = L"no-sq-version";
        return result;
    }

    result.path = rootStub;
    result.velopackRootStub = true;
    return result;
}
}
