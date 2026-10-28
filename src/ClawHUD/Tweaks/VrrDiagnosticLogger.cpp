#include "VrrDiagnosticLogger.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

namespace clawhud
{
namespace
{
std::mutex g_mutex;
std::filesystem::path g_testDirectory;
std::string g_runId;
std::size_t g_maximumBytes = 2u * 1024u * 1024u;
unsigned g_generations = 3;
bool g_forceRotationFailure{};
LONG g_runSequence{};

std::wstring LogsDirectory()
{
    if (!g_testDirectory.empty())
        return g_testDirectory.wstring();

    PWSTR knownFolder{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
            nullptr, &knownFolder)))
        return {};
    std::wstring directory(knownFolder);
    CoTaskMemFree(knownFolder);
    directory += L"\\ClawHUD";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return {};
    directory += L"\\logs";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return {};
    return directory;
}

std::string Utf8(std::wstring_view value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) != size)
        return {};
    return result;
}

std::string Timestamp()
{
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char value[32]{};
    sprintf_s(value, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds);
    return value;
}

bool Rotate(const std::filesystem::path& current)
{
    if (g_forceRotationFailure || g_generations < 2)
        return false;
    for (unsigned generation = g_generations - 1; generation > 1; --generation)
    {
        const auto source = current.wstring() + L"." + std::to_wstring(generation - 1);
        const auto destination = current.wstring() + L"." + std::to_wstring(generation);
        if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;
        if (!MoveFileExW(source.c_str(), destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return false;
    }

    const auto first = current.wstring() + L".1";
    if (GetFileAttributesW(first.c_str()) != INVALID_FILE_ATTRIBUTES &&
        !MoveFileExW(first.c_str(), (current.wstring() + L".2").c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return false;
    if (GetFileAttributesW(current.c_str()) != INVALID_FILE_ATTRIBUTES &&
        !MoveFileExW(current.c_str(), first.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return false;
    return true;
}

void AppendLocked(std::string_view category, std::string_view message) noexcept
{
    try
    {
        auto directory = LogsDirectory();
        if (directory.empty()) return;
        if (!g_testDirectory.empty())
        {
            std::error_code error;
            std::filesystem::create_directories(g_testDirectory, error);
            if (error) return;
        }

        const std::filesystem::path path =
            std::filesystem::path(directory) / L"tweaks-vrr.log";
        const std::string line = Timestamp() + " [" +
            (g_runId.empty() ? std::string("standalone") : g_runId) + "] [" +
            std::string(category) + "] " + std::string(message) + "\r\n";
        if (line.size() > g_maximumBytes) return;

        std::error_code error;
        const auto currentSize = std::filesystem::exists(path, error) && !error
            ? std::filesystem::file_size(path, error) : 0;
        if (error) return;
        if (currentSize > g_maximumBytes ||
            line.size() > g_maximumBytes - static_cast<std::size_t>(currentSize))
        {
            if (currentSize > g_maximumBytes)
            {
                // Recover from an oversized file left by an older build or external writer.
                std::ofstream reset(path, std::ios::binary | std::ios::trunc);
                if (!reset) return;
            }
            else if (!Rotate(path))
                return;
        }
        if (line.size() > g_maximumBytes)
            return;

        std::ofstream output(path, std::ios::binary | std::ios::app);
        if (!output) return;
        output.write(line.data(), static_cast<std::streamsize>(line.size()));
    }
    catch (...) { }
}
}

std::string VrrDiagnosticLogger::BeginRun(bool enabled,
    std::string_view appVersion, std::string_view candidateCount)
{
    std::lock_guard lock(g_mutex);
    SYSTEMTIME time{};
    GetSystemTime(&time);
    const LONG sequence = InterlockedIncrement(&g_runSequence);
    char id[48]{};
    sprintf_s(id, "%04u%02u%02u-%02u%02u%02u-%ld", time.wYear,
        time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, sequence);
    g_runId = id;
    AppendLocked("VRR", "RunStart enabled=" + std::string(enabled ? "1" : "0") +
        " appVersion=" + std::string(appVersion) + " candidates=" +
        std::string(candidateCount));
    return g_runId;
}

void VrrDiagnosticLogger::Write(std::string_view category,
    std::string_view message) noexcept
{
    try
    {
        std::lock_guard lock(g_mutex);
        AppendLocked(category, message);
    }
    catch (...) { }
}

void VrrDiagnosticLogger::WriteWide(std::string_view category,
    std::wstring_view message) noexcept
{
    try
    {
        const auto encoded = Utf8(message);
        Write(category, encoded.empty() && !message.empty()
            ? std::string_view("message UTF-8 conversion failed")
            : std::string_view(encoded));
    }
    catch (...) { }
}

void VrrDiagnosticLogger::EndRun(std::string_view message) noexcept
{
    Write("VRR", std::string("RunEnd ") + std::string(message));
}

#ifdef CLAWHUD_VRR_DIAGNOSTIC_LOGGER_TESTS
void VrrDiagnosticLogger::SetDirectoryForTests(const std::wstring& directory) noexcept
{
    try
    {
        std::lock_guard lock(g_mutex);
        g_testDirectory = directory;
        g_runId.clear();
    }
    catch (...) { }
}

void VrrDiagnosticLogger::SetLimitsForTests(std::size_t maximumBytes,
    unsigned generations) noexcept
{
    try
    {
        std::lock_guard lock(g_mutex);
        g_maximumBytes = maximumBytes;
        g_generations = generations;
    }
    catch (...) { }
}

void VrrDiagnosticLogger::SetRotationFailureForTests(bool enabled) noexcept
{
    try
    {
        std::lock_guard lock(g_mutex);
        g_forceRotationFailure = enabled;
    }
    catch (...) { }
}

void VrrDiagnosticLogger::ResetForTests() noexcept
{
    try
    {
        std::lock_guard lock(g_mutex);
        g_testDirectory.clear();
        g_runId.clear();
        g_maximumBytes = 2u * 1024u * 1024u;
        g_generations = 3;
        g_forceRotationFailure = false;
    }
    catch (...) { }
}
#endif
}
