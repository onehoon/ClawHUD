#include "PresentMonRuntimeBootstrap.h"

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
    const auto msi = PresentMonRuntimeMsiPathForModule(
        L"C:\\Apps\\ClawHUD\\ClawHUD.exe");
    ok &= Check(msi == L"C:\\Apps\\ClawHUD\\runtime\\ClawHUD.PresentMonRuntime.msi",
        "MSI path is resolved beside the executable");
    ok &= Check(PresentMonRuntimeMsiPathForModule({}).empty(),
        "empty module path returns no MSI path");

    const PresentMonRuntimeReadinessEvidence ready{ true, true, true, true, true };
    ok &= Check(IsPresentMonRuntimeReady(ready),
        "healthy compatible runtime is ready");
    ok &= Check(!IsPresentMonRuntimeReady({ true, true, true, true, false }),
        "incompatible runtime is not ready");
    ok &= Check(!IsPresentMonRuntimeReady({ false, true, true, true, true }),
        "missing service is not ready");
    ok &= Check(!IsPresentMonRuntimeReady({ true, false, true, true, true }),
        "missing registry path is not ready");

    ok &= Check(ClassifyPresentMonRuntimeMsiExit(0) ==
        PresentMonRuntimeMsiExit::SuccessCandidate,
        "zero MSI exit is a success candidate");
    ok &= Check(ClassifyPresentMonRuntimeMsiExit(3010) ==
        PresentMonRuntimeMsiExit::RebootRequiredCandidate,
        "3010 MSI exit is a reboot-required candidate");
    ok &= Check(ClassifyPresentMonRuntimeMsiExit(1641) ==
        PresentMonRuntimeMsiExit::Failed,
        "unexpected reboot MSI exit is not accepted");
    ok &= Check(ClassifyPresentMonRuntimeMsiExit(1603) ==
        PresentMonRuntimeMsiExit::Failed,
        "other MSI failures are rejected");
    return ok ? 0 : 1;
}
