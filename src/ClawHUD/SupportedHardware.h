#pragma once

#include <string_view>

enum class HardwareSupport
{
    Supported,
    Unsupported,
    Indeterminate,
};

HardwareSupport ClassifyBaseBoardProduct(std::wstring_view boardProduct);
HardwareSupport CheckSupportedHardware();
