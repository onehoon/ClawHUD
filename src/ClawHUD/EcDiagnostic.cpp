#include "EcDiagnostic.h"

#include "EcHelperClient.h"
#include "RuntimeLogger.h"
#include "../shared/EcHelperProtocol.h"
#include "Version.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
std::wstring Now(bool fileName = false)
{
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::tm local{};
    localtime_s(&local, &time); std::wstringstream out;
    out << std::put_time(&local, fileName ? L"%Y%m%d-%H%M%S" : L"%Y-%m-%d %H:%M:%S"); return out.str();
}
std::string Narrow(const std::wstring& text)
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::string Hex(const std::vector<std::uint8_t>& bytes)
{
    std::ostringstream out; out << std::hex << std::uppercase << std::setfill('0');
    for (const auto byte : bytes) out << std::setw(2) << static_cast<unsigned>(byte) << ' ';
    return out.str();
}

const char* Stage(clawhud::ec::EcFailureStage stage)
{
    using clawhud::ec::EcFailureStage;
    switch (stage)
    {
    case EcFailureStage::None: return "None";
    case EcFailureStage::CoInitialize: return "CoInitialize";
    case EcFailureStage::ConnectWmi: return "ConnectWmi";
    case EcFailureStage::GetClass: return "GetClass";
    case EcFailureStage::GetMethod: return "GetMethod";
    case EcFailureStage::SpawnInput: return "SpawnInput";
    case EcFailureStage::GetInputData: return "GetInputData";
    case EcFailureStage::GetWmiFallback: return "GetWmiFallback";
    case EcFailureStage::CreateSafeArray: return "CreateSafeArray";
    case EcFailureStage::AccessSafeArray: return "AccessSafeArray";
    case EcFailureStage::PutBytes: return "PutBytes";
    case EcFailureStage::PutInputData: return "PutInputData";
    case EcFailureStage::ExecMethod: return "ExecMethod";
    case EcFailureStage::GetOutputData: return "GetOutputData";
    case EcFailureStage::GetOutputBytes: return "GetOutputBytes";
    case EcFailureStage::InvalidResponse: return "InvalidResponse";
    case EcFailureStage::InvalidSuccessFlag: return "InvalidSuccessFlag";
    case EcFailureStage::HelperNotElevated: return "HelperNotElevated";
    case EcFailureStage::Pipe: return "Pipe";
    case EcFailureStage::HelperLaunch: return "HelperLaunch";
    case EcFailureStage::HelperMissing: return "HelperMissing";
    }
    return "Unknown";
}

std::optional<int> DecodeFanRpm(std::uint8_t first, std::uint8_t second)
{
    const int delta = static_cast<int>(first) - static_cast<int>(second);
    if (!delta) return std::nullopt;
    return static_cast<int>(std::abs(480000.0 / delta));
}

bool Elevated()
{
    HANDLE token{}; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION value{}; DWORD size{}; const bool result = GetTokenInformation(token, TokenElevation, &value, sizeof(value), &size) != FALSE;
    CloseHandle(token); return result && value.TokenIsElevated != 0;
}

}

bool EcDiagnostic::Start()
{
    if (running_.exchange(true)) return false;
    if (worker_.joinable()) worker_.join();
    stop_ = false;
    try { worker_ = std::thread(&EcDiagnostic::Run, this); }
    catch (...) { running_ = false; throw; }
    Status(L"Running"); return true;
}

void EcDiagnostic::Stop()
{
    stop_ = true; if (worker_.joinable()) worker_.join(); running_ = false;
}

void EcDiagnostic::Status(const wchar_t* text) const
{
    if (!notifyWindow_) return;
    auto* value = new std::wstring(text);
    if (!PostMessageW(notifyWindow_, kEcDiagnosticStatus, reinterpret_cast<WPARAM>(value), 0)) delete value;
}

void EcDiagnostic::Run()
{
    std::filesystem::path path;
    try { path = clawhud::LogDirectory() / (L"ec-" + Now(true) + L".txt"); }
    catch (...) { Status(L"Failed"); running_ = false; return; }
    std::ofstream log(path, std::ios::binary);
    if (!log.is_open()) { Status(L"Failed"); running_ = false; return; }
    log << "=== CLAWHUD MSI EC DIAGNOSTIC ===\nTimestamp: " << Narrow(Now())
        << "\nClawHUD Version: " << Narrow(CLAWHUD_VERSION) << "\nWindows Build: Unavailable\nProcess Elevated: "
        << (Elevated() ? "YES" : "NO") << "\nBoard: Unavailable\nBIOS: Unavailable\n\n";
    log << "Read-only methods: Get_Temperature, Get_Fan, Get_Data\n";
    EcHelperClient reader(false);
    log << "Main Process Elevated: " << (Elevated() ? "YES" : "NO") << "\n";
    for (int sample = 1; sample <= 10 && !stop_; ++sample)
    {
        log << "\n=== SAMPLE " << sample << " / 10 ===\nTime: " << Narrow(Now()) << "\n";
        std::vector<std::uint8_t> value;
        const auto read = [&](const char* name, bool ok)
        {
            log << name << "\nStatus: " << (ok ? "OK" : "FAILED") << "\nRaw: " << Hex(value) << "\n";
            if (!ok) log << "Stage: " << Stage(reader.LastStage()) << "\nHRESULT: 0x" << std::hex << static_cast<unsigned long>(reader.LastError()) << std::dec << "\n";
        };
        bool ok = reader.ReadTemperature(value); read("Get_Temperature(0)", ok);
        if (ok && value.size() >= 2) log << "CPU Temp: " << static_cast<unsigned>(value[0]) << " C\nGPU Temp: " << static_cast<unsigned>(value[1]) << " C\n";
        ok = reader.ReadFan(value);
        if (ok && value.size() < 4)
        {
            log << "Get_Fan(0)\nStatus: FAILED\nRaw: " << Hex(value)
                << "\nStage: InvalidResponse\nReason: Fan payload shorter than 4 bytes\n";
            ok = false;
        }
        else read("Get_Fan(0)", ok);
        if (ok && value.size() >= 4)
        {
            const auto fan1 = DecodeFanRpm(value[0], value[1]); const auto fan2 = DecodeFanRpm(value[2], value[3]);
            log << "Fan1 RPM: " << (fan1 ? std::to_string(*fan1) : "Unavailable")
                << "\nFan2 RPM: " << (fan2 ? std::to_string(*fan2) : "Unavailable")
                << "\nFan2 mapping: bytes 2/3 are a candidate; hardware validation remains pending.\n";
        }
        const auto data = [&](std::uint8_t selector, const char* name)
        {
            const bool result = reader.ReadData(selector, value); read(name, result); return result;
        };
        if (data(221, "Get_Data(221)") && !value.empty()) log << "CPU Package Power: " << static_cast<unsigned>(value[0]) << " W\n";
        const bool c0 = data(70, "Get_Data(70)"); const auto b0 = value;
        const bool c1 = data(71, "Get_Data(71)"); const auto b1 = value;
        const bool v0 = data(74, "Get_Data(74)"); const auto b2 = value;
        const bool v1 = data(75, "Get_Data(75)"); const auto b3 = value;
        if (c0 && c1 && v0 && v1 && !b0.empty() && !b1.empty() && !b2.empty() && !b3.empty())
        {
            log << "Battery Current Raw: " << static_cast<unsigned>(b0[0]) << ' ' << static_cast<unsigned>(b1[0])
                << "\nBattery Voltage Raw: " << static_cast<unsigned>(b2[0]) << ' ' << static_cast<unsigned>(b3[0])
                << "\nSystem Power: Not decoded (sign/scaling requires hardware validation)\n";
        }
        log.flush(); if (sample < 10) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    log << "\nHelper: " << (reader.Connected() ? "Connected" : "Unavailable")
        << "\nHelper Elevated: " << (reader.HelperElevatedVerified() ? "YES" : "NO")
        << " (verified in helper before WMI)\nHelper PID: " << reader.HelperPid()
        << "\n";
    reader.Close(); log << "Helper: released\n"; log.flush(); Status(stop_ ? L"Cancelled" : L"Completed"); running_ = false;
}
