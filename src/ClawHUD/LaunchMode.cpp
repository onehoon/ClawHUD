#include "LaunchMode.h"

namespace clawhud
{
LaunchMode ResolveLaunchMode(std::span<const std::wstring_view> arguments) noexcept
{
    for (const auto argument : arguments)
    {
        if (argument == L"--managed")
            return LaunchMode::Managed;
    }
    return LaunchMode::Standalone;
}
}
