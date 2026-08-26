#include "ProductionTargetPolicy.h"

#include <iostream>

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
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"steam.exe"),
        "Steam launcher is not a production target");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"explorer.exe"),
        "Explorer is not a production target");
    ok &= Check(!clawhud::IsRejectedProductionTargetImage(L"game.exe"),
        "game process remains an eligible candidate");
    return ok ? 0 : 1;
}
