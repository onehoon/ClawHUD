#include "DiagnosticSession.h"

#include <dwmapi.h>
#include <appmodel.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <array>
#include <iomanip>
#include <sstream>
#include <unordered_map>

DiagnosticSession* DiagnosticSession::active_ = nullptr;

namespace
{
std::filesystem::path OutputPath()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t name[64]{};
    swprintf_s(name, L"game-detect-%04u%02u%02u-%02u%02u%02u.jsonl", time.wYear,
        time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    return std::filesystem::current_path() / name;
}

std::uint32_t ReadSteamAppId() noexcept
{
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0,
        KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return 0;
    DWORD type{}, value{}, size = sizeof(value);
    const auto result = RegQueryValueExW(key, L"RunningAppID", nullptr, &type,
        reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(value) ? value : 0;
}

std::string Utf8(std::wstring_view value) noexcept
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size > 0 ? static_cast<size_t>(size) : 0, '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

bool Is3dPath(std::wstring_view path, DWORD& pid)
{
    const auto open = path.find(L"pid_"); const auto end = path.find(L'_', open == std::wstring_view::npos ? 0 : open + 4);
    if (open == std::wstring_view::npos || end == std::wstring_view::npos || path.find(L"_engtype_3D") == std::wstring_view::npos) return false;
    try { pid = static_cast<DWORD>(std::stoul(std::wstring(path.substr(open + 4, end - open - 4)))); return pid != 0; } catch (...) { return false; }
}
}

DiagnosticSession::~DiagnosticSession() { Stop(); }

bool DiagnosticSession::Start()
{
    if (running_ || active_) return false;
    path_ = OutputPath();
    log_.open(path_, std::ios::binary | std::ios::trunc);
    if (!log_) return false;
    startedAt_ = std::chrono::steady_clock::now();
    active_ = this;
    foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    constexpr std::array<DWORD, 5> events{ EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW,
        EVENT_OBJECT_HIDE, EVENT_OBJECT_DESTROY, EVENT_OBJECT_NAMECHANGE };
    for (size_t index = 0; index < events.size(); ++index)
        windowHooks_[index] = SetWinEventHook(events[index], events[index], nullptr,
            WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!foregroundHook_)
    {
        Stop();
        return false;
    }
    previousSteamAppId_ = ReadSteamAppId();
    running_ = true;
    std::string api2Detail;
    const bool api2Ready = api2_.Start(api2Detail);
    WriteRecord("session_header", "\"schemaVersion\":1,\"api2Loader\":\"manual beside exe\",\"api2State\":\"" + api2Detail + "\"");
    WriteRecord("steam_running_app_id", "\"oldAppId\":null,\"appId\":" + std::to_string(previousSteamAppId_));
    nextApi2Sample_ = startedAt_;
    sampler_ = std::jthread([this] { SampleLoop(); });
    steamWatcher_ = std::jthread([this] { WatchSteamRunningAppId(); });
    return true;
}

void DiagnosticSession::Stop() noexcept
{
    running_ = false;
    if (sampler_.joinable()) sampler_.request_stop(), sampler_.join();
    if (steamWatcher_.joinable()) steamWatcher_.request_stop(), steamWatcher_.join();
    if (foregroundHook_) UnhookWinEvent(foregroundHook_);
    foregroundHook_ = nullptr;
    for (auto& hook : windowHooks_) { if (hook) UnhookWinEvent(hook); hook = nullptr; }
    if (active_ == this) active_ = nullptr;
    api2_.Stop();
    WriteSummary();
    if (log_) { WriteRecord("session_stop", ""); log_.close(); }
}

std::filesystem::path DiagnosticSession::LogPath() const { return path_; }

void CALLBACK DiagnosticSession::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
    LONG objectId, LONG childId, DWORD, DWORD)
{
    if (active_) active_->RecordWinEvent(event, hwnd, objectId, childId);
}

void DiagnosticSession::RecordWinEvent(DWORD event, HWND hwnd, LONG objectId, LONG childId) noexcept
{
    if (!running_ || !hwnd || (event != EVENT_SYSTEM_FOREGROUND &&
        (objectId != OBJID_WINDOW || childId != CHILDID_SELF))) return;
    const char* type = event == EVENT_SYSTEM_FOREGROUND ? "foreground_change" :
        event == EVENT_OBJECT_CREATE ? "window_create" : event == EVENT_OBJECT_SHOW ? "window_show" :
        event == EVENT_OBJECT_HIDE ? "window_hide" : event == EVENT_OBJECT_DESTROY ? "window_destroy" : "window_name_change";
    DWORD pid{};
    WriteRecord(type, WindowFields(hwnd, &pid));
    ObserveProcess(pid, type);
}

void DiagnosticSession::SampleLoop() noexcept
{
    while (running_)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextApi2Sample_)
        {
            const HWND foreground = GetForegroundWindow();
            DWORD pid{}; if (foreground) GetWindowThreadProcessId(foreground, &pid);
            if (pid) { ObserveProcess(pid, "foreground_sample"); WriteRecord("api2", "\"pid\":" + std::to_string(pid) + "," + api2_.Sample(pid)); }
            nextApi2Sample_ = now + std::chrono::milliseconds(250);
        }
        SampleTopGpu();
        Sleep(500);
    }
}

void DiagnosticSession::ObserveProcess(DWORD processId, std::string_view reason) noexcept
{
    if (!processId || processId == GetCurrentProcessId()) return;
    try
    {
        std::lock_guard lock(observedMutex_);
        if (firstSeenMs_.contains(processId)) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt_).count();
        firstSeenMs_.emplace(processId, elapsed);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        std::wstring image, aumid, packageFull, packageFamily;
        if (process)
        {
            DWORD imageLength = 0; QueryFullProcessImageNameW(process, 0, nullptr, &imageLength);
            if (imageLength) { image.resize(imageLength); if (QueryFullProcessImageNameW(process, 0, image.data(), &imageLength)) image.resize(imageLength); else image.clear(); }
            UINT32 size{}; if (GetApplicationUserModelId(process, &size, nullptr) == ERROR_INSUFFICIENT_BUFFER) { aumid.resize(size); if (GetApplicationUserModelId(process, &size, aumid.data()) == ERROR_SUCCESS) aumid.resize(size ? size - 1 : 0); else aumid.clear(); }
            size = 0; if (GetPackageFullName(process, &size, nullptr) == ERROR_INSUFFICIENT_BUFFER) { packageFull.resize(size); if (GetPackageFullName(process, &size, packageFull.data()) == ERROR_SUCCESS) packageFull.resize(size ? size - 1 : 0); else packageFull.clear(); }
            size = 0; if (GetPackageFamilyName(process, &size, nullptr) == ERROR_INSUFFICIENT_BUFFER) { packageFamily.resize(size); if (GetPackageFamilyName(process, &size, packageFamily.data()) == ERROR_SUCCESS) packageFamily.resize(size ? size - 1 : 0); else packageFamily.clear(); }
            CloseHandle(process);
        }
        const bool gameConfig = !image.empty() && std::filesystem::exists(std::filesystem::path(image).parent_path() / L"MicrosoftGame.config");
        WriteRecord("pid_observed", "\"pid\":" + std::to_string(processId) + ",\"reason\":\"" + std::string(reason) +
            "\",\"imagePath\":\"" + Json(image) + "\",\"aumid\":\"" + Json(aumid) +
            "\",\"packageFullName\":\"" + Json(packageFull) + "\",\"packageFamilyName\":\"" + Json(packageFamily) +
            "\",\"microsoftGameConfigExists\":" + (gameConfig ? "true" : "false"));
    }
    catch (...) {}
}

void DiagnosticSession::WriteSummary() noexcept
{
    try
    {
        std::lock_guard lock(observedMutex_);
        for (const auto& [pid, firstSeen] : firstSeenMs_)
            WriteRecord("pid_summary", "\"pid\":" + std::to_string(pid) + ",\"firstSeenMs\":" + std::to_string(firstSeen));
    }
    catch (...) {}
}

void DiagnosticSession::SampleTopGpu() noexcept
{
    DWORD chars{};
    if (PdhExpandWildCardPathW(nullptr, L"\\GPU Engine(*)\\Running time", nullptr, &chars, 0) != PDH_MORE_DATA) return;
    std::vector<wchar_t> paths(chars + 1);
    if (PdhExpandWildCardPathW(nullptr, L"\\GPU Engine(*)\\Running time", paths.data(), &chars, 0) != ERROR_SUCCESS) return;
    HQUERY query{}; if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return;
    struct Counter { DWORD pid; HCOUNTER counter; double first{}; bool valid{}; };
    std::vector<Counter> counters;
    for (const wchar_t* path = paths.data(); *path; path += wcslen(path) + 1)
    {
        DWORD pid{}; if (!Is3dPath(path, pid)) continue; HCOUNTER counter{};
        if (PdhAddCounterW(query, path, 0, &counter) == ERROR_SUCCESS) counters.push_back({ pid, counter });
    }
    if (PdhCollectQueryData(query) != ERROR_SUCCESS || PdhCollectQueryData(query) != ERROR_SUCCESS) { PdhCloseQuery(query); return; }
    for (auto& counter : counters) { PDH_FMT_COUNTERVALUE value{}; counter.valid = PdhGetFormattedCounterValue(counter.counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS; counter.first = value.doubleValue; }
    Sleep(100); if (PdhCollectQueryData(query) != ERROR_SUCCESS) { PdhCloseQuery(query); return; }
    std::unordered_map<DWORD, double> totals;
    for (const auto& counter : counters) { PDH_FMT_COUNTERVALUE value{}; if (counter.valid && PdhGetFormattedCounterValue(counter.counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS && value.doubleValue > counter.first) totals[counter.pid] += value.doubleValue - counter.first; }
    PdhCloseQuery(query);
    std::vector<std::pair<DWORD, double>> ranked(totals.begin(), totals.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) { return left.second > right.second; });
    for (size_t rank = 0; rank < std::min<size_t>(ranked.size(), 5); ++rank)
    {
        ObserveProcess(ranked[rank].first, "top_gpu");
        WriteRecord("top_gpu", "\"mode\":\"Raw\",\"rank\":" + std::to_string(rank + 1) + ",\"pid\":" + std::to_string(ranked[rank].first) + ",\"gpu3dDelta\":" + std::to_string(ranked[rank].second));
    }
}

void DiagnosticSession::WatchSteamRunningAppId() noexcept
{
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0,
        KEY_NOTIFY | KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return;
    HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!changed) { RegCloseKey(key); return; }
    while (running_)
    {
        if (RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET,
            changed, TRUE) != ERROR_SUCCESS)
            break;
        while (running_ && WaitForSingleObject(changed, 100) == WAIT_TIMEOUT) {}
        if (!running_) break;
        const auto appId = ReadSteamAppId();
        if (appId != previousSteamAppId_)
        {
            WriteRecord("steam_running_app_id", "\"oldAppId\":" + std::to_string(previousSteamAppId_) +
                ",\"appId\":" + std::to_string(appId));
            previousSteamAppId_ = appId;
        }
    }
    CloseHandle(changed);
    RegCloseKey(key);
}

void DiagnosticSession::WriteRecord(std::string type, std::string fields) noexcept
{
    try
    {
        std::lock_guard lock(logMutex_);
        if (!log_) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt_).count();
        SYSTEMTIME now{}; GetLocalTime(&now);
        char time[48]{}; sprintf_s(time, "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        log_ << "{\"wallTime\":\"" << time << "\",\"elapsedMs\":" << elapsed
             << ",\"sequence\":" << ++sequence_ << ",\"type\":\"" << type << "\"";
        if (!fields.empty()) log_ << ',' << fields;
        log_ << "}\n";
        log_.flush();
    }
    catch (...) {}
}

std::string DiagnosticSession::WindowFields(HWND hwnd, DWORD* processId) noexcept
{
    DWORD pid{}; GetWindowThreadProcessId(hwnd, &pid); if (processId) *processId = pid;
    wchar_t title[1024]{}, className[256]{}; GetWindowTextW(hwnd, title, 1024); GetClassNameW(hwnd, className, 256);
    RECT rect{}; const bool rectAvailable = SUCCEEDED(DwmGetWindowAttribute(hwnd,
        DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))) || GetWindowRect(hwnd, &rect);
    DWORD cloaked{}; const bool cloakedAvailable = SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)));
    std::ostringstream fields;
    fields << "\"pid\":" << pid << ",\"hwnd\":\"" << Hex(hwnd) << "\",\"title\":\"" << Json(title)
           << "\",\"class\":\"" << Json(className) << "\",\"visible\":" << (IsWindowVisible(hwnd) ? "true" : "false")
           << ",\"minimized\":" << (IsIconic(hwnd) ? "true" : "false") << ",\"owner\":\"" << Hex(GetWindow(hwnd, GW_OWNER)) << "\"";
    if (cloakedAvailable) fields << ",\"cloaked\":" << (cloaked ? "true" : "false");
    if (rectAvailable) fields << ",\"rect\":\"" << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom << "\"";
    return fields.str();
}

std::string DiagnosticSession::Json(std::wstring_view value) noexcept
{
    std::string result;
    for (const char character : Utf8(value))
    {
        if (character == '\\' || character == '\"') result += '\\';
        if (character == '\n') result += "\\n";
        else if (character != '\r') result += character;
    }
    return result;
}

std::string DiagnosticSession::Hex(HWND hwnd) noexcept
{
    std::ostringstream value;
    value << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(hwnd);
    return value.str();
}
