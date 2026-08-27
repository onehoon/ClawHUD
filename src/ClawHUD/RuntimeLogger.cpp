#include "RuntimeLogger.h"

#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace clawhud
{
namespace
{
constexpr std::uintmax_t kMaximumLogBytes = 2u * 1024u * 1024u;
std::mutex g_mutex;
std::filesystem::path g_directory;
bool g_initialized{};
bool g_fileEnabled{};

#ifdef CLAWHUD_RUNTIME_LOGGER_TESTS
std::filesystem::path g_testDirectory;
#endif

const wchar_t* LevelName(RuntimeLogLevel level) noexcept
{
    switch (level)
    {
    case RuntimeLogLevel::Warn: return L"WARN";
    case RuntimeLogLevel::Error: return L"ERROR";
    default: return L"INFO";
    }
}

std::string Utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!bytes) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), bytes, nullptr, nullptr);
    return result;
}

std::wstring Timestamp()
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
        now.wSecond, now.wMilliseconds);
    return buffer;
}

void RotateIfNeeded(const std::filesystem::path& file)
{
    std::error_code error;
    if (!std::filesystem::exists(file, error) || error ||
        std::filesystem::file_size(file, error) < kMaximumLogBytes || error)
        return;

    const auto one = file.parent_path() / L"clawhud.1.log";
    const auto two = file.parent_path() / L"clawhud.2.log";
    std::filesystem::remove(two, error);
    error.clear();
    std::filesystem::rename(one, two, error);
    error.clear();
    std::filesystem::rename(file, one, error);
}

void InitializeLocked()
{
    if (g_initialized) return;
    g_initialized = true;
#ifdef CLAWHUD_RUNTIME_LOGGER_TESTS
    if (!g_testDirectory.empty())
        g_directory = g_testDirectory;
    else
#endif
    {
        PWSTR localAppData{};
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
            nullptr, &localAppData)))
        {
            g_directory = std::filesystem::path(localAppData) / L"ClawHUD" / L"logs";
            CoTaskMemFree(localAppData);
        }
    }
    if (g_directory.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(g_directory, error);
    g_fileEnabled = !error;
}
}

std::filesystem::path LogDirectory()
{
    std::lock_guard lock(g_mutex);
    InitializeLocked();
    return g_directory;
}

void RuntimeLogger::Initialize() noexcept
{
    try
    {
        std::lock_guard lock(g_mutex);
        InitializeLocked();
    }
    catch (...) { OutputDebugStringW(L"[ClawHUD] [WARN] Runtime logging initialization failed\n"); }
}

void RuntimeLogger::Shutdown() noexcept
{
    try
    {
        std::lock_guard lock(g_mutex);
        g_fileEnabled = false;
        g_initialized = false;
        g_directory.clear();
    }
    catch (...) { OutputDebugStringW(L"[ClawHUD] [WARN] Runtime logging shutdown failed\n"); }
}

void RuntimeLogger::Log(RuntimeLogLevel level, const std::wstring& message) noexcept
{
    try
    {
        const std::wstring line = std::wstring(L"[") + LevelName(level) + L"] " + message;
        OutputDebugStringW((L"[ClawHUD] " + line + L"\n").c_str());

        std::lock_guard lock(g_mutex);
        InitializeLocked();
        if (!g_fileEnabled) return;
        const auto file = g_directory / L"clawhud.log";
        RotateIfNeeded(file);
        std::ofstream output(file, std::ios::binary | std::ios::app);
        if (!output) return;
        output << Utf8(Timestamp() + L" " + line) << "\r\n";
    }
    catch (...)
    {
        OutputDebugStringW(L"[ClawHUD] [WARN] Runtime logging failed\n");
    }
}

#ifdef CLAWHUD_RUNTIME_LOGGER_TESTS
void RuntimeLogger::SetDirectoryForTests(const std::wstring& directory) noexcept
{
    try
    {
        std::lock_guard lock(g_mutex);
        g_testDirectory = directory;
        g_initialized = false;
        g_fileEnabled = false;
        g_directory.clear();
    }
    catch (...) { OutputDebugStringW(L"[ClawHUD] [WARN] Runtime logger test setup failed\n"); }
}

void RuntimeLogger::ResetForTests() noexcept
{
    try
    {
        std::lock_guard lock(g_mutex);
        g_testDirectory.clear();
        g_initialized = false;
        g_fileEnabled = false;
        g_directory.clear();
    }
    catch (...) { OutputDebugStringW(L"[ClawHUD] [WARN] Runtime logger test reset failed\n"); }
}
#endif
}
