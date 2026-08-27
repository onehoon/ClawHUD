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
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"steamwebhelper.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamebar.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"gamebarftserver.exe") &&
        clawhud::IsRejectedProductionTargetImage(L"xboxpcappft.exe"),
        "Steam and Windows gaming shells are not production targets");
    ok &= Check(clawhud::IsRejectedProductionTargetImage(L"explorer.exe"),
        "Explorer is not a production target");
    ok &= Check(!clawhud::IsRejectedProductionTargetImage(L"game.exe"),
        "game process remains an eligible candidate");
    ok &= Check(!clawhud::ShouldRetainCommittedProductionTarget(0, true),
        "uncommitted foreground candidate is replaceable");
    ok &= Check(clawhud::ShouldRetainCommittedProductionTarget(42, true) &&
        !clawhud::ShouldRetainCommittedProductionTarget(42, false),
        "live committed game is retained but exited game is not");
    ok &= Check(!clawhud::ShouldConfirmProductionTarget(42, 43, true) &&
        !clawhud::ShouldConfirmProductionTarget(42, 42, false) &&
        clawhud::ShouldConfirmProductionTarget(42, 42, true),
        "only matching candidate FPS confirms the target");
    ok &= Check(!clawhud::ShouldConsiderForegroundProductionTarget(true, true, false) &&
        !clawhud::ShouldConsiderForegroundProductionTarget(true, false, true) &&
        clawhud::ShouldConsiderForegroundProductionTarget(true, false, false),
        "diagnostic and suspend states block adoption");
    return ok ? 0 : 1;
}
