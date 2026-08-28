#pragma once

#include <string>
#include <filesystem>

namespace clawhud
{
std::filesystem::path LogDirectory();

enum class RuntimeLogLevel
{
    Debug,
    Info,
    Warn,
    Error
};

class RuntimeLogger
{
public:
    static void Initialize() noexcept;
    static void Shutdown() noexcept;
    static void SetDebugLogging(bool enabled) noexcept;
    static void Log(RuntimeLogLevel level, const std::wstring& message) noexcept;

#ifdef CLAWHUD_RUNTIME_LOGGER_TESTS
    static void SetDirectoryForTests(const std::wstring& directory) noexcept;
    static void SetRotationFailureForTests(bool enabled) noexcept;
    static void ResetForTests() noexcept;
#endif
};
}
