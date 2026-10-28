#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace clawhud
{
class VrrDiagnosticLogger
{
public:
    static std::string BeginRun(bool enabled, std::string_view appVersion,
        std::string_view candidateCount);
    static void Write(std::string_view category, std::string_view message) noexcept;
    static void WriteWide(std::string_view category, std::wstring_view message) noexcept;
    static void EndRun(std::string_view message) noexcept;

#ifdef CLAWHUD_VRR_DIAGNOSTIC_LOGGER_TESTS
    static void SetDirectoryForTests(const std::wstring& directory) noexcept;
    static void SetLimitsForTests(std::size_t maximumBytes,
        unsigned generations) noexcept;
    static void SetRotationFailureForTests(bool enabled) noexcept;
    static void ResetForTests() noexcept;
#endif
};
}
