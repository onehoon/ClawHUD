#include "EcHelperClient.h"
#include "RuntimeLogger.h"

#include <windows.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
std::filesystem::path ReadableTempDirectory()
{
    wchar_t path[MAX_PATH]{};
    GetTempPathW(ARRAYSIZE(path), path);
    const auto directory = std::filesystem::path(path) /
        (L"ClawHUD.EcHelperClientRuntimeLoggingTests." +
            std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
}

int main()
{
    const auto directory = ReadableTempDirectory();
    clawhud::RuntimeLogger::SetDirectoryForTests(directory.wstring());

    // Diagnostic ownership must not write normal runtime events.
    EcHelperClient diagnosticReader(false);
    std::vector<std::uint8_t> payload;
    diagnosticReader.ReadTemperature(payload);
    diagnosticReader.ReadTemperature(payload);
    assert(!std::filesystem::exists(directory / L"clawhud.log"));

    // A persistent launch failure is one event, not one event per sample.
    EcHelperClient runtimeReader(true);
    runtimeReader.ReadTemperature(payload);
    runtimeReader.ReadTemperature(payload);
    runtimeReader.ReadTemperature(payload);
    const auto log = Read(directory / L"clawhud.log");
    const std::string event = "EC Helper launch requested";
    std::size_t count{};
    for (std::size_t position = log.find(event); position != std::string::npos;
        position = log.find(event, position + event.size()))
        ++count;
    assert(count == 1);

    clawhud::RuntimeLogger::ResetForTests();
    std::filesystem::remove_all(directory);
}
