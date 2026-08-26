#include "SupportedHardware.h"

#include <iostream>

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition)
        return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool ok = true;
    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T42") == HardwareSupport::Supported, "MS-1T42 supported");
    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T52") == HardwareSupport::Supported, "MS-1T52 supported");
    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T91") == HardwareSupport::Supported, "MS-1T91 supported");
    ok &= Check(ClassifyBaseBoardProduct(L"ms-1t42") == HardwareSupport::Supported, "case-insensitive match");
    ok &= Check(ClassifyBaseBoardProduct(L" MS-1T91 ") == HardwareSupport::Supported, "trimmed match");
    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T41") == HardwareSupport::Unsupported, "MS-1T41 unsupported");
    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T92") == HardwareSupport::Unsupported, "MS-1T92 unsupported");
    ok &= Check(ClassifyBaseBoardProduct(L"OTHER-BOARD") == HardwareSupport::Unsupported, "other board unsupported");
    ok &= Check(ClassifyBaseBoardProduct(L"MS-1T420") == HardwareSupport::Unsupported, "fuzzy suffix rejected");
    ok &= Check(ClassifyBaseBoardProduct(L"") == HardwareSupport::Indeterminate, "empty product indeterminate");
    ok &= Check(ClassifyBaseBoardProduct(L"   \t\r\n") == HardwareSupport::Indeterminate, "blank product indeterminate");
    return ok ? 0 : 1;
}
