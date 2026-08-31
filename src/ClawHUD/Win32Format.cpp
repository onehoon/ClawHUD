#include "Win32Format.h"

#include <cstdio>
#include <cwchar>

namespace clawhud
{
std::wstring HexHresult(HRESULT hr)
{
    wchar_t buffer[11]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

std::wstring HwndText(HWND window)
{
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%p", window);
    return buffer;
}
}
