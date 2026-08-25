#include "VrrDiagnostic.h"

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
    ok &= Check(!DiagnosticHudModeUsesPeriodicUpdates(DiagnosticHudMode::Off), "OFF stops periodic updates");
    ok &= Check(!DiagnosticHudModeUsesPeriodicUpdates(DiagnosticHudMode::Static), "STATIC stops periodic updates");
    ok &= Check(DiagnosticHudModeUsesPeriodicUpdates(DiagnosticHudMode::Dynamic), "DYNAMIC enables periodic updates");
    return ok ? 0 : 1;
}
