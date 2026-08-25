#include "VrrDiagnostic.h"

#include "App.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <vector>

namespace
{
constexpr auto kCaptureDuration = std::chrono::seconds(28);
constexpr auto kPocWatchdog = std::chrono::seconds(35);
constexpr auto kCaptureWatchdog = std::chrono::seconds(35);
constexpr auto kTargetWait = std::chrono::seconds(15);

struct CsvSummary
{
    bool valid{};
    std::string reason;
    std::size_t rows{};
    std::string dominantSwapChain;
    std::size_t dominantRows{};
    std::map<std::string, std::size_t> modes;
    std::size_t displayed{};
    std::size_t notDisplayed{};
    double presentAverage{};
    double displayAverage{};
    double displayMin{};
    double displayMax{};
};

std::wstring Now(bool fileName = false)
{
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::tm local{};
    localtime_s(&local, &time); std::wstringstream out;
    out << std::put_time(&local, fileName ? L"%Y%m%d-%H%M%S" : L"%Y-%m-%d %H:%M:%S"); return out.str();
}

std::string Narrow(const std::wstring& value)
{
    std::string result; result.reserve(value.size());
    for (const wchar_t character : value) result.push_back(static_cast<char>(character));
    return result;
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
    if (name.ends_with(L"\\explorer.exe") || name.ends_with(L"\\clawhud.exe") || name.ends_with(L"\\clawhud.vrrpoc.exe")) return std::nullopt;
    return pid;
}

bool Alive(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!process) return false;
    DWORD code{}; const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE; CloseHandle(process); return alive;
}

bool ExistingPocWindow() { return FindWindowW(L"ClawHUD.VrrPoc", nullptr) != nullptr; }

std::wstring ProcessPath(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!process) return L"Unavailable";
    wchar_t path[MAX_PATH]{}; DWORD size = ARRAYSIZE(path); QueryFullProcessImageNameW(process, 0, path, &size); CloseHandle(process);
    return size ? std::wstring(path, size) : L"Unavailable";
}

std::vector<std::string> CsvLine(const std::string& line)
{
    std::vector<std::string> fields; std::string field; bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (c == '"') { if (quoted && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; } else quoted = !quoted; }
        else if (c == ',' && !quoted) { fields.push_back(field); field.clear(); }
        else field += c;
    }
    fields.push_back(field); return fields;
}

CsvSummary ParseCsv(const std::filesystem::path& path)
{
    CsvSummary result; std::ifstream file(path); if (!file.is_open()) { result.reason = "CSV could not be opened"; return result; }
    std::string line; if (!std::getline(file, line)) { result.reason = "CSV is empty"; return result; }
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) line.erase(0, 3);
    const auto headers = CsvLine(line); std::map<std::string, std::size_t> column;
    for (std::size_t i = 0; i < headers.size(); ++i) column[headers[i]] = i;
    const char* required[] = { "ProcessID", "SwapChainAddress", "PresentMode", "CPUStartQPCTime", "DisplayedTime", "MsBetweenPresents", "MsBetweenDisplayChange" };
    for (const auto name : required) if (!column.contains(name)) { result.reason = std::string("Required PresentMon column missing: ") + name; return result; }
    const auto field = [&](const std::vector<std::string>& row, const char* name) -> std::string { const auto i = column.at(name); return i < row.size() ? row[i] : std::string{}; };
    std::map<std::string, std::size_t> swaps; double presentTotal{}, displayTotal{}; std::size_t presentCount{}, displayCount{};
    while (std::getline(file, line))
    {
        if (line.empty()) continue; const auto row = CsvLine(line); ++result.rows;
        ++swaps[field(row, "SwapChainAddress")]; ++result.modes[field(row, "PresentMode")];
        const auto displayed = field(row, "DisplayedTime");
        if (!displayed.empty() && displayed != "NA") ++result.displayed; else ++result.notDisplayed;
        try { const double value = std::stod(field(row, "MsBetweenPresents")); presentTotal += value; ++presentCount; } catch (...) {}
        try
        {
            const double value = std::stod(field(row, "MsBetweenDisplayChange")); displayTotal += value; ++displayCount;
            if (displayCount == 1 || value < result.displayMin) result.displayMin = value;
            if (displayCount == 1 || value > result.displayMax) result.displayMax = value;
        }
        catch (...) {}
    }
    if (!result.rows) { result.reason = "CSV has no data rows"; return result; }
    for (const auto& [swap, count] : swaps) if (count > result.dominantRows) { result.dominantSwapChain = swap; result.dominantRows = count; }
    result.presentAverage = presentCount ? presentTotal / presentCount : 0.0;
    result.displayAverage = displayCount ? displayTotal / displayCount : 0.0; result.valid = true; return result;
}

void WriteSummary(std::wofstream& log, const wchar_t* phase, const std::filesystem::path& csv, const CsvSummary& summary)
{
    log << L"=== " << phase << L" SUMMARY ===\nCSV: " << csv.wstring() << L"\n";
    if (!summary.valid) { log << L"Summary: unavailable\nReason: " << std::wstring(summary.reason.begin(), summary.reason.end()) << L"\n"; return; }
    log << L"Rows: " << summary.rows << L"\nDominant SwapChain: " << std::wstring(summary.dominantSwapChain.begin(), summary.dominantSwapChain.end())
        << L"\nDominant Rows: " << summary.dominantRows << L"\nPresentMode distribution:\n";
    for (const auto& [mode, count] : summary.modes) log << L"  " << std::wstring(mode.begin(), mode.end()) << L": " << count << L"\n";
    log << L"Average MsBetweenPresents: " << summary.presentAverage << L"\nAverage MsBetweenDisplayChange: " << summary.displayAverage
        << L"\nMin MsBetweenDisplayChange: " << summary.displayMin << L"\nMax MsBetweenDisplayChange: " << summary.displayMax
        << L"\nDisplayed frames: " << summary.displayed << L"\nNot-displayed frames: " << summary.notDisplayed << L"\n";
}

bool Launch(const std::wstring& command, PROCESS_INFORMATION& process)
{
    std::vector<wchar_t> line(command.begin(), command.end()); line.push_back(L'\0'); STARTUPINFOW startup{ sizeof(startup) };
    return CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
}
}

bool VrrDiagnostic::Start()
{
    if (running_.exchange(true)) return false; if (worker_.joinable()) worker_.join(); stop_ = false;
    try { worker_ = std::thread(&VrrDiagnostic::Run, this); } catch (...) { running_ = false; throw; }
    Status(L"Waiting for game"); return true;
}

void VrrDiagnostic::Stop()
{
    stop_ = true;
    if (const HANDLE capture = captureProcess_.load()) TerminateProcess(capture, 2);
    if (const HANDLE poc = pocProcess_.load()) TerminateProcess(poc, 2);
    if (worker_.joinable()) worker_.join(); running_ = false;
}

void VrrDiagnostic::Status(const wchar_t* text) const
{
    if (!notifyWindow_) return; auto* value = new std::wstring(text);
    if (!PostMessageW(notifyWindow_, kVrrDiagnosticStatus, reinterpret_cast<WPARAM>(value), 0)) delete value;
}

bool VrrDiagnostic::Capture(const std::filesystem::path& executable, DWORD pid, const std::filesystem::path& csv,
    const std::string& session, std::wofstream& log)
{
    const std::wstring command = L"\"" + executable.wstring() + L"\" --process_id " + std::to_wstring(pid) +
        L" --output_file \"" + csv.wstring() + L"\" --timed " + std::to_wstring(kCaptureDuration.count()) +
        L" --terminate_after_timed --no_console_stats --qpc_time_ms --session_name " + std::wstring(session.begin(), session.end());
    PROCESS_INFORMATION process{}; if (!Launch(command, process)) { log << L"PresentMon: FAILED\nReason: CreateProcess failed\n"; return false; }
    captureProcess_ = process.hProcess; const auto deadline = std::chrono::steady_clock::now() + kCaptureWatchdog; bool timeout = false;
    while (!stop_)
    {
        const DWORD wait = WaitForSingleObject(process.hProcess, 100); if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED || std::chrono::steady_clock::now() >= deadline) { timeout = true; break; }
    }
    if ((stop_ || timeout) && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) { TerminateProcess(process.hProcess, stop_ ? 2 : 3); WaitForSingleObject(process.hProcess, 5000); }
    DWORD code = STILL_ACTIVE; GetExitCodeProcess(process.hProcess, &code); captureProcess_ = nullptr; CloseHandle(process.hThread); CloseHandle(process.hProcess);
    const bool csvCreated = std::filesystem::exists(csv) && std::filesystem::file_size(csv) != 0;
    log << L"PresentMon Exit Code: " << code << L"\nCapture CSV Created: " << (csvCreated ? L"YES" : L"NO") << L"\n";
    if (stop_) { log << L"PresentMon: Cancelled\n"; return false; }
    if (timeout || code != 0 || !csvCreated) { log << L"PresentMon: FAILED\n"; return false; }
    return true;
}

void VrrDiagnostic::Run()
{
    std::wofstream log;
    try
    {
        const auto folder = LogFolder(app_); const auto stamp = Now(true); const auto txt = folder / (L"vrr-" + stamp + L".txt");
        const auto offCsv = folder / (L"vrr-" + stamp + L"-off.csv"); const auto onCsv = folder / (L"vrr-" + stamp + L"-on.csv"); log.open(txt);
        if (!log.is_open()) { Status(L"Failed"); running_ = false; return; }
        log << L"=== CLAWHUD VRR DIAGNOSTIC ===\nTimestamp: " << Now() << L"\nPresentMon: 2.5.1\n"
            << L"Display tracking: enabled (no --no_track_display)\n"
            << L"Pinned v2.5.1 console asset does not expose --write_display_metadata; standard display timing columns are retained.\n"
            << L"VRR Analysis: NEEDS MANUAL REVIEW\nNOTE: PresentMon captures application presents and OS-visible display timing.\n"
            << L"Intel UMD XeFG-generated output frames may not all be observable here.\nDo not treat this capture as authoritative true XeFG displayed FPS.\n\n";
        const auto pm = std::filesystem::path(app_.ExecutablePath()).parent_path() / L"tools" / L"PresentMon.exe";
        if (!std::filesystem::exists(pm)) { log << L"PresentMon: FAILED\nReason: tools\\PresentMon.exe not found\n"; Status(L"Failed"); running_ = false; return; }
        std::this_thread::sleep_for(std::chrono::seconds(1)); std::optional<DWORD> target; const auto targetEnd = std::chrono::steady_clock::now() + kTargetWait;
        while (!stop_ && std::chrono::steady_clock::now() < targetEnd) { target = ForegroundTarget(); if (target) break; std::this_thread::sleep_for(std::chrono::milliseconds(250)); }
        if (stop_) { log << L"RESULT: Cancelled\n"; Status(L"Cancelled"); running_ = false; return; }
        if (!target) { log << L"VRR TEST FAILED\nReason: No valid foreground target process\n"; Status(L"Failed"); running_ = false; return; }
        if (ExistingPocWindow()) { log << L"VRR TEST FAILED\nReason: ClawHUD.VrrPoc is already running; HUD-OFF baseline is not valid\n"; Status(L"Failed"); running_ = false; return; }
        log << L"Target Process: " << ProcessPath(*target) << L"\nPID: " << *target << L"\n\n";
        log << L"=== PHASE A - HUD OFF ===\nStart: " << Now() << L"\nCSV: " << offCsv.wstring() << L"\n"; Status(L"HUD OFF");
        const std::string sessionStamp = Narrow(stamp);
        std::this_thread::sleep_for(std::chrono::seconds(1)); const bool offOk = Capture(pm, *target, offCsv, "ClawHUD-VRR-" + std::to_string(*target) + "-OFF-" + sessionStamp, log);
        std::this_thread::sleep_for(std::chrono::seconds(1)); log << L"End: " << Now() << L"\n"; const auto off = ParseCsv(offCsv); WriteSummary(log, L"PHASE A - HUD OFF", offCsv, off);
        if (stop_) { log << L"RESULT: Cancelled\n"; Status(L"Cancelled"); running_ = false; return; }
        if (!offOk || !Alive(*target)) { log << L"RESULT: Failed\n"; Status(L"Failed"); running_ = false; return; }

        log << L"=== PHASE B - HUD ON ===\nStart: " << Now() << L"\nCSV: " << onCsv.wstring() << L"\n"; Status(L"HUD ON");
        const auto poc = std::filesystem::path(app_.ExecutablePath()).replace_filename(L"ClawHUD.VrrPoc.exe"); const std::wstring command = L"\"" + poc.wstring() + L"\" --diagnostic";
        PROCESS_INFORMATION child{}; if (!Launch(command, child)) { log << L"PoC Launch: FAILED\nRESULT: Failed\n"; Status(L"Failed"); running_ = false; return; }
        pocProcess_ = child.hProcess;
        log << L"PoC Launch: OK\nPoC PID: " << child.dwProcessId << L"\n"; const auto visibleEnd = std::chrono::steady_clock::now() + std::chrono::seconds(5); bool visible = false;
        while (!stop_ && std::chrono::steady_clock::now() < visibleEnd) { const HWND hwnd = FindWindowW(L"ClawHUD.VrrPoc", nullptr); if (hwnd && IsWindowVisible(hwnd)) { visible = true; break; } std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
        if (!visible) { if (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT) { TerminateProcess(child.hProcess, 3); WaitForSingleObject(child.hProcess, 5000); } pocProcess_ = nullptr; CloseHandle(child.hThread); CloseHandle(child.hProcess); log << L"PoC HUD: FAILED\nReason: PoC HUD did not become visible\nRESULT: Failed\n"; Status(L"Failed"); running_ = false; return; }
        const bool onOk = Capture(pm, *target, onCsv, "ClawHUD-VRR-" + std::to_string(*target) + "-ON-" + sessionStamp, log);
        bool pocTimeout = false; const auto pocEnd = std::chrono::steady_clock::now() + kPocWatchdog;
        while (!stop_ && WaitForSingleObject(child.hProcess, 100) == WAIT_TIMEOUT && std::chrono::steady_clock::now() < pocEnd) {}
        if ((stop_ || std::chrono::steady_clock::now() >= pocEnd) && WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT) { pocTimeout = true; TerminateProcess(child.hProcess, stop_ ? 2 : 3); WaitForSingleObject(child.hProcess, 5000); }
        DWORD code = STILL_ACTIVE; GetExitCodeProcess(child.hProcess, &code); pocProcess_ = nullptr; CloseHandle(child.hThread); CloseHandle(child.hProcess); const auto on = ParseCsv(onCsv); WriteSummary(log, L"PHASE B - HUD ON", onCsv, on);
        log << L"PoC Exit: " << ((!pocTimeout && code == 0) ? L"OK (normal)" : L"FAILED") << L"\n=== COMPARISON ===\nPresentMode changed: " << ((off.modes != on.modes) ? L"YES" : L"NO") << L"\nVRR Analysis: NEEDS MANUAL REVIEW\n";
        const bool completed = !stop_ && onOk && !pocTimeout && code == 0; log << L"Result: " << (completed ? L"Completed" : L"Failed") << L"\n"; Status(stop_ ? L"Cancelled" : (completed ? L"Completed" : L"Failed")); running_ = false;
    }
    catch (...) { if (log.is_open()) log << L"RESULT: Failed\n"; Status(L"Failed"); running_ = false; }
}
