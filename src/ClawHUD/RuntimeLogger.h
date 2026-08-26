#pragma once

#include <string>

namespace clawhud
{
enum class RuntimeLogLevel
{
    Info,
    Warn,
    Error
};

class RuntimeLogger
{
public:
    static void Initialize() noexcept;
    static void Shutdown() noexcept;
    static void Log(RuntimeLogLevel level, const std::wstring& message) noexcept;

#ifdef CLAWHUD_RUNTIME_LOGGER_TESTS
    static void SetDirectoryForTests(const std::wstring& directory) noexcept;
    static void ResetForTests() noexcept;
#endif
};
}
