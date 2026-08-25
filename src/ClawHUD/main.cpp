#include "App.h"

#include <Velopack.hpp>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    Velopack::VelopackApp::Build().SetAutoApplyOnStartup(false).Run();
    App app(instance);
    return app.Run();
}
