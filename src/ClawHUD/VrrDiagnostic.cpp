#include "VrrDiagnostic.h"

#include "App.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <vector>

namespace
{
constexpr auto kPhaseDuration = std::chrono::seconds(30);
constexpr auto kPocWatchdog = std::chrono::seconds(35);
constexpr auto kTargetWait = std::chrono::seconds(15);

std::wstring Now(bool fileName = false)
{
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::tm local{};
    localtime_s(&local, &time); std::wstringstream out;
    out << std::put_time(&local, fileName ? L"%Y%m%d-%H%M%S" : L"%Y-%m-%d %H:%M:%S"); return out.str();
}

std::filesystem::path LogFolder(const App& app)
{
    const auto path = std::filesystem::path(app.ExecutablePath()).parent_path() / L"logs" / L"diagnostics";
    std::filesystem::create_directories(path); return path;
}

std::optional<DWORD> ForegroundTarget()
{
    const HWND window = GetForegroundWindow(); if (!window) return std::nullopt;
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    if (!pid || pid == GetCurrentProcessId()) return std::nullopt;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!process) return std::nullopt;
    wchar_t path[MAX_PATH]{}; DWORD size = ARRAYSIZE(path);
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE; CloseHandle(process);
    if (!ok) return std::nullopt;
    std::wstring name(path, size); std::transform(name.begin(), name.end(), name.begin(), towlower);
    if (name.ends_with(L"\\explorer.exe") || name.ends_with(L"\\clawhud.exe") ||
        name.ends_with(L"\\clawhud.vrrpoc.exe")) return std::nullopt;
    return pid;
}

bool Alive(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false; DWORD code{}; const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process); return alive;
}

bool ExistingPocWindow()
{
    return FindWindowW(L"ClawHUD.VrrPoc", nullptr) != nullptr;
}

std::wstring ProcessPath(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!process) return L"Unavailable";
    wchar_t path[MAX_PATH]{}; DWORD size = ARRAYSIZE(path); QueryFullProcessImageNameW(process, 0, path, &size); CloseHandle(process);
    return size ? std::wstring(path, size) : L"Unavailable";
}
}

bool VrrDiagnostic::Start()
{
    if (running_.exchange(true)) return false;
    if (worker_.joinable()) worker_.join();
    stop_ = false;
    try { worker_ = std::thread(&VrrDiagnostic::Run, this); }
    catch (...) { running_ = false; throw; }
    Status(L"Waiting for game"); return true;
}

void VrrDiagnostic::Stop()
{
    stop_ = true; if (worker_.joinable()) worker_.join(); running_ = false;
}

void VrrDiagnostic::Status(const wchar_t* text) const
{
    if (!notifyWindow_) return; auto* value = new std::wstring(text);
    if (!PostMessageW(notifyWindow_, kVrrDiagnosticStatus, reinterpret_cast<WPARAM>(value), 0)) delete value;
}

void VrrDiagnostic::Run()
{
    std::wofstream log;
    try
    {
        const auto path = LogFolder(app_) / (L"vrr-" + Now(true) + L".txt"); log.open(path);
        if (!log.is_open()) { Status(L"Failed"); running_ = false; return; }
        log << L"=== CLAWHUD VRR DIAGNOSTIC ===\nTimestamp: " << Now()
            << L"\nRecommended Test Conditions: VRR enabled manually; 120 Hz display; game roughly 70-120 FPS\n"
            << L"VRR Analysis: Not performed\nPresentMon: Not implemented in this PR\n\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::optional<DWORD> target; const auto deadline = std::chrono::steady_clock::now() + kTargetWait;
        while (!stop_ && std::chrono::steady_clock::now() < deadline)
        {
            target = ForegroundTarget(); if (target) break; std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (stop_) { log << L"RESULT: Cancelled\n"; Status(L"Cancelled"); running_ = false; return; }
        if (!target) { log << L"VRR TEST FAILED\nReason: No valid foreground target process\n"; Status(L"Failed"); running_ = false; return; }
        if (ExistingPocWindow())
        {
            log << L"VRR TEST FAILED\nReason: ClawHUD.VrrPoc is already running; HUD-OFF baseline is not valid\n";
            Status(L"Failed"); running_ = false; return;
        }
        log << L"Target Process: " << ProcessPath(*target) << L"\nPID: " << *target << L"\n\n";

        log << L"=== PHASE A - HUD OFF ===\nStart: " << Now() << L"\nTarget: " << ProcessPath(*target)
            << L"\nPID: " << *target << L"\nDuration: 30 seconds\nPoC Running: NO\n";
        Status(L"HUD OFF");
        const auto phaseAEnd = std::chrono::steady_clock::now() + kPhaseDuration;
        while (!stop_ && std::chrono::steady_clock::now() < phaseAEnd) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        log << L"End: " << Now() << L"\n";
        if (stop_) { log << L"RESULT: Cancelled\n"; Status(L"Cancelled"); running_ = false; return; }
        if (!Alive(*target)) { log << L"VRR TEST FAILED\nReason: Target process exited\n"; Status(L"Failed"); running_ = false; return; }

        log << L"=== PHASE B - HUD ON ===\nStart: " << Now() << L"\nDuration: 30 seconds\n"; Status(L"HUD ON");
        const auto poc = std::filesystem::path(app_.ExecutablePath()).replace_filename(L"ClawHUD.VrrPoc.exe");
        const std::wstring commandText = L"\"" + poc.wstring() + L"\" --diagnostic";
        std::vector<wchar_t> command(commandText.begin(), commandText.end());
        command.push_back(L'\0'); STARTUPINFOW startup{ sizeof(startup) }; PROCESS_INFORMATION child{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child))
        {
            log << L"PoC Launch: FAILED\nResult: Failed\n"; Status(L"Failed"); running_ = false; return;
        }
        log << L"PoC Launch: OK\nPoC PID: " << child.dwProcessId << L"\n";
        const auto watchdogEnd = std::chrono::steady_clock::now() + kPocWatchdog; bool timedOut = false;
        while (!stop_)
        {
            const DWORD wait = WaitForSingleObject(child.hProcess, 100);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_FAILED || std::chrono::steady_clock::now() >= watchdogEnd) { timedOut = true; break; }
        }
        if ((stop_ || timedOut) && WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT)
        {
            TerminateProcess(child.hProcess, stop_ ? 2 : 3);
            WaitForSingleObject(child.hProcess, 5000);
        }
        DWORD code = STILL_ACTIVE; GetExitCodeProcess(child.hProcess, &code);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
        const bool completed = !stop_ && !timedOut && code == 0;
        log << L"End: " << Now() << L"\nPoC Exit: "
            << (stop_ ? L"Cancelled" : (completed ? L"OK (normal)" : L"FAILED")) << L"\n";
        log << L"=== RESULT ===\nOrchestration: " << (stop_ ? L"Cancelled" : (completed ? L"Completed" : L"Failed"))
            << L"\nVRR Analysis: Not performed\n";
        Status(stop_ ? L"Cancelled" : (completed ? L"Completed" : L"Failed")); running_ = false;
    }
    catch (...) { if (log.is_open()) log << L"RESULT: Failed\n"; Status(L"Failed"); running_ = false; }
}
