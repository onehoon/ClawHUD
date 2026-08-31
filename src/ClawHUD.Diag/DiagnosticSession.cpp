#include "DiagnosticSession.h"
#include "DiagBlocklist.h"

#include <dwmapi.h>
#include <appmodel.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <array>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <regex>

std::atomic<DiagnosticSession*> DiagnosticSession::active_ = nullptr;

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

HKEY OpenSteamWatchKey() noexcept
{
    for (const wchar_t* path : { L"Software\\Valve\\Steam", L"Software\\Valve", L"Software" })
    {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_NOTIFY | KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
            return key;
    }
    return nullptr;
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

std::wstring ProcessExe(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t path[32768]{}; DWORD length = static_cast<DWORD>(std::size(path));
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    return ok ? std::filesystem::path(std::wstring(path, length)).filename().wstring() : std::wstring{};
}

std::wstring QueryProcessImagePathInternal(HANDLE process)
{
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) return {};
    path.resize(length);
    return path;
}

std::uint64_t QueryProcessStartFileTimeInternal(HANDLE process)
{
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!process || !GetProcessTimes(process, &creation, &exit, &kernel, &user)) return 0;
    ULARGE_INTEGER value{};
    value.LowPart = creation.dwLowDateTime;
    value.HighPart = creation.dwHighDateTime;
    return value.QuadPart;
}

// GetVersionEx is manifest-gated and reports Windows 8 (6.2) for an
// unmanifested exe like ClawHUD.Diag. RtlGetVersion returns the real build.
std::string ActualOsVersion()
{
    struct RtlOsVersionInfo
    {
        ULONG size{ sizeof(RtlOsVersionInfo) };
        ULONG major{};
        ULONG minor{};
        ULONG build{};
        ULONG platform{};
        WCHAR servicePack[128]{};
    };
    using RtlGetVersionFn = LONG(WINAPI*)(RtlOsVersionInfo*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
        ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : nullptr);
    RtlOsVersionInfo version{};
    if (!rtlGetVersion || rtlGetVersion(&version) < 0) return "unavailable";
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
        std::to_string(version.build);
}

struct CandidateWindow { HWND hwnd{}; std::wstring title; };
BOOL CALLBACK CollectCandidateWindow(HWND hwnd, LPARAM parameter)
{
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) || GetAncestor(hwnd, GA_ROOT) != hwnd) return TRUE;
    DWORD pid{}; GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;
    wchar_t title[1024]{}; GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    reinterpret_cast<std::unordered_map<DWORD, CandidateWindow>*>(parameter)->try_emplace(pid, CandidateWindow{ hwnd, title });
    return TRUE;
}

struct WindowListQuery { DWORD processId{}; std::vector<HWND> windows; };
BOOL CALLBACK CollectTopLevelWindows(HWND hwnd, LPARAM parameter)
{
    auto& query = *reinterpret_cast<WindowListQuery*>(parameter);
    DWORD pid{}; GetWindowThreadProcessId(hwnd, &pid);
    if (pid == query.processId && GetAncestor(hwnd, GA_ROOT) == hwnd)
        query.windows.push_back(hwnd);
    return TRUE;
}

bool IsBlocked(std::wstring_view executable)
{
    std::wstring lower(executable);
    for (auto& value : lower) value = static_cast<wchar_t>(towlower(value));
    const std::string utf8 = Utf8(lower);
    const std::string_view list = kDiagPresentMonBlocklist;
    size_t begin{};
    while (begin < list.size())
    {
        const size_t end = list.find('\n', begin);
        std::string_view line = list.substr(begin, (end == std::string_view::npos ? list.size() : end) - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line == utf8) return true;
        begin = end == std::string_view::npos ? list.size() : end + 1;
    }
    return false;
}

struct MicrosoftConfigEvidence { bool exists{}; bool readable{}; bool executableMatch{}; };

std::wstring ReadConfigText(const std::filesystem::path& path, bool& readable)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff && static_cast<unsigned char>(bytes[1]) == 0xfe)
    {
        if ((bytes.size() - 2) % sizeof(wchar_t) != 0) return {};
        std::wstring result((bytes.size() - 2) / sizeof(wchar_t), L'\0');
        std::memcpy(result.data(), bytes.data() + 2, bytes.size() - 2);
        readable = true;
        return result;
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!bytes.empty() && !length) return {};
    std::wstring result(length, L'\0');
    if (length) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), result.data(), length);
    readable = true;
    return result;
}

MicrosoftConfigEvidence ProbeMicrosoftGameConfig(const std::wstring& image, const std::wstring& packageFull)
{
    std::vector<std::filesystem::path> roots;
    if (!image.empty()) roots.push_back(std::filesystem::path(image).parent_path());
    if (!packageFull.empty())
    {
        using QueryPath = LONG(WINAPI*)(PCWSTR, PackagePathType, UINT32*, PWSTR);
        const auto query = reinterpret_cast<QueryPath>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetPackagePathByFullName2"));
        if (query) for (const auto type : { PackagePathType_Install, PackagePathType_Effective, PackagePathType_Mutable, PackagePathType_MachineExternal, PackagePathType_UserExternal, PackagePathType_EffectiveExternal })
        {
            UINT32 length{};
            if (query(packageFull.c_str(), type, &length, nullptr) != ERROR_INSUFFICIENT_BUFFER) continue;
            std::wstring root(length, L'\0');
            if (query(packageFull.c_str(), type, &length, root.data()) == ERROR_SUCCESS)
            {
                if (!root.empty() && root.back() == L'\0') root.pop_back();
                if (!root.empty()) roots.emplace_back(root);
            }
        }
    }
    MicrosoftConfigEvidence result;
    const std::wstring executable = std::filesystem::path(image).filename().wstring();
    for (const auto& root : roots)
    {
        const auto config = root / L"MicrosoftGame.config";
        std::error_code error;
        if (!std::filesystem::exists(config, error) || error) continue;
        result.exists = true;
        bool readable{};
        const auto xml = ReadConfigText(config, readable);
        result.readable = result.readable || readable;
        if (!readable) continue;
        std::wregex executableTag(L"<\\s*(?:[A-Za-z0-9_.-]+:)?Executable\\b([^>]*)>([^<]*)<\\s*/\\s*(?:[A-Za-z0-9_.-]+:)?Executable\\s*>", std::regex_constants::icase);
        std::wregex selfClosing(L"<\\s*(?:[A-Za-z0-9_.-]+:)?Executable\\b([^>]*)/\\s*>", std::regex_constants::icase);
        std::wregex name(L"Name\\s*=\\s*(?:\\\"([^\\\"]*)\\\"|'([^']*)')", std::regex_constants::icase);
        const auto matches = [&](const std::wsmatch& tag, const std::wstring& text) {
            const std::wstring attributes = tag[1].str();
            std::wsmatch attribute; std::wstring configured = std::regex_search(attributes, attribute, name) ? (attribute[1].matched ? attribute[1].str() : attribute[2].str()) : text;
            return !configured.empty() && _wcsicmp(std::filesystem::path(configured).filename().c_str(), executable.c_str()) == 0;
        };
        for (std::wsregex_iterator it(xml.begin(), xml.end(), executableTag), end; it != end; ++it)
            if (matches(*it, (*it)[2].str())) result.executableMatch = true;
        for (std::wsregex_iterator it(xml.begin(), xml.end(), selfClosing), end; it != end; ++it)
            if (matches(*it, L"")) result.executableMatch = true;
    }
    return result;
}

bool HasPositiveMicrosoftGameIdentity(DWORD processId)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return false;
    const std::wstring image = DiagnosticQueryProcessImagePath(process);
    UINT32 size{}; std::wstring packageFull;
    if (GetPackageFullName(process, &size, nullptr) == ERROR_INSUFFICIENT_BUFFER)
    {
        packageFull.resize(size);
        if (GetPackageFullName(process, &size, packageFull.data()) == ERROR_SUCCESS)
            packageFull.resize(size ? size - 1 : 0);
        else packageFull.clear();
    }
    CloseHandle(process);
    const auto evidence = ProbeMicrosoftGameConfig(image, packageFull);
    return evidence.exists && evidence.executableMatch;
}
}

std::wstring DiagnosticQueryProcessImagePath(HANDLE process)
{
    return QueryProcessImagePathInternal(process);
}

std::uint64_t DiagnosticQueryProcessStartFileTime(HANDLE process)
{
    return QueryProcessStartFileTimeInternal(process);
}

DiagnosticSession::~DiagnosticSession() { Stop(); }

bool DiagnosticSession::Start()
{
    if (running_ || active_.load()) return false;
    {
        std::lock_guard lock(observedMutex_);
        timelines_.clear();
        identityByPid_.clear();
        windowCache_.clear();
    }
    {
        std::lock_guard lock(logMutex_);
        sequence_ = 0;
    }
    previousForeground_ = nullptr;
    previousForegroundPid_ = 0;
    previousSteamAppId_.store(0);
    path_ = OutputPath();
    summaryPath_ = path_;
    summaryPath_.replace_filename(path_.stem().wstring() + L"-summary.txt");
    log_.open(path_, std::ios::binary | std::ios::trunc);
    if (!log_) return false;
    startedAt_ = std::chrono::steady_clock::now();
    active_.store(this);
    if (!StartWinEventThread())
    {
        Stop();
        return false;
    }
    previousSteamAppId_.store(ReadSteamAppId());
    running_ = true;
    std::string api2Detail;
    api2_.Start(api2Detail);
    TIME_ZONE_INFORMATION timezone{}; GetTimeZoneInformation(&timezone);
    WriteRecord("session_header", "\"schemaVersion\":1,\"api2Loader\":\"manual beside exe\",\"api2State\":\"" + api2Detail + "\",\"api2SwapChainCapacity\":" + std::to_string(Api2Evidence::kSwapChainCapacity) + ",\"osVersion\":\"" + ActualOsVersion() + "\",\"timezoneBiasMinutes\":" + std::to_string(-timezone.Bias) + ",\"api2IntervalMs\":250,\"pdhIntervalMs\":500,\"pdhDeltaWindowMs\":100,\"presentMonBlocklistCommit\":\"f57eb474371c635ff2be620c04ca47400ca1b81a\"");
    WriteRecord("steam_running_app_id", "\"oldAppId\":null,\"appId\":" + std::to_string(previousSteamAppId_.load()));
    api2Sampler_ = std::jthread([this](std::stop_token stop) {
        while (!stop.stop_requested() && running_)
        {
            const auto started = std::chrono::steady_clock::now();
            SampleApi2ObservedPids();
            std::this_thread::sleep_until(started + std::chrono::milliseconds(250));
        }
    });
    pdhSampler_ = std::jthread([this](std::stop_token stop) {
        while (!stop.stop_requested() && running_)
        {
            const auto started = std::chrono::steady_clock::now();
            SampleTopGpu();
            std::this_thread::sleep_until(started + std::chrono::milliseconds(500));
        }
    });
    steamWatcher_ = std::jthread([this] { WatchSteamRunningAppId(); });
    return true;
}

void DiagnosticSession::Stop() noexcept
{
    if (!running_ && !log_.is_open() && !api2Sampler_.joinable() &&
        !pdhSampler_.joinable() && !steamWatcher_.joinable() && !winEventThread_.joinable())
        return;
    running_ = false;
    if (api2Sampler_.joinable()) api2Sampler_.request_stop(), api2Sampler_.join();
    if (pdhSampler_.joinable()) pdhSampler_.request_stop(), pdhSampler_.join();
    if (steamWatcher_.joinable()) steamWatcher_.request_stop(), steamWatcher_.join();
    StopWinEventThread();
    if (active_.load() == this) active_.store(nullptr);
    api2_.Stop();
    WriteSummary();
    if (log_) { WriteRecord("session_stop", ""); log_.close(); }
}

bool DiagnosticSession::StartWinEventThread()
{
    std::promise<bool> ready;
    auto result = ready.get_future();
    try
    {
        winEventThread_ = std::jthread([this, ready = std::move(ready)]() mutable {
            winEventThreadId_.store(GetCurrentThreadId());
            MSG seed{}; PeekMessageW(&seed, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
            foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
            constexpr std::array<DWORD, 5> events{ EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW,
                EVENT_OBJECT_HIDE, EVENT_OBJECT_DESTROY, EVENT_OBJECT_NAMECHANGE };
            for (size_t index = 0; index < events.size(); ++index)
                windowHooks_[index] = SetWinEventHook(events[index], events[index], nullptr,
                    WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
            const bool ok = foregroundHook_ && std::all_of(std::begin(windowHooks_),
                std::end(windowHooks_), [](HWINEVENTHOOK hook) { return hook != nullptr; });
            ready.set_value(ok);
            if (ok)
            {
                MSG message{};
                while (GetMessageW(&message, nullptr, 0, 0) > 0)
                { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            if (foregroundHook_) UnhookWinEvent(foregroundHook_);
            foregroundHook_ = nullptr;
            for (auto& hook : windowHooks_) { if (hook) UnhookWinEvent(hook); hook = nullptr; }
            winEventThreadId_.store(0);
        });
        return result.get();
    }
    catch (...) { return false; }
}

void DiagnosticSession::StopWinEventThread() noexcept
{
    const DWORD id = winEventThreadId_.exchange(0);
    if (id) PostThreadMessageW(id, WM_QUIT, 0, 0);
    if (winEventThread_.joinable()) winEventThread_.join();
}

std::filesystem::path DiagnosticSession::LogPath() const { return path_; }

void CALLBACK DiagnosticSession::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
    LONG objectId, LONG childId, DWORD, DWORD)
{
    if (auto* session = active_.load()) session->RecordWinEvent(event, hwnd, objectId, childId);
}

void DiagnosticSession::RecordWinEvent(DWORD event, HWND hwnd, LONG objectId, LONG childId) noexcept
{
    if (!running_ || !hwnd || objectId != OBJID_WINDOW || childId != CHILDID_SELF) return;
    // The foreground / window-lifecycle stream is a top-level-window stream.
    // DESTROY is emitted only when pre-destroy top-level metadata was cached.
    const bool destroying = event == EVENT_OBJECT_DESTROY;
    if (!destroying && GetAncestor(hwnd, GA_ROOT) != hwnd) return;
    const char* type = event == EVENT_SYSTEM_FOREGROUND ? "foreground_change" :
        event == EVENT_OBJECT_CREATE ? "window_create" : event == EVENT_OBJECT_SHOW ? "window_show" :
        event == EVENT_OBJECT_HIDE ? "window_hide" : event == EVENT_OBJECT_DESTROY ? "window_destroy" : "window_name_change";
    DWORD pid{};
    std::string fields;
    if (destroying)
    {
        std::lock_guard lock(observedMutex_);
        const auto cached = windowCache_.find(hwnd);
        if (cached == windowCache_.end()) return;
        fields = cached->second.fields + ",\"metadataSource\":\"cached\"";
        pid = cached->second.processId;
        windowCache_.erase(cached);
    }
    else
    {
        const std::string live = WindowFields(hwnd, &pid);
        {
            std::lock_guard lock(observedMutex_);
            windowCache_[hwnd] = { pid, live };
        }
        fields = live + ",\"metadataSource\":\"live\"";
    }
    if (event == EVENT_SYSTEM_FOREGROUND)
        fields = "\"oldHwnd\":\"" + Hex(previousForeground_) + "\",\"oldPid\":" + std::to_string(previousForegroundPid_) + "," + fields;
    WriteRecord(type, fields);
    const bool usefulTopLevel = event == EVENT_SYSTEM_FOREGROUND ||
        (GetAncestor(hwnd, GA_ROOT) == hwnd && ((IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) ||
         ((event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_SHOW) && previousSteamAppId_.load() != 0)));
    if (usefulTopLevel) ObserveProcess(pid, type);
    else if ((event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_SHOW) &&
        GetAncestor(hwnd, GA_ROOT) == hwnd && pid && !IsObserved(pid) &&
        HasPositiveMicrosoftGameIdentity(pid))
        ObserveProcess(pid, "microsoft_identity");
    if (event == EVENT_SYSTEM_FOREGROUND)
    {
        MarkFirst(pid, "foreground");
        previousForeground_ = hwnd;
        previousForegroundPid_ = pid;
    }
    else if (event == EVENT_OBJECT_CREATE) MarkFirst(pid, "window_create");
    else if (event == EVENT_OBJECT_SHOW) MarkFirst(pid, "window_show");
    else if (event == EVENT_OBJECT_HIDE) MarkFirst(pid, "window_hide");
    else if (event == EVENT_OBJECT_DESTROY) MarkFirst(pid, "window_destroy");
}

void DiagnosticSession::SampleApi2ObservedPids() noexcept
{
    const HWND foreground = GetForegroundWindow();
    DWORD foregroundPid{}; if (foreground) GetWindowThreadProcessId(foreground, &foregroundPid);
    if (foregroundPid) ObserveProcess(foregroundPid, "foreground_sample");
    for (const auto pid : ObservedPids())
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
        {
            MarkExited(pid);
            continue;
        }
        CloseHandle(process);
        const auto sample = api2_.Sample(pid);
        WriteRecord("api2", "\"pid\":" + std::to_string(pid) + "," + sample.json);
        // Milestones come from the structured result aggregated across every
        // swap-chain row, never from reparsing sample.json.
        if (sample.pollSucceeded)
        {
            if (sample.swapChainCount > 0 || sample.anySwapChainAddress)
            { MarkFirst(pid, "api2_swapchain"); MarkFirst(pid, "swapchain"); }
            if (sample.anyDisplayedFpsPositive) MarkFirst(pid, "displayed_fps");
            if (sample.anyPresentedFpsPositive) MarkFirst(pid, "presented_fps");
        }
    }
}

void DiagnosticSession::ObserveProcess(DWORD processId, std::string_view reason) noexcept
{
    if (!processId || processId == GetCurrentProcessId()) return;
    try
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        const std::uint64_t processStartFileTime =
            process ? DiagnosticQueryProcessStartFileTime(process) : 0;
        const DiagProcessKey key{ processId, processStartFileTime };

        std::lock_guard lock(observedMutex_);
        if (const auto current = identityByPid_.find(processId);
            current != identityByPid_.end() && current->second == key &&
            timelines_.contains(key))
        {
            if (process) CloseHandle(process);
            return;
        }
        // Numeric PID now refers to a new process generation; force the API2
        // lifecycle to follow it and start a fresh timeline.
        api2_.Retire(processId);
        identityByPid_[processId] = key;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt_).count();
        timelines_.try_emplace(key, PidTimeline{ .firstSeenMs = elapsed });

        std::wstring image, aumid, packageFull, packageFamily;
        if (process)
        {
            image = DiagnosticQueryProcessImagePath(process);
            UINT32 size{}; if (GetApplicationUserModelId(process, &size, nullptr) == ERROR_INSUFFICIENT_BUFFER) { aumid.resize(size); if (GetApplicationUserModelId(process, &size, aumid.data()) == ERROR_SUCCESS) aumid.resize(size ? size - 1 : 0); else aumid.clear(); }
            size = 0; if (GetPackageFullName(process, &size, nullptr) == ERROR_INSUFFICIENT_BUFFER) { packageFull.resize(size); if (GetPackageFullName(process, &size, packageFull.data()) == ERROR_SUCCESS) packageFull.resize(size ? size - 1 : 0); else packageFull.clear(); }
            size = 0; if (GetPackageFamilyName(process, &size, nullptr) == ERROR_INSUFFICIENT_BUFFER) { packageFamily.resize(size); if (GetPackageFamilyName(process, &size, packageFamily.data()) == ERROR_SUCCESS) packageFamily.resize(size ? size - 1 : 0); else packageFamily.clear(); }
            CloseHandle(process);
        }
        const auto gameConfig = ProbeMicrosoftGameConfig(image, packageFull);
        WindowListQuery windows{ processId };
        EnumWindows(CollectTopLevelWindows, reinterpret_cast<LPARAM>(&windows));
        std::string initialWindows = "[";
        for (size_t index = 0; index < windows.windows.size(); ++index)
        {
            if (index) initialWindows += ',';
            initialWindows += "\"" + Hex(windows.windows[index]) + "\"";
        }
        initialWindows += ']';
        auto& timeline = timelines_.at(key);
        timeline.exe = Json(std::filesystem::path(image).filename().wstring());
        timeline.imagePath = Json(image);
        timeline.processStartFileTime = processStartFileTime;
        timeline.steamAppIdAtFirstSeen = previousSteamAppId_.load();
        timeline.microsoftGameIdentity = gameConfig.exists && gameConfig.executableMatch;
        WriteRecord("pid_observed", "\"pid\":" + std::to_string(processId) + ",\"reason\":\"" + std::string(reason) +
            "\",\"exe\":\"" + Json(std::filesystem::path(image).filename().wstring()) + "\",\"imagePath\":\"" + Json(image) + "\",\"aumid\":\"" + Json(aumid) +
            "\",\"processStartFileTime\":" + std::to_string(processStartFileTime) +
            ",\"initialTopLevelHwnds\":" + initialWindows +
            ",\"packageFullName\":\"" + Json(packageFull) + "\",\"packageFamilyName\":\"" + Json(packageFamily) +
            "\",\"microsoftGameConfigExists\":" + (gameConfig.exists ? "true" : "false") +
            ",\"microsoftGameConfigReadable\":" + (gameConfig.readable ? "true" : "false") +
            ",\"microsoftGameExecutableMatch\":" + (gameConfig.executableMatch ? "true" : "false"));
    }
    catch (...) {}
}

void DiagnosticSession::WriteSummary() noexcept
{
    try
    {
        std::lock_guard lock(observedMutex_);
        std::ofstream summary(summaryPath_, std::ios::binary | std::ios::trunc);
        for (const auto& [key, timeline] : timelines_)
        {
            const DWORD pid = key.pid;
            if (summary)
                summary << "PID " << pid << " (start " << timeline.processStartFileTime << ") " << timeline.exe << "\n"
                    << "firstSeenMs=" << timeline.firstSeenMs << " firstWindowCreateMs=" << timeline.firstWindowCreateMs
                    << " firstWindowShowMs=" << timeline.firstWindowShowMs << " firstTopGpuMs=" << timeline.firstTopGpuMs
                    << " firstApi2SwapchainMs=" << timeline.firstApi2SwapchainMs << " firstPresentedFpsMs=" << timeline.firstPresentedFpsMs
                    << " firstDisplayedFpsMs=" << timeline.firstDisplayedFpsMs << " firstForegroundMs=" << timeline.firstForegroundMs << "\n"
                    << "steamAppIdAtFirstSeen=" << timeline.steamAppIdAtFirstSeen << " microsoftGameIdentity="
                    << (timeline.microsoftGameIdentity ? "true" : "false") << " processExitObserved="
                    << (timeline.processExited ? "true" : "false") << "\n\n";
            WriteRecord("pid_summary", "\"pid\":" + std::to_string(pid) +
                ",\"firstSeenMs\":" + std::to_string(timeline.firstSeenMs) +
                ",\"firstWindowCreateMs\":" + std::to_string(timeline.firstWindowCreateMs) +
                ",\"firstWindowShowMs\":" + std::to_string(timeline.firstWindowShowMs) +
                ",\"firstForegroundMs\":" + std::to_string(timeline.firstForegroundMs) +
                ",\"firstTopGpuMs\":" + std::to_string(timeline.firstTopGpuMs) +
                ",\"firstApi2SwapchainMs\":" + std::to_string(timeline.firstApi2SwapchainMs) +
                ",\"firstSwapchainMs\":" + std::to_string(timeline.firstSwapchainMs) +
                ",\"firstDisplayedFpsMs\":" + std::to_string(timeline.firstDisplayedFpsMs) +
                ",\"firstPresentedFpsMs\":" + std::to_string(timeline.firstPresentedFpsMs) +
                ",\"lastForegroundMs\":" + std::to_string(timeline.lastForegroundMs) +
                ",\"lastRendererEvidenceMs\":" + std::to_string(timeline.lastRendererEvidenceMs) +
                ",\"lastWindowHideMs\":" + std::to_string(timeline.lastWindowHideMs) +
                ",\"lastWindowDestroyMs\":" + std::to_string(timeline.lastWindowDestroyMs) +
                ",\"steamAppIdAtFirstSeen\":" + std::to_string(timeline.steamAppIdAtFirstSeen) +
                ",\"microsoftGameIdentity\":" + (timeline.microsoftGameIdentity ? "true" : "false") +
                ",\"processExitObserved\":" + (timeline.processExited ? "true" : "false") +
                ",\"processStartFileTime\":" + std::to_string(timeline.processStartFileTime) +
                ",\"exe\":\"" + timeline.exe + "\",\"imagePath\":\"" + timeline.imagePath + "\"");
        }
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
    std::unordered_map<DWORD, CandidateWindow> windows;
    EnumWindows(CollectCandidateWindow, reinterpret_cast<LPARAM>(&windows));
    struct Ranked { DWORD pid{}; double delta{}; CandidateWindow window; std::wstring exe; };
    std::vector<Ranked> ranked;
    for (const auto& [pid, delta] : totals)
        if (const auto window = windows.find(pid); window != windows.end()) ranked.push_back({ pid, delta, window->second, ProcessExe(pid) });
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) { return left.delta > right.delta; });
    for (size_t rank = 0; rank < std::min<size_t>(ranked.size(), 5); ++rank)
    {
        ObserveProcess(ranked[rank].pid, "top_gpu");
        MarkFirst(ranked[rank].pid, "top_gpu");
        WriteRecord("top_gpu", "\"mode\":\"Raw\",\"rank\":" + std::to_string(rank + 1) + ",\"pid\":" + std::to_string(ranked[rank].pid) + ",\"exe\":\"" + Json(ranked[rank].exe) + "\",\"hwnd\":\"" + Hex(ranked[rank].window.hwnd) + "\",\"title\":\"" + Json(ranked[rank].window.title) + "\",\"gpu3dDelta\":" + std::to_string(ranked[rank].delta));
    }
    size_t parityRank{};
    for (const auto& candidate : ranked)
    {
        if (IsBlocked(candidate.exe)) continue;
        WriteRecord("top_gpu", "\"mode\":\"PresentMonParity\",\"rank\":" + std::to_string(++parityRank) + ",\"pid\":" + std::to_string(candidate.pid) + ",\"exe\":\"" + Json(candidate.exe) + "\",\"hwnd\":\"" + Hex(candidate.window.hwnd) + "\",\"title\":\"" + Json(candidate.window.title) + "\",\"gpu3dDelta\":" + std::to_string(candidate.delta));
        if (parityRank == 5) break;
    }
}

std::vector<DWORD> DiagnosticSession::ObservedPids() noexcept
{
    std::lock_guard lock(observedMutex_);
    std::vector<DWORD> result;
    result.reserve(identityByPid_.size());
    for (const auto& [pid, _] : identityByPid_) result.push_back(pid);
    return result;
}

bool DiagnosticSession::IsObserved(DWORD processId) noexcept
{
    std::lock_guard lock(observedMutex_);
    return identityByPid_.contains(processId);
}

void DiagnosticSession::MarkExited(DWORD processId) noexcept
{
    api2_.Retire(processId);
    bool firstExit = false;
    std::uint64_t startFileTime{};
    {
        std::lock_guard lock(observedMutex_);
        const auto identity = identityByPid_.find(processId);
        if (identity == identityByPid_.end()) return;
        startFileTime = identity->second.startFileTime;
        if (const auto timeline = timelines_.find(identity->second);
            timeline != timelines_.end() && !timeline->second.processExited)
        {
            timeline->second.processExited = true;
            firstExit = true;
        }
        // Drop the live index so a reused numeric PID starts fresh.
        identityByPid_.erase(identity);
    }
    if (firstExit)
        WriteRecord("process_exit", "\"pid\":" + std::to_string(processId) +
            ",\"processStartFileTime\":" + std::to_string(startFileTime));
}

void DiagnosticSession::MarkFirst(DWORD processId, std::string_view milestone) noexcept
{
    if (!processId) return;
    std::lock_guard lock(observedMutex_);
    const auto identity = identityByPid_.find(processId);
    if (identity == identityByPid_.end()) return;
    const auto found = timelines_.find(identity->second);
    if (found == timelines_.end()) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt_).count();
    auto& timeline = found->second;
    auto set = [elapsed](std::int64_t& value) { if (value < 0) value = elapsed; };
    if (milestone == "window_create") set(timeline.firstWindowCreateMs);
    else if (milestone == "window_show") set(timeline.firstWindowShowMs);
    else if (milestone == "foreground") { set(timeline.firstForegroundMs); timeline.lastForegroundMs = elapsed; }
    else if (milestone == "top_gpu") set(timeline.firstTopGpuMs);
    else if (milestone == "api2_swapchain") { set(timeline.firstApi2SwapchainMs); timeline.lastRendererEvidenceMs = elapsed; }
    else if (milestone == "swapchain") set(timeline.firstSwapchainMs);
    else if (milestone == "displayed_fps") { set(timeline.firstDisplayedFpsMs); timeline.lastRendererEvidenceMs = elapsed; }
    else if (milestone == "presented_fps") { set(timeline.firstPresentedFpsMs); timeline.lastRendererEvidenceMs = elapsed; }
    else if (milestone == "window_hide") timeline.lastWindowHideMs = elapsed;
    else if (milestone == "window_destroy") timeline.lastWindowDestroyMs = elapsed;
}

void DiagnosticSession::WatchSteamRunningAppId() noexcept
{
    HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!changed) return;
    while (running_)
    {
        HKEY key = OpenSteamWatchKey();
        if (!key) { Sleep(250); continue; }
        if (RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET,
            changed, TRUE) != ERROR_SUCCESS)
        { RegCloseKey(key); break; }
        while (running_ && WaitForSingleObject(changed, 100) == WAIT_TIMEOUT) {}
        RegCloseKey(key);
        if (!running_) break;
        const auto appId = ReadSteamAppId();
        const auto old = previousSteamAppId_.exchange(appId);
        if (appId != old)
        {
            WriteRecord("steam_running_app_id", "\"oldAppId\":" + std::to_string(old) +
                ",\"appId\":" + std::to_string(appId));
        }
    }
    CloseHandle(changed);
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
        TIME_ZONE_INFORMATION timezone{};
        const DWORD zone = GetTimeZoneInformation(&timezone);
        const LONG bias = timezone.Bias + (zone == TIME_ZONE_ID_DAYLIGHT ? timezone.DaylightBias :
            zone == TIME_ZONE_ID_STANDARD ? timezone.StandardBias : 0);
        const LONG offset = -bias;
        char time[48]{}; sprintf_s(time, "%04u-%02u-%02uT%02u:%02u:%02u.%03u%c%02ld:%02ld",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
            offset < 0 ? '-' : '+', std::abs(offset) / 60, std::abs(offset) % 60);
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
    std::wstring imagePath, exe;
    if (pid)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process)
        {
            imagePath = DiagnosticQueryProcessImagePath(process);
            CloseHandle(process);
            if (!imagePath.empty()) exe = std::filesystem::path(imagePath).filename().wstring();
        }
    }
    wchar_t title[1024]{}, className[256]{}; GetWindowTextW(hwnd, title, 1024); GetClassNameW(hwnd, className, 256);
    RECT rect{}; const bool rectAvailable = SUCCEEDED(DwmGetWindowAttribute(hwnd,
        DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))) || GetWindowRect(hwnd, &rect);
    MONITORINFO monitor{ sizeof(monitor) }; const HMONITOR handle = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    const bool monitorAvailable = handle && GetMonitorInfoW(handle, &monitor);
    DWORD cloaked{}; const bool cloakedAvailable = SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)));
    std::ostringstream fields;
    fields << "\"pid\":" << pid << ",\"hwnd\":\"" << Hex(hwnd) << "\",\"exe\":\"" << Json(exe)
           << "\",\"imagePath\":\"" << Json(imagePath) << "\",\"topLevel\":" << (GetAncestor(hwnd, GA_ROOT) == hwnd ? "true" : "false")
           << ",\"title\":\"" << Json(title)
           << "\",\"class\":\"" << Json(className) << "\",\"visible\":" << (IsWindowVisible(hwnd) ? "true" : "false")
           << ",\"minimized\":" << (IsIconic(hwnd) ? "true" : "false") << ",\"owner\":\"" << Hex(GetWindow(hwnd, GW_OWNER)) << "\""
           << ",\"style\":\"0x" << std::hex << GetWindowLongPtrW(hwnd, GWL_STYLE) << std::dec << "\",\"exStyle\":\"0x" << std::hex << GetWindowLongPtrW(hwnd, GWL_EXSTYLE) << std::dec << "\"";
    fields << ",\"cloaked\":" << (cloakedAvailable ? (cloaked ? "true" : "false") : "null");
    if (rectAvailable)
        fields << ",\"rect\":\"" << rect.left << ',' << rect.top << ',' << rect.right << ',' << rect.bottom << "\""
               << ",\"windowWidth\":" << (rect.right - rect.left) << ",\"windowHeight\":" << (rect.bottom - rect.top);
    else
        fields << ",\"rect\":null,\"windowWidth\":null,\"windowHeight\":null";
    if (monitorAvailable)
    {
        const auto same = [](LONG left, LONG right) { return std::abs(left - right) <= 2; };
        const bool fullscreen = rectAvailable && same(rect.left, monitor.rcMonitor.left) && same(rect.top, monitor.rcMonitor.top) && same(rect.right, monitor.rcMonitor.right) && same(rect.bottom, monitor.rcMonitor.bottom);
        fields << ",\"monitorRect\":\"" << monitor.rcMonitor.left << ',' << monitor.rcMonitor.top << ',' << monitor.rcMonitor.right << ',' << monitor.rcMonitor.bottom << "\",\"monitorWorkRect\":\"" << monitor.rcWork.left << ',' << monitor.rcWork.top << ',' << monitor.rcWork.right << ',' << monitor.rcWork.bottom << "\",\"fullscreenLike\":" << (fullscreen ? "true" : "false");
    }
    else
        fields << ",\"monitorRect\":null,\"monitorWorkRect\":null,\"fullscreenLike\":null";
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
