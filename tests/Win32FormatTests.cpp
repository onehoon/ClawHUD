#include "Win32Format.h"

#include <cwctype>
#include <iostream>
#include <string>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

std::wstring ToUpper(std::wstring value)
{
    for (auto& ch : value)
        ch = static_cast<wchar_t>(towupper(ch));
    return value;
}

void HexHresultFormat(bool& ok)
{
    ok &= Check(HexHresult(S_OK) == L"0x00000000", "S_OK renders as 0x00000000");
    ok &= Check(HexHresult(static_cast<HRESULT>(0x80004005)) == L"0x80004005",
        "E_FAIL renders with all eight nibbles");
    ok &= Check(HexHresult(static_cast<HRESULT>(1)) == L"0x00000001",
        "small values stay zero-padded to eight digits");
    ok &= Check(HexHresult(static_cast<HRESULT>(0xABCDEF12)) == L"0x" + ToUpper(L"abcdef12"),
        "hex digits are uppercase");
    ok &= Check(HexHresult(S_OK).size() == 10, "output is always 0x + 8 digits");
}

void HwndTextFormat(bool& ok)
{
    const std::wstring nullText = HwndText(nullptr);
    ok &= Check(nullText.rfind(L"0x", 0) == 0, "HwndText is 0x-prefixed");
    ok &= Check(nullText.find_first_not_of(L'0', 2) == std::wstring::npos,
        "a null handle is all zero digits after 0x");

    const std::wstring text = HwndText(reinterpret_cast<HWND>(0xABCDEF));
    ok &= Check(text.rfind(L"0x", 0) == 0, "HwndText is 0x-prefixed for a real handle");
    const std::wstring digits = ToUpper(text.substr(2));
    ok &= Check(digits.find(L"ABCDEF") != std::wstring::npos,
        "the handle value appears in the rendered digits");
    ok &= Check(digits.find_first_not_of(L"0123456789ABCDEF") == std::wstring::npos,
        "only hex digits follow the 0x prefix");
}
}

int main()
{
    bool ok = true;
    HexHresultFormat(ok);
    HwndTextFormat(ok);
    return ok ? 0 : 1;
}
