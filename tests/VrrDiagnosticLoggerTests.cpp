#include "Tweaks/VrrDiagnosticLogger.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace clawhud;
namespace
{
bool Check(bool value, const char* message)
{
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}
}

int main()
{
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    const std::filesystem::path directory =
        std::filesystem::path(tempPath) / L"ClawHUD-VrrDiagnosticLoggerTests";
    std::error_code error;
    std::filesystem::remove_all(directory, error);

    bool ok = true;
    VrrDiagnosticLogger::SetDirectoryForTests(directory.wstring());
    const auto firstRun = VrrDiagnosticLogger::BeginRun(true, "1.0.0", "2");
    VrrDiagnosticLogger::Write("IGCL", "attempt=1 WMI candidates=2");
    VrrDiagnosticLogger::WriteWide("DRR", L"\uBAA8\uB2C8\uD130=LG ULTRAGEAR 48-180 Hz");
    VrrDiagnosticLogger::EndRun("DRR=Applied Intel=AlreadyCorrect attempts=1");

    const auto logPath = directory / L"tweaks-vrr.log";
    const auto firstLog = Read(logPath);
    ok &= Check(!firstRun.empty() && firstLog.find("RunStart enabled=1 appVersion=1.0.0 candidates=2") != std::string::npos,
        "RunStart includes enabled state, app version and candidate count");
    ok &= Check(firstLog.find("[" + firstRun + "] [IGCL]") != std::string::npos &&
        firstLog.find("[" + firstRun + "] [DRR]") != std::string::npos &&
        firstLog.find("[" + firstRun + "] [VRR] RunEnd") != std::string::npos,
        "DRR, IGCL and RunEnd share one run ID");
    ok &= Check(firstLog.find("\xEB\xAA\xA8\xEB\x8B\x88\xED\x84\xB0=LG ULTRAGEAR 48-180 Hz") !=
        std::string::npos,
        "wide diagnostic strings are written as UTF-8");
    ok &= Check(!std::filesystem::exists(directory / L"clawhud.log"),
        "VRR diagnostic logger creates no general runtime log");

    const auto secondRun = VrrDiagnosticLogger::BeginRun(false, "1.0.0", "0");
    VrrDiagnosticLogger::EndRun("DRR=Disabled Intel=Disabled attempts=1");
    const auto appended = Read(logPath);
    ok &= Check(secondRun != firstRun && appended.find("[" + firstRun + "]") != std::string::npos &&
        appended.find("[" + secondRun + "]") != std::string::npos,
        "subsequent runs append with a distinct run ID");

    VrrDiagnosticLogger::SetLimitsForTests(220, 3);
    for (int index = 0; index < 8; ++index)
        VrrDiagnosticLogger::Write("IGCL", "rotation-record-" +
            std::to_string(index) + std::string(56, 'x'));
    for (const auto& path : { logPath, std::filesystem::path(logPath.wstring() + L".1"),
             std::filesystem::path(logPath.wstring() + L".2") })
        if (std::filesystem::exists(path))
            ok &= Check(std::filesystem::file_size(path) <= 220,
                "rotated log generations remain within the configured size limit");
    ok &= Check(std::filesystem::exists(logPath.wstring() + L".1") &&
        std::filesystem::exists(logPath.wstring() + L".2") &&
        !std::filesystem::exists(logPath.wstring() + L".3"),
        "log rotates across three bounded generations");

    VrrDiagnosticLogger::SetRotationFailureForTests(true);
    const auto sizeBeforeFailure = std::filesystem::file_size(logPath);
    VrrDiagnosticLogger::Write("IGCL", std::string(180, 'y'));
    ok &= Check(std::filesystem::file_size(logPath) == sizeBeforeFailure,
        "rotation failure drops only the diagnostic write without growing the active file");
    VrrDiagnosticLogger::SetRotationFailureForTests(false);

    {
        std::ofstream oversized(logPath, std::ios::binary | std::ios::trunc);
        oversized << std::string(500, 'z');
    }
    VrrDiagnosticLogger::Write("IGCL", "recovered-after-oversized-log");
    ok &= Check(std::filesystem::file_size(logPath) <= 220 &&
        Read(logPath).find("recovered-after-oversized-log") != std::string::npos,
        "an oversized pre-existing log is reset so logging recovers within its bound");

    const auto blockedPath = directory / L"blocked-parent";
    std::ofstream blocked(blockedPath, std::ios::binary);
    blocked << "file, not directory";
    blocked.close();
    VrrDiagnosticLogger::SetDirectoryForTests((blockedPath / L"logs").wstring());
    VrrDiagnosticLogger::Write("DRR", "write failure must not escape");

    VrrDiagnosticLogger::ResetForTests();
    std::filesystem::remove_all(directory, error);
    return ok ? 0 : 1;
}
