#include "App.h"

#include <Velopack.hpp>

#include <filesystem>
#include <string>

namespace
{
bool IsVelopackInstalled()
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (length == 0 || length == ARRAYSIZE(path)) return false;
    return std::filesystem::exists(std::filesystem::path(path).parent_path() / L"Update.exe");
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    if (IsVelopackInstalled())
        Velopack::VelopackApp::Build().SetAutoApplyOnStartup(false).Run();
    App app(instance);
    return app.Run();
}
