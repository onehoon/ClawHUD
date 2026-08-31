#pragma once

#include <windows.h>

#include <string>

namespace clawhud
{
// "0x%08X" rendering of an HRESULT for log messages.
std::wstring HexHresult(HRESULT hr);

// "0x%p" rendering of a window handle for log messages.
std::wstring HwndText(HWND window);
}
