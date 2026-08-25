#include "EcDiagnostic.h"

#include "MsiEcReader.h"

#include <shellapi.h>

#include <chrono>
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

bool Elevated()
{
    HANDLE token{}; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION value{}; DWORD size{}; const bool result = GetTokenInformation(token, TokenElevation, &value, sizeof(value), &size) != FALSE;
    CloseHandle(token); return result && value.TokenIsElevated != 0;
}

std::filesystem::path Folder()
{
    const auto path = std::filesystem::current_path() / L"logs" / L"diagnostics";
    std::filesystem::create_directories(path); return path;
}
}

bool EcDiagnostic::Start()
{
    if (worker_.joinable()) return false;
    stop_ = false; worker_ = std::thread(&EcDiagnostic::Run, this); Status(L"Running"); return true;
}

void EcDiagnostic::Stop()
{
    stop_ = true; if (worker_.joinable()) worker_.join();
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
    try { path = Folder() / (L"ec-" + Now(true) + L".txt"); }
    catch (...) { Status(L"Failed"); return; }
    std::ofstream log(path, std::ios::binary);
    if (!log.is_open()) { Status(L"Failed"); return; }
    log << "=== CLAWHUD MSI EC DIAGNOSTIC ===\nTimestamp: " << Narrow(Now())
        << "\nClawHUD Version: 0.1.0\nWindows Build: Unavailable\nProcess Elevated: "
        << (Elevated() ? "YES" : "NO") << "\nBoard: Unavailable\nBIOS: Unavailable\n\n";
    log << "Read-only methods: Get_Temperature, Get_Fan, Get_Data\n";
    MsiEcReader reader;
    if (!reader.Initialize())
    {
        log << "WMI Connection: FAILED\nHRESULT: 0x" << std::hex << static_cast<unsigned long>(reader.LastError()) << "\n";
        Status(L"Failed"); return;
    }
    log << "WMI Connection: OK\n";
    for (int sample = 1; sample <= 10 && !stop_; ++sample)
    {
        log << "\n=== SAMPLE " << sample << " / 10 ===\nTime: " << Narrow(Now()) << "\n";
        std::vector<std::uint8_t> value;
        const auto read = [&](const char* name, bool ok)
        {
            log << name << "\nStatus: " << (ok ? "OK" : "FAILED") << "\nRaw: " << Hex(value) << "\n";
            if (!ok) log << "HRESULT: 0x" << std::hex << static_cast<unsigned long>(reader.LastError()) << std::dec << "\n";
        };
        bool ok = reader.ReadTemperature(value); read("Get_Temperature(0)", ok);
        if (ok && value.size() >= 2) log << "CPU Temp: " << static_cast<unsigned>(value[0]) << " C\nGPU Temp: " << static_cast<unsigned>(value[1]) << " C\n";
        ok = reader.ReadFan(value); read("Get_Fan(0)", ok);
        if (ok && value.size() >= 4)
        {
            log << "Fan1 RPM: " << DecodeFanRpm(value[0], value[1]).value_or(0)
                << "\nFan2 RPM: " << DecodeFanRpm(value[2], value[3]).value_or(0)
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
    reader.Shutdown(); log << "\nWMI Connection: released\n"; log.flush(); Status(stop_ ? L"Cancelled" : L"Completed");
}

void EcDiagnostic::OpenLogFolder() const
{
    try { const auto path = Folder(); ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
    catch (...) {}
}
