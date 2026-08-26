#pragma once

#include <string_view>

namespace clawhud
{
bool IsRejectedProductionTargetImage(std::wstring_view image) noexcept;
}
