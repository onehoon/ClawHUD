#include "PresentMonApi2Client.h"

#include <fstream>
#include <iostream>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool ok = true;
    const auto root = std::filesystem::temp_directory_path() /
        "ClawHUD-PresentMonApi2ClientTests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "app");
    const auto module = root / "app" / "ClawHUD.exe";
    const auto loader = root / "app" / "PresentMonAPI2Loader.dll";

    ok &= Check(PresentMonApi2AppLocalLoaderPath(module).empty(),
        "missing app-local loader is unavailable");
    std::ofstream(loader).put('x');
    ok &= Check(PresentMonApi2AppLocalLoaderPath(module) == loader,
        "app-local loader is selected");
    std::filesystem::create_directories(root / "Intel" / "PresentMon" / "SDK");
    const auto sdkLoader = root / "Intel" / "PresentMon" / "SDK" /
        "PresentMonAPI2Loader.dll";
    std::ofstream(sdkLoader).put('x');
    std::filesystem::remove(loader);
    ok &= Check(PresentMonApi2AppLocalLoaderPath(module).empty(),
        "SDK loader is never selected");

    PresentMonApi2Client client;
    ok &= Check(!client.Initialized() && !client.SessionOpen(),
        "new client starts uninitialized");
    client.Shutdown();
    client.Shutdown();
    ok &= Check(!client.Initialized() && !client.SessionOpen(),
        "repeated shutdown is safe");
    const bool initialized = client.Initialize();
    ok &= Check(initialized == client.Initialized(),
        "loader initialization state is internally consistent");
    if (!initialized)
    {
        ok &= Check(client.InitStatus().failure != PresentMonApi2InitFailure::None,
            "failed initialization preserves a failure reason");
        ok &= Check(std::string(PresentMonApi2InitFailureName(
            client.InitStatus().failure)) != "NONE",
            "failed initialization has a readable failure reason");
    }
    client.Shutdown();
    ok &= Check(!client.Initialized() && !client.SessionOpen(),
        "shutdown clears initialization after either initialization result");
    std::filesystem::remove_all(root);
    return ok ? 0 : 1;
}
