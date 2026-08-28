#include "RuntimeLogger.h"

#include <windows.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::filesystem::path TempDirectory()
{
    wchar_t path[MAX_PATH]{};
    GetTempPathW(ARRAYSIZE(path), path);
    const auto result = std::filesystem::path(path) /
        (L"ClawHUD.RuntimeLoggerTests." + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(result);
    std::filesystem::create_directories(result);
    return result;
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

void TestAppendAndRotation(const std::filesystem::path& directory)
{
    const auto log = directory / L"clawhud.log";
    { std::ofstream output(log); output << "old\n"; }
    clawhud::RuntimeLogger::SetDirectoryForTests(directory.wstring());
    assert(clawhud::LogDirectory() == directory);
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, L"appended");
    assert(Read(log).find("old") != std::string::npos);

    { std::ofstream output(log, std::ios::binary | std::ios::trunc);
      output << std::string(2 * 1024 * 1024, 'x'); }
    { std::ofstream output(directory / L"clawhud.1.log"); output << "one"; }
    { std::ofstream output(directory / L"clawhud.2.log"); output << "two"; }
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, L"rotated");
    assert(Read(directory / L"clawhud.1.log").find('x') != std::string::npos);
    assert(Read(directory / L"clawhud.2.log") == "one");
    assert(Read(log).find("rotated") != std::string::npos);
}

void TestFailureIsFailClosed(const std::filesystem::path& directory)
{
    const auto invalid = directory / L"not-a-directory";
    { std::ofstream output(invalid); output << "file"; }
    clawhud::RuntimeLogger::SetDirectoryForTests((invalid / L"logs").wstring());
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error, L"ignored");

    bool threw{};
    try { (void)clawhud::LogDirectory(); }
    catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}

void TestConcurrentWrites(const std::filesystem::path& directory)
{
    std::filesystem::remove(directory / L"clawhud.log");
    std::filesystem::remove(directory / L"clawhud.1.log");
    std::filesystem::remove(directory / L"clawhud.2.log");
    clawhud::RuntimeLogger::SetDirectoryForTests(directory.wstring());
    std::vector<std::thread> workers;
    for (int i = 0; i != 8; ++i)
        workers.emplace_back([i] { for (int j = 0; j != 100; ++j)
            clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info,
                L"thread=" + std::to_wstring(i) + L" item=" + std::to_wstring(j)); });
    for (auto& worker : workers) worker.join();
    std::ifstream input(directory / L"clawhud.log");
    std::string line;
    std::size_t count{};
    while (std::getline(input, line))
    {
        assert(line.find("[INFO] thread=") != std::string::npos);
        ++count;
    }
    assert(count == 800);
}

void TestDebugFiltering(const std::filesystem::path& directory)
{
    const auto log = directory / L"clawhud.log";
    std::filesystem::remove(log);
    clawhud::RuntimeLogger::SetDebugLogging(false);
    clawhud::RuntimeLogger::SetDirectoryForTests(directory.wstring());
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug, L"hidden");
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, L"info-off");
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn, L"warn-off");
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Error, L"error-off");
    auto content = Read(log);
    assert(content.find("hidden") == std::string::npos);
    assert(content.find("info-off") != std::string::npos);
    assert(content.find("warn-off") != std::string::npos);
    assert(content.find("error-off") != std::string::npos);

    clawhud::RuntimeLogger::SetDebugLogging(true);
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug, L"visible");
    content = Read(log);
    assert(content.find("visible") != std::string::npos);
    clawhud::RuntimeLogger::SetDebugLogging(false);
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug, L"hidden-again");
    assert(Read(log).find("hidden-again") == std::string::npos);
}

void TestRotationFailureIsBounded(const std::filesystem::path& directory)
{
    const auto log = directory / L"clawhud.log";
    { std::ofstream output(log, std::ios::binary | std::ios::trunc);
      output << std::string(2 * 1024 * 1024, 'x'); }
    clawhud::RuntimeLogger::SetDirectoryForTests(directory.wstring());
    clawhud::RuntimeLogger::SetRotationFailureForTests(true);
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, L"bounded");
    clawhud::RuntimeLogger::SetRotationFailureForTests(false);
    assert(std::filesystem::file_size(log) < 4096);
    assert(Read(log).find("bounded") != std::string::npos);
}
}

int main()
{
    const auto directory = TempDirectory();
    TestAppendAndRotation(directory);
    TestFailureIsFailClosed(directory);
    TestConcurrentWrites(directory);
    TestDebugFiltering(directory);
    TestRotationFailureIsBounded(directory);
    clawhud::RuntimeLogger::ResetForTests();
    std::filesystem::remove_all(directory);
}
