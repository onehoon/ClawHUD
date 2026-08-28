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

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
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
    Require(content.find("hidden") == std::string::npos,
        "Debug entry was written while debug logging was disabled");
    Require(content.find("info-off") != std::string::npos, "Info entry missing");
    Require(content.find("warn-off") != std::string::npos, "Warn entry missing");
    Require(content.find("error-off") != std::string::npos, "Error entry missing");

    clawhud::RuntimeLogger::SetDebugLogging(true);
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug, L"visible");
    content = Read(log);
    Require(content.find("visible") != std::string::npos,
        "Debug entry was not written after enabling debug logging");
    clawhud::RuntimeLogger::SetDebugLogging(false);
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug, L"hidden-again");
    Require(Read(log).find("hidden-again") == std::string::npos,
        "Debug entry remained visible after disabling debug logging");
}

void TestRotationFailureIsBounded(const std::filesystem::path& directory)
{
    const auto log = directory / L"clawhud.log";
    const auto one = directory / L"clawhud.1.log";
    const auto two = directory / L"clawhud.2.log";
    { std::ofstream output(log, std::ios::binary | std::ios::trunc);
      output << std::string(2 * 1024 * 1024, 'x'); }
    { std::ofstream output(one, std::ios::binary | std::ios::trunc); output << "one"; }
    { std::ofstream output(two, std::ios::binary | std::ios::trunc); output << "two"; }
    clawhud::RuntimeLogger::SetDirectoryForTests(directory.wstring());
    clawhud::RuntimeLogger::SetRotationFailureForTests(true);
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, L"bounded");
    clawhud::RuntimeLogger::SetRotationFailureForTests(false);
    Require(std::filesystem::file_size(log) == 2u * 1024u * 1024u,
        "rotation failure allowed log growth");
    Require(Read(log).find('x') != std::string::npos,
        "rotation failure discarded the existing log");
    Require(Read(log).find("bounded") == std::string::npos,
        "rotation failure wrote the blocked entry");
    Require(Read(one) == "one", "rotation failure changed clawhud.1.log");
    Require(Read(two) == "two", "rotation failure changed clawhud.2.log");
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, L"recovered");
    Require(Read(log).find("recovered") != std::string::npos,
        "logging did not recover after transient rotation failure");
}

void TestRotationMetadataFailureIsFailClosed(const std::filesystem::path& directory)
{
    const auto log = directory / L"clawhud.log";
    { std::ofstream output(log, std::ios::binary | std::ios::trunc);
      output << std::string(2 * 1024 * 1024, 'm'); }
    clawhud::RuntimeLogger::SetDirectoryForTests(directory.wstring());
    clawhud::RuntimeLogger::SetRotationMetadataFailureForTests(true);
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, L"metadata-blocked");
    clawhud::RuntimeLogger::SetRotationMetadataFailureForTests(false);
    Require(std::filesystem::file_size(log) == 2u * 1024u * 1024u,
        "metadata failure allowed log growth");
    Require(Read(log).find("metadata-blocked") == std::string::npos,
        "metadata failure wrote the blocked entry");
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
    TestRotationMetadataFailureIsFailClosed(directory);
    clawhud::RuntimeLogger::ResetForTests();
    std::filesystem::remove_all(directory);
}
