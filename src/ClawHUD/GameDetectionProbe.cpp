#include "GameDetectionProbe.h"

#include <dwmapi.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>

#pragma comment(lib, "pdh.lib")

namespace clawhud
{
namespace
{
std::string Narrow(std::wstring_view value)
{
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(std::max(bytes, 0)), '\0');
    if (bytes) WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), result.data(), bytes, nullptr, nullptr);
    return result;
}
std::string Quote(std::wstring_view value)
{
    return "\"" + Narrow(value) + "\"";
}
std::string HexPointer(const void* value)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase
        << reinterpret_cast<std::uintptr_t>(value);
    return out.str();
}
std::string RectText(const RECT& rect)
{
    std::ostringstream out;
    out << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom;
    return out.str();
}
struct WindowInfo
{
    HWND window{};
    bool visible{};
    HWND owner{};
};
BOOL CALLBACK CollectWindows(HWND window, LPARAM parameter)
{
    auto* windows = reinterpret_cast<std::unordered_map<DWORD, WindowInfo>*>(parameter);
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    if (pid && IsPresentMonCandidateWindow(IsWindowVisible(window),
        GetWindow(window, GW_OWNER)))
        windows->try_emplace(pid, WindowInfo{ window, true, nullptr });
    return TRUE;
}
std::vector<std::wstring> ExpandGpuPaths(PDH_STATUS& status)
{
    DWORD size{};
    status = PdhExpandWildCardPathW(nullptr, L"\\GPU Engine(*)\\Running time",
        nullptr, &size, 0);
    if (status != PDH_MORE_DATA)
        return {};
    std::vector<wchar_t> buffer(size + 1);
    status = PdhExpandWildCardPathW(nullptr, L"\\GPU Engine(*)\\Running time",
        buffer.data(), &size, 0);
    if (status != ERROR_SUCCESS)
        return {};
    std::vector<std::wstring> paths;
    for (const wchar_t* item = buffer.data(); *item; item += wcslen(item) + 1)
        paths.emplace_back(item);
    return paths;
}
}

std::optional<DWORD> ParseGpuEngineProcessId(std::wstring_view instance) noexcept
{
    constexpr std::wstring_view prefix = L"pid_";
    if (instance.substr(0, prefix.size()) != prefix) return std::nullopt;
    const auto end = instance.find(L'_', prefix.size());
    if (end == std::wstring_view::npos || end == prefix.size()) return std::nullopt;
    DWORD value{};
    for (const wchar_t c : instance.substr(prefix.size(), end - prefix.size()))
    {
        if (c < L'0' || c > L'9') return std::nullopt;
        const DWORD digit = static_cast<DWORD>(c - L'0');
        if (value > (MAXDWORD - digit) / 10) return std::nullopt;
        value = value * 10 + digit;
    }
    return value ? std::optional<DWORD>(value) : std::nullopt;
}
bool IsGpuEngine3DInstance(std::wstring_view instance) noexcept
{
    constexpr std::wstring_view suffix = L"_engtype_3D";
    return instance.size() >= suffix.size() &&
        instance.substr(instance.size() - suffix.size()) == suffix;
}
bool IsPresentMonCandidateWindow(bool visible, HWND owner) noexcept
{ return visible && owner == nullptr; }
std::vector<GameDetectionCandidate> RankGpuCandidates(
    const std::vector<GameDetectionEngineDelta>& engines,
    const std::vector<GameDetectionCandidate>& windows)
{
    std::map<DWORD, double> totals;
    for (const auto& engine : engines)
        if (engine.processId && engine.delta > 0) totals[engine.processId] += engine.delta;
    std::vector<GameDetectionCandidate> result;
    for (const auto& window : windows)
    {
        const auto total = totals.find(window.processId);
        if (total == totals.end() || total->second <= 0) continue;
        auto candidate = window;
        candidate.gpu3dDelta = total->second;
        result.push_back(std::move(candidate));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.gpu3dDelta > right.gpu3dDelta;
    });
    return result;
}
bool IsFullscreenLike(const RECT& window, const RECT& monitor, LONG tolerance) noexcept
{
    return std::abs(window.left - monitor.left) <= tolerance &&
        std::abs(window.top - monitor.top) <= tolerance &&
        std::abs(window.right - monitor.right) <= tolerance &&
        std::abs(window.bottom - monitor.bottom) <= tolerance;
}
std::string FormatProbePid(DWORD processId)
{ return processId ? std::to_string(processId) : "n/a"; }
std::string FormatProbeOptional(const std::optional<double>& value)
{
    if (!value) return "n/a";
    std::ostringstream out; out << std::setprecision(8) << *value; return out.str();
}

GameDetectionProbe::GameDetectionProbe(std::filesystem::path path,
    Api2Summary api2Summary, std::function<std::string()> clawHudSummary)
    : path_(std::move(path)), api2Summary_(std::move(api2Summary)),
      clawHudSummary_(std::move(clawHudSummary)) {}
GameDetectionProbe::~GameDetectionProbe() { Stop(); }
bool GameDetectionProbe::Start()
{
    if (started_) return false;
    log_.open(path_, std::ios::out | std::ios::trunc);
    started_ = log_.is_open();
    if (started_)
        log_ << "=== Game Detection Research Probe ===\n";
    return started_;
}
void GameDetectionProbe::Stop() noexcept
{
    if (log_.is_open()) { log_ << "probe_complete=true\n"; log_.flush(); log_.close(); }
    started_ = false;
}
std::wstring GameDetectionProbe::ProcessName(DWORD processId)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return {};
    wchar_t path[MAX_PATH * 4]{}; DWORD size = static_cast<DWORD>(std::size(path));
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
    CloseHandle(process);
    if (!ok) return {};
    const std::wstring full(path, size);
    const auto slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? full : full.substr(slash + 1);
}
std::wstring GameDetectionProbe::WindowTitle(HWND window)
{
    wchar_t title[512]{};
    const int length = GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    return std::wstring(title, static_cast<size_t>(std::max(length, 0)));
}
void GameDetectionProbe::LogForeground(HWND window, DWORD processId)
{
    const auto executable = ProcessName(processId);
    const auto title = WindowTitle(window);
    const HWND owner = window ? GetWindow(window, GW_OWNER) : nullptr;
    log_ << "[GameDetectProbe][Foreground]\n"
        << "pid=" << FormatProbePid(processId) << " hwnd=" << HexPointer(window)
        << " exe=" << Quote(executable) << " title=" << Quote(title)
        << " visible=" << (window && IsWindowVisible(window) ? 1 : 0)
        << " owner=" << HexPointer(owner) << "\n";
    if (previousForegroundPid_ != processId)
    {
        log_ << "[GameDetectProbe][ForegroundChange] oldPid="
            << FormatProbePid(previousForegroundPid_) << " newPid="
            << FormatProbePid(processId) << " oldExe=" << Quote(previousForegroundExe_)
            << " newExe=" << Quote(executable) << "\n";
        previousForegroundPid_ = processId; previousForegroundExe_ = executable;
    }
    if (api2Summary_)
        log_ << "[GameDetectProbe][API2]\n" << api2Summary_(processId) << '\n';
    if (clawHudSummary_)
        log_ << "[GameDetectProbe][ClawHUD]\n" << clawHudSummary_() << '\n';
}
void GameDetectionProbe::LogGeometry(HWND window)
{
    RECT windowRect{}; bool haveWindow = false;
    if (window)
    {
        haveWindow = SUCCEEDED(DwmGetWindowAttribute(window,
            DWMWA_EXTENDED_FRAME_BOUNDS, &windowRect, sizeof(windowRect)));
        if (!haveWindow) haveWindow = GetWindowRect(window, &windowRect) != FALSE;
    }
    RECT monitorRect{}; HMONITOR monitor = window ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : nullptr;
    MONITORINFO info{ sizeof(info) };
    if (monitor) GetMonitorInfoW(monitor, &info), monitorRect = info.rcMonitor;
    log_ << "[GameDetectProbe][Geometry] window=" << (haveWindow ? RectText(windowRect) : "n/a")
        << " monitor=" << (monitor ? RectText(monitorRect) : "n/a")
        << " fullscreenLike=" << (haveWindow && monitor && IsFullscreenLike(windowRect, monitorRect) ? 1 : 0) << '\n';
}
void GameDetectionProbe::LogPdhCandidates()
{
    PDH_STATUS status{}; const auto paths = ExpandGpuPaths(status);
    if (status != ERROR_SUCCESS)
    {
        log_ << "[GameDetectProbe][PDH][Error] operation=PdhExpandWildCardPath status=0x"
            << std::hex << status << std::dec << "\n";
        log_ << "[GameDetectProbe][TopGPU] unavailable\n"; return;
    }
    std::unordered_map<DWORD, WindowInfo> windows; EnumWindows(CollectWindows, reinterpret_cast<LPARAM>(&windows));
    std::vector<GameDetectionEngineDelta> engines; std::vector<GameDetectionCandidate> candidates;
    HQUERY query{}; status = PdhOpenQueryW(nullptr, 0, &query);
    std::vector<std::pair<DWORD, HCOUNTER>> counters;
    if (status == ERROR_SUCCESS)
    {
        for (const auto& path : paths)
        {
            const auto begin = path.find(L'('), end = path.find(L')', begin);
            if (begin == std::wstring::npos || end == std::wstring::npos) continue;
            const auto instance = std::wstring_view(path).substr(begin + 1, end - begin - 1);
            if (!IsGpuEngine3DInstance(instance)) continue;
            const auto pid = ParseGpuEngineProcessId(instance); if (!pid) continue;
            HCOUNTER counter{};
            if (PdhAddCounterW(query, path.c_str(), 0, &counter) == ERROR_SUCCESS)
                counters.emplace_back(*pid, counter);
        }
        status = PdhCollectQueryData(query);
        if (status == ERROR_SUCCESS) Sleep(100), status = PdhCollectQueryData(query);
        if (status == ERROR_SUCCESS)
            for (const auto& [pid, counter] : counters)
            {
                PDH_FMT_COUNTERVALUE value{};
                const auto read = PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value);
                if (read == ERROR_SUCCESS) engines.push_back({ pid, value.doubleValue });
                else log_ << "[GameDetectProbe][PDH][Error] operation=PdhGetFormattedCounterValue pid="
                    << pid << " status=0x" << std::hex << read << std::dec << '\n';
            }
        PdhCloseQuery(query);
    }
    else log_ << "[GameDetectProbe][PDH][Error] operation=PdhOpenQuery status=0x"
        << std::hex << status << std::dec << '\n';
    for (const auto& [pid, info] : windows)
        candidates.push_back({ pid, info.window, ProcessName(pid), WindowTitle(info.window), 0 });
    const auto ranked = RankGpuCandidates(engines, candidates);
    for (size_t i = 0; i < std::min<size_t>(ranked.size(), 5); ++i)
        log_ << "[GameDetectProbe][PDH] rank=" << i + 1 << " pid=" << ranked[i].processId
            << " exe=" << Quote(ranked[i].executable) << " hwnd=" << HexPointer(ranked[i].window)
            << " title=" << Quote(ranked[i].title) << " gpu3dDelta="
            << std::setprecision(8) << ranked[i].gpu3dDelta << '\n';
    if (ranked.empty()) log_ << "[GameDetectProbe][TopGPU] unavailable\n";
    else log_ << "[GameDetectProbe][TopGPU] pid=" << ranked[0].processId
        << " exe=" << Quote(ranked[0].executable) << " hwnd=" << HexPointer(ranked[0].window)
        << " title=" << Quote(ranked[0].title) << " gpu3dDelta=" << ranked[0].gpu3dDelta << '\n';
}
void GameDetectionProbe::Sample(std::int64_t elapsedMs)
{
    if (!started_) return;
    const HWND foreground = GetForegroundWindow(); DWORD pid{};
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    log_ << "[GameDetectProbe][Sample] seq=" << ++sequence_ << " t=" << elapsedMs << "ms\n";
    LogForeground(foreground, pid); LogGeometry(foreground); LogPdhCandidates(); log_.flush();
}
}
