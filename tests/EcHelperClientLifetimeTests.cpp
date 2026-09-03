#include "EcHelperClient.h"
#include "RuntimeLogger.h"

#include <windows.h>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Cleanup 1 (work order 9.1 / 9.2): prove the *actual* helper-launch policy, not
// just the log count. Within one EcHelperClient lifetime a failed elevated
// launch stays consumed -- later reads must not fire another runas. Only an
// explicit Close() ends the lifetime and permits one new launch attempt.

namespace
{
std::filesystem::path TempDir()
{
    wchar_t path[MAX_PATH]{};
    GetTempPathW(ARRAYSIZE(path), path);
    const auto directory = std::filesystem::path(path) /
        (L"ClawHUD.EcHelperClientLifetimeTests." +
            std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}
}

int main()
{
    const auto directory = TempDir();
    clawhud::RuntimeLogger::SetDirectoryForTests(directory.wstring());

    EcHelperClient::g_forceStartHelperFailure = true;
    EcHelperClient::g_startHelperCalls = 0;

    std::vector<std::uint8_t> payload;

    {
        // A cancelled / failed elevated launch is sticky for this lifetime.
        EcHelperClient client(true);
        client.ReadTemperature(payload);
        assert(EcHelperClient::g_startHelperCalls == 1);
        assert(client.AttemptedForTests());

        // Every subsequent read across all three operations: no new launch.
        client.ReadFan(payload);
        client.ReadData(221, payload);
        client.ReadData(70, payload);
        client.ReadTemperature(payload);
        assert(EcHelperClient::g_startHelperCalls == 1);

        // Explicit lifetime release re-arms exactly one new attempt.
        client.Close();
        assert(!client.AttemptedForTests());

        client.ReadTemperature(payload);
        assert(EcHelperClient::g_startHelperCalls == 2);
        client.ReadFan(payload);
        client.ReadData(221, payload);
        assert(EcHelperClient::g_startHelperCalls == 2);
    }

    // A brand-new EcHelperClient is a brand-new lifetime: one attempt allowed.
    {
        EcHelperClient fresh(true);
        fresh.ReadTemperature(payload);
        fresh.ReadTemperature(payload);
        assert(EcHelperClient::g_startHelperCalls == 3);
    }

    clawhud::RuntimeLogger::ResetForTests();
    std::filesystem::remove_all(directory);
    return 0;
}
