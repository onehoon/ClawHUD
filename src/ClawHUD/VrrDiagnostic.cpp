#include "VrrDiagnostic.h"

#include "App.h"
#include "IntelVrrDiagnosticProbe.h"
#include "ProductionTargetPolicy.h"
#include "RuntimeLogger.h"
#include "VrrDiagnosticAnalysis.h"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <evntrace.h>
#include <mmsystem.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace
{
using Microsoft::WRL::ComPtr;
constexpr auto kCaptureDuration = std::chrono::seconds(28);
constexpr auto kDiagnosticCaptureDuration = std::chrono::seconds(92);
constexpr auto kCaptureWatchdog = std::chrono::seconds(100);
constexpr auto kGracefulStopWait = std::chrono::seconds(5);
constexpr std::size_t kMaximumPresentMonOutputBytes = 64u * 1024u;

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

struct TraceStopProperties
{
    EVENT_TRACE_PROPERTIES properties{};
    wchar_t loggerName[128]{};
};

void StopTraceSession(const std::wstring& sessionName) noexcept
{
    if (sessionName.empty()) return;
    TraceStopProperties buffer{};
    buffer.properties.Wnode.BufferSize = sizeof(buffer);
    buffer.properties.LoggerNameOffset =
        static_cast<ULONG>(offsetof(TraceStopProperties, loggerName));
    (void)ControlTraceW(0, sessionName.c_str(), &buffer.properties,
        EVENT_TRACE_CONTROL_STOP);
}

struct ForegroundTargetInfo
{
    HWND window{};
    DWORD processId{};
    std::wstring processPath;
};

std::optional<ForegroundTargetInfo> CaptureForegroundTarget()
{
    const HWND window = GetForegroundWindow(); if (!window) return std::nullopt;
    DWORD processId{}; GetWindowThreadProcessId(window, &processId);
    if (!processId || processId == GetCurrentProcessId()) return std::nullopt;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId); if (!process) return std::nullopt;
    wchar_t path[MAX_PATH]{}; DWORD size = ARRAYSIZE(path);
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE; CloseHandle(process);
    if (!ok) return std::nullopt;
    const std::wstring processPath(path, size);
    std::wstring name = processPath.substr(processPath.find_last_of(L"\\/") + 1);
    std::transform(name.begin(), name.end(), name.begin(), towlower);
    if (name == L"clawhud.exe" || name == L"clawhud.echelper.exe" ||
        clawhud::IsRejectedProductionTargetImage(name))
        return std::nullopt;
    return ForegroundTargetInfo{ window, processId, processPath };
}

void PlayDiagnosticSound(bool completed) noexcept
{
    const LPCWSTR alias = MAKEINTRESOURCEW(static_cast<WORD>(
        completed ? SND_ALIAS_SYSTEMDEFAULT : SND_ALIAS_SYSTEMASTERISK));
    PlaySoundW(alias, nullptr, SND_ALIAS_ID | SND_ASYNC | SND_NODEFAULT);
}

bool Alive(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!process) return false;
    DWORD code{}; const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE; CloseHandle(process); return alive;
}

void WriteSummary(std::wofstream& log, const wchar_t* phase, const std::filesystem::path& csv, const clawhud::VrrCsvSummary& summary)
{
    log << L"=== " << phase << L" SUMMARY ===\nCSV: " << csv.wstring() << L"\n";
    if (!summary.valid) { log << L"Summary: unavailable\nReason: " << std::wstring(summary.reason.begin(), summary.reason.end()) << L"\n"; return; }
    if (summary.hasCoverageRange)
        log << L"QPC coverage: " << summary.coverageMs << L" ms ("
            << summary.coverageRatio * 100.0 << L"%)\nCoverage sufficient: "
            << (summary.sufficientCoverage ? L"YES" : L"NO") << L"\n";
    log << L"Rows: " << summary.rows << L"\nDominant SwapChain: " << std::wstring(summary.dominantSwapChain.begin(), summary.dominantSwapChain.end())
        << L"\nDominant Rows: " << summary.dominantRows << L"\nUsable PresentMode samples: " << summary.presentModeSamples
        << L"\nPresentMode distribution:\n";
    for (const auto& [mode, count] : summary.modes)
    {
        const double percentage = summary.modes.empty() ? 0.0 :
            100.0 * static_cast<double>(count) /
            static_cast<double>(std::accumulate(summary.modes.begin(), summary.modes.end(), std::size_t{},
                [](std::size_t total, const auto& item) { return total + item.second; }));
        log << L"  " << std::wstring(mode.begin(), mode.end()) << L": " << count
            << L" (" << std::fixed << std::setprecision(1) << percentage << L"%)\n";
    }
    const auto independentFlip = clawhud::IndependentFlipPercentageIfAvailable(summary);
    log << L"Independent Flip: " << std::fixed << std::setprecision(1)
        << (independentFlip ? std::to_wstring(*independentFlip) + L"%" : L"Unavailable") << L"\n";
    log << L"Average MsBetweenPresents: " << summary.presentAverage << L"\nAverage MsBetweenDisplayChange: " << summary.displayAverage
        << L"\nMin MsBetweenDisplayChange: " << summary.displayMin << L"\nMax MsBetweenDisplayChange: " << summary.displayMax
        << L"\nDisplayed frames: " << summary.displayed << L"\nNot-displayed frames: " << summary.notDisplayed << L"\n";
}

void LogMpoCapability(std::wofstream& log)
{
    log << L"=== MPO / HARDWARE COMPOSITION CAPABILITY ===\n"
        << L"Capability evidence only; this does not prove runtime MPO usage.\n";
    ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL level{};
    const HRESULT deviceHr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, &level, nullptr);
    if (FAILED(deviceHr))
    {
        log << L"DXGI capability: Unavailable\nHRESULT: 0x" << std::hex
            << static_cast<unsigned long>(deviceHr) << std::dec << L"\n"
            << L"D3DKMT MPO plane caps: unavailable / deferred\n\n";
        return;
    }
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIOutput> output;
    if (FAILED(device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)))
    {
        log << L"DXGI output: Unavailable\nD3DKMT MPO plane caps: unavailable / deferred\n\n";
        return;
    }

    const HMONITOR primaryMonitor = MonitorFromPoint(
        POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    DXGI_OUTPUT_DESC outputDescription{};
    for (UINT index = 0; ; ++index)
    {
        ComPtr<IDXGIOutput> candidate;
        const HRESULT enumHr = adapter->EnumOutputs(index, &candidate);
        if (enumHr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(enumHr)) break;
        DXGI_OUTPUT_DESC candidateDescription{};
        if (SUCCEEDED(candidate->GetDesc(&candidateDescription)) &&
            candidateDescription.Monitor == primaryMonitor)
        {
            output = candidate;
            outputDescription = candidateDescription;
            break;
        }
    }
    if (!output)
    {
        log << L"DXGI primary output: Unavailable\n"
            << L"D3DKMT MPO plane caps: unavailable / deferred\n\n";
        return;
    }
    log << L"DXGI primary output: " << outputDescription.DeviceName << L"\n";

    ComPtr<IDXGIOutput3> output3;
    if (SUCCEEDED(output.As(&output3)))
    {
        UINT flags{};
        const HRESULT hr = output3->CheckOverlaySupport(DXGI_FORMAT_B8G8R8A8_UNORM,
            device.Get(), &flags);
        if (SUCCEEDED(hr))
        {
            log << L"DXGI Overlay Support (BGRA8):\n"
                << L"  DIRECT: " << ((flags & DXGI_OVERLAY_SUPPORT_FLAG_DIRECT) ? L"YES" : L"NO") << L"\n"
                << L"  SCALING: " << ((flags & DXGI_OVERLAY_SUPPORT_FLAG_SCALING) ? L"YES" : L"NO") << L"\n"
                << L"  Raw: 0x" << std::hex << static_cast<unsigned long>(flags) << std::dec << L"\n";
        }
        else
        {
            log << L"DXGI Overlay Support (BGRA8): Unavailable\nHRESULT: 0x" << std::hex
                << static_cast<unsigned long>(hr) << std::dec << L"\n";
        }
    }
    else
        log << L"DXGI Overlay Support (BGRA8): Unavailable\n";

    ComPtr<IDXGIOutput6> output6;
    if (SUCCEEDED(output.As(&output6)))
    {
        UINT flags{};
        const HRESULT hr = output6->CheckHardwareCompositionSupport(&flags);
        if (SUCCEEDED(hr))
        {
            log << L"DXGI Hardware Composition Support:\n"
                << L"  FULLSCREEN: " << ((flags & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_FULLSCREEN) ? L"YES" : L"NO") << L"\n"
                << L"  WINDOWED: " << ((flags & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_WINDOWED) ? L"YES" : L"NO") << L"\n"
                << L"  CURSOR_STRETCHED: " << ((flags & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_CURSOR_STRETCHED) ? L"YES" : L"NO") << L"\n"
                << L"  Raw: 0x" << std::hex << static_cast<unsigned long>(flags) << std::dec << L"\n";
        }
        else
            log << L"DXGI Hardware Composition Support: Unavailable\nHRESULT: 0x" << std::hex
                << static_cast<unsigned long>(hr) << std::dec << L"\n";
    }
    else
        log << L"DXGI Hardware Composition Support: Unavailable\n";
    log << L"D3DKMT MPO plane caps: unavailable / deferred\n\n";
}

double QpcMilliseconds()
{
    LARGE_INTEGER counter{}, frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart == 0 ? 0.0 :
        1000.0 * static_cast<double>(counter.QuadPart) /
        static_cast<double>(frequency.QuadPart);
}

struct PresentMonCapture
{
    PROCESS_INFORMATION process{};
    HANDLE outputRead{};
    std::thread outputReader;
    std::string output;
    std::wstring command;
    std::wstring sessionName;
    std::chrono::steady_clock::time_point startedAt{};
};

bool StartPresentMon(const std::filesystem::path& executable, DWORD pid,
    const std::filesystem::path& csv, const std::string& session,
    PresentMonCapture& capture)
{
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE outputWrite{};
    if (!CreatePipe(&capture.outputRead, &outputWrite, &security, 0)) return false;
    SetHandleInformation(capture.outputRead, HANDLE_FLAG_INHERIT, 0);
    capture.command = L"\"" + executable.wstring() + L"\" --process_id " + std::to_wstring(pid) +
        L" --output_file \"" + csv.wstring() + L"\" --timed " +
        std::to_wstring(kDiagnosticCaptureDuration.count()) +
        L" --terminate_after_timed --no_console_stats --qpc_time_ms --session_name " +
        std::wstring(session.begin(), session.end());
    capture.sessionName.assign(session.begin(), session.end());
    std::vector<wchar_t> line(capture.command.begin(), capture.command.end());
    line.push_back(L'\0');
    STARTUPINFOW startup{ sizeof(startup) };
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = outputWrite;
    startup.hStdError = outputWrite;
    const bool started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &capture.process) != FALSE;
    CloseHandle(outputWrite);
    if (!started)
    {
        CloseHandle(capture.outputRead);
        capture.outputRead = nullptr;
        return false;
    }
    capture.startedAt = std::chrono::steady_clock::now();
    capture.outputReader = std::thread([&capture]
    {
        char buffer[4096]; DWORD read{};
        while (ReadFile(capture.outputRead, buffer, sizeof(buffer), &read, nullptr) && read)
        {
            if (capture.output.size() < kMaximumPresentMonOutputBytes)
                capture.output.append(buffer, std::min<std::size_t>(read,
                    kMaximumPresentMonOutputBytes - capture.output.size()));
        }
    });
    return true;
}

bool StopPresentMon(PresentMonCapture& capture, bool stop, bool terminateEarly,
    std::wofstream& log,
    const std::filesystem::path& csv)
{
    bool timeout = false;
    if (terminateEarly && !stop)
        StopTraceSession(capture.sessionName);
    const auto deadline = capture.startedAt + kCaptureWatchdog;
    const auto gracefulDeadline = std::chrono::steady_clock::now() + kGracefulStopWait;
    while (WaitForSingleObject(capture.process.hProcess, 100) != WAIT_OBJECT_0)
    {
        const auto now = std::chrono::steady_clock::now();
        if (stop || now >= deadline || (terminateEarly && now >= gracefulDeadline))
        {
            timeout = !stop && now >= deadline;
            TerminateProcess(capture.process.hProcess, stop ? 2 : 3);
            WaitForSingleObject(capture.process.hProcess, 5000);
            break;
        }
    }
    DWORD code = STILL_ACTIVE;
    GetExitCodeProcess(capture.process.hProcess, &code);
    CloseHandle(capture.process.hThread);
    CloseHandle(capture.process.hProcess);
    if (capture.outputReader.joinable()) capture.outputReader.join();
    CloseHandle(capture.outputRead);
    capture.outputRead = nullptr;
    std::error_code error;
    const bool csvExists = std::filesystem::exists(csv, error) && !error;
    const auto csvSize = csvExists ? std::filesystem::file_size(csv, error) : 0;
    log << L"=== PRESENTMON CAPTURE ===\nCommand: " << capture.command
        << L"\nPresentMon PID: " << capture.process.dwProcessId
        << L"\nExit Code: " << code << L"\nCSV Created: " << (csvExists ? L"YES" : L"NO")
        << L"\nCSV Size: " << (error ? 0 : csvSize) << L"\nStop Time: " << Now() << L"\n"
        << L"Captured stdout/stderr bytes: " << capture.output.size() << L"\n";
    if (!capture.output.empty())
        log << L"=== PRESENTMON OUTPUT ===\n" << std::wstring(capture.output.begin(), capture.output.end()) << L"\n";
    if (stop) log << L"PresentMon: Cancelled\n";
    else if (terminateEarly) log << L"PresentMon: gracefully stopped after diagnostic phases\n";
    if (timeout) log << L"PresentMon: watchdog timeout\n";
    return !stop && !timeout && code == 0 && csvExists && csvSize != 0;
}
}

bool VrrDiagnostic::Start()
{
    if (running_.exchange(true)) return false;
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard lock(triggerMutex_);
        targetPid_.reset();
        targetWindow_ = nullptr;
        targetPath_.clear();
    }
    savedHudState_ = app_.CaptureHudVisibilityState();
    hudStateSaved_ = true;
    stop_ = false;
    state_ = VrrDiagnosticState::WaitingForTrigger;
    try { worker_ = std::thread(&VrrDiagnostic::Run, this); }
    catch (...) { hudStateSaved_ = false; state_ = VrrDiagnosticState::Idle; running_ = false; throw; }
    Status(L"Waiting for F8"); return true;
}

void VrrDiagnostic::Stop()
{
    stop_ = true;
    triggerCondition_.notify_all();
    app_.CancelPendingHudVisibilityRequests();
    if (worker_.joinable()) worker_.join();
    if (hudStateSaved_)
    {
        if (app_.RestoreHudVisibilityState(savedHudState_))
            hudStateSaved_ = false;
        else
            OutputDebugStringW(L"[ClawHUD] Failed to restore HUD state while stopping VRR diagnostic\n");
    }
    {
        std::lock_guard lock(triggerMutex_);
        targetPid_.reset();
        targetWindow_ = nullptr;
        targetPath_.clear();
    }
    state_ = VrrDiagnosticState::Idle;
    running_ = false;
}

void VrrDiagnostic::Run()
{
    DWORD targetPid{};
    HWND foregroundWindow{};
    std::wstring targetPath;
    {
        std::unique_lock lock(triggerMutex_);
        triggerCondition_.wait(lock, [this]
        {
            return stop_.load() || targetPid_.has_value();
        });
        if (stop_)
        {
            if (VrrDiagnosticStopReportsCancellation(state_.load()))
                Status(L"Cancelled");
            state_ = VrrDiagnosticState::Idle;
            running_ = false;
            return;
        }
        targetPid = *targetPid_;
        foregroundWindow = targetWindow_;
        targetPath = targetPath_;
    }
    const bool completed = RunImpl(targetPid, foregroundWindow, targetPath);
    bool restoreSucceeded = true;
    if (!stop_.load() && hudStateSaved_)
    {
        restoreSucceeded = app_.RequestDiagnosticHudState(savedHudState_);
        if (!restoreSucceeded)
            OutputDebugStringW(L"[ClawHUD] Failed to restore HUD state after VRR diagnostic\n");
        else if (!stop_.load())
            hudStateSaved_ = false;
    }
    if (!stop_.load() && VrrDiagnosticCompletionSoundAllowed(completed, restoreSucceeded))
        PlayDiagnosticSound(true);
    state_ = VrrDiagnosticState::Idle;
    running_ = false;
}

bool VrrDiagnostic::TriggerFromForeground()
{
    if (!WaitingForTrigger()) return false;
    const auto target = CaptureForegroundTarget();
    if (!target)
    {
        OutputDebugStringW(L"[ClawHUD] VRR diagnostic trigger ignored: no valid foreground target\n");
        return false;
    }
    {
        std::lock_guard lock(triggerMutex_);
        if (stop_ || state_.load() != VrrDiagnosticState::WaitingForTrigger)
            return false;
        targetPid_ = target->processId;
        targetWindow_ = target->window;
        targetPath_ = target->processPath;
        state_ = VrrDiagnosticState::Running;
    }
    triggerCondition_.notify_one();
    PlayDiagnosticSound(false);
    return true;
}

void VrrDiagnostic::Status(const wchar_t* text) const
{
    if (!notifyWindow_) return; auto* value = new std::wstring(text);
    if (!PostMessageW(notifyWindow_, kVrrDiagnosticStatus, reinterpret_cast<WPARAM>(value), 0)) delete value;
}

bool VrrDiagnostic::RunImpl(DWORD targetPid, HWND foregroundWindow,
    const std::wstring& targetPath)
{
    std::wofstream log;
    clawhud::IntelVrrDiagnosticProbe igcl;
    try
    {
        const auto folder = clawhud::LogDirectory();
        const auto stamp = Now(true);
        const auto txt = folder / (L"vrr-" + stamp + L".txt");
        const auto csv = folder / (L"vrr-" + stamp + L".csv");
        log.open(txt);
        if (!log.is_open()) { Status(L"Failed"); return false; }
        log << L"=== CLAWHUD VRR DIAGNOSTIC ===\nTimestamp: " << Now() << L"\nPresentMon: 2.5.1\n"
            << L"PresentMon lifecycle: one process for OFF / STATIC / DYNAMIC\n"
            << L"Display tracking: enabled (no --no_track_display)\n"
            << L"VRR verdict is authoritative only when all phase captures meet coverage requirements.\n"
            << L"NOTE: PresentMon captures application presents and OS-visible display timing.\n"
            << L"Intel UMD XeFG-generated output frames may not all be observable here.\n"
            << L"Do not treat this capture as authoritative true XeFG displayed FPS.\n\n";
        LogMpoCapability(log);
        igcl.Initialize(log);
        igcl.LogState(log);
        const auto pm = std::filesystem::path(app_.ExecutablePath()).parent_path() / L"tools" / L"PresentMon.exe";
        if (!std::filesystem::exists(pm))
        {
            log << L"PresentMon: FAILED\nReason: tools\\PresentMon.exe not found\n";
            Status(L"Failed"); return false;
        }
        log << L"=== VRR DIAGNOSTIC TRIGGER ===\nTrigger: F8\nForeground HWND: "
            << reinterpret_cast<const void*>(foregroundWindow) << L"\nTarget PID: " << targetPid
            << L"\nTarget Process: " << targetPath << L"\nTarget Alive: "
            << (Alive(targetPid) ? L"YES" : L"NO") << L"\n\n";

        log << L"=== HUD OFF SETUP ===\n";
        if (!app_.RequestDiagnosticHudMode(DiagnosticHudMode::Off) ||
            !app_.RequestDiagnosticHudVisibilityMatches(false))
        {
            log << L"Main HUD OFF setup: FAILED\n";
            Status(L"Failed"); return false;
        }
        log << L"Main HUD OFF setup: confirmed\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (stop_)
        {
            log << L"RESULT: Cancelled\n"; Status(L"Cancelled"); return false;
        }

        const std::string session = "ClawHUD-VRR-" + std::to_string(targetPid) + "-" + Narrow(stamp);
        PresentMonCapture capture;
        log << L"PresentMon Start Time: " << Now() << L"\nCSV: " << csv.wstring() << L"\n"
            << L"PresentMon Session: " << std::wstring(session.begin(), session.end()) << L"\n";
        if (!StartPresentMon(pm, targetPid, csv, session, capture))
        {
            log << L"PresentMon: FAILED\nReason: CreateProcess or stdout pipe setup failed\n";
            Status(L"Failed"); return false;
        }

        struct PhaseRange { DiagnosticHudMode mode{}; double beginQpcMs{}; double endQpcMs{}; };
        struct PhaseResult
        {
            clawhud::VrrCsvSummary csv;
            std::vector<clawhud::VblankSummary> vblank;
            PhaseRange range;
            bool targetAlive{};
            bool hudVisible{};
            bool phaseRan{};
        };
        auto waitPhase = [&]()
        {
            const auto deadline = std::chrono::steady_clock::now() + kCaptureDuration;
            while (!stop_ && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return !stop_;
        };
        auto runPhase = [&](const wchar_t* title, const wchar_t* status,
            DiagnosticHudMode mode, PhaseResult& result) -> bool
        {
            log << L"=== " << title << L" ===\nStart: " << Now() << L"\n";
            Status(status);
            const bool expectedVisible = mode != DiagnosticHudMode::Off;
            if (mode != DiagnosticHudMode::Off)
            {
                if (!app_.RequestDiagnosticHudMode(mode))
                {
                    log << L"Main HUD mode request: FAILED\n"; return false;
                }
                result.hudVisible = app_.RequestDiagnosticHudVisibilityMatches(expectedVisible);
                if (!result.hudVisible)
                {
                    log << L"Main HUD visibility: FAILED\n"; return false;
                }
                log << L"Transition exclusion: 1000 ms\n";
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            else
                result.hudVisible = app_.RequestDiagnosticHudVisibilityMatches(false);
            if (!result.hudVisible)
            {
                log << L"Main HUD visibility: FAILED\n"; return false;
            }
            log << L"Main HUD mode: confirmed\n";
            if (stop_) return false;
            result.range.mode = mode;
            result.range.beginQpcMs = QpcMilliseconds();
            igcl.StartSampling();
            const bool completed = waitPhase();
            result.range.endQpcMs = QpcMilliseconds();
            result.vblank = igcl.StopSampling(log, title);
            result.targetAlive = Alive(targetPid);
            result.hudVisible = app_.RequestDiagnosticHudVisibilityMatches(expectedVisible);
            result.phaseRan = completed;
            log << L"QPC range: " << result.range.beginQpcMs << L" - "
                << result.range.endQpcMs << L" ms\nEnd: " << Now() << L"\n";
            if (!result.targetAlive) log << L"Reason: Target process exited\n";
            if (!result.hudVisible) log << L"Reason: Main HUD visibility did not match phase\n";
            return completed && result.targetAlive && result.hudVisible;
        };

        PhaseResult off, staticHud, dynamicHud;
        const bool offOk = runPhase(L"PHASE A - HUD OFF", L"HUD OFF", DiagnosticHudMode::Off, off);
        const bool staticOk = offOk && runPhase(L"PHASE B - STATIC HUD", L"STATIC HUD", DiagnosticHudMode::Static, staticHud);
        const bool dynamicOk = staticOk && runPhase(L"PHASE C - DYNAMIC HUD", L"DYNAMIC HUD", DiagnosticHudMode::Dynamic, dynamicHud);
        const bool pmOk = StopPresentMon(capture, stop_, !dynamicOk, log, csv);
        if (stop_)
        {
            log << L"RESULT: Cancelled\n"; Status(L"Cancelled"); return false;
        }

        std::ifstream csvFile(csv, std::ios::binary);
        std::ostringstream csvText;
        if (csvFile.is_open()) csvText << csvFile.rdbuf();
        const std::string csvContents = csvText.str();
        const auto parsePhase = [&](const PhaseResult& phase, std::string_view preferred)
        {
            if (!phase.phaseRan)
            {
                clawhud::VrrCsvSummary unavailable;
                unavailable.reason = "Phase did not run";
                return unavailable;
            }
            return clawhud::ParseVrrCsvText(csvContents, preferred,
                phase.range.beginQpcMs, phase.range.endQpcMs);
        };
        off.csv = parsePhase(off, {});
        staticHud.csv = parsePhase(staticHud, off.csv.dominantSwapChain);
        dynamicHud.csv = parsePhase(dynamicHud, off.csv.dominantSwapChain);
        WriteSummary(log, L"PHASE A - HUD OFF", csv, off.csv);
        WriteSummary(log, L"PHASE B - STATIC HUD", csv, staticHud.csv);
        WriteSummary(log, L"PHASE C - DYNAMIC HUD", csv, dynamicHud.csv);
        log << L"=== PHASE RANGES ===\nOFF: " << off.range.beginQpcMs << L" - " << off.range.endQpcMs
            << L" ms\nSTATIC: " << staticHud.range.beginQpcMs << L" - " << staticHud.range.endQpcMs
            << L" ms\nDYNAMIC: " << dynamicHud.range.beginQpcMs << L" - " << dynamicHud.range.endQpcMs << L" ms\n\n";

        const auto evaluation = clawhud::EvaluateVrrComparison(off.csv, staticHud.csv, dynamicHud.csv);
        const bool phaseOk = pmOk && offOk && staticOk && dynamicOk && off.csv.sufficientCoverage &&
            staticHud.csv.sufficientCoverage && dynamicHud.csv.sufficientCoverage;
        const auto finalVerdict = phaseOk ? evaluation.verdict : clawhud::VrrDiagnosticVerdict::Inconclusive;
        const std::string finalReason = phaseOk ? evaluation.reason :
            "PresentMon capture did not provide sufficient samples for every diagnostic phase.";
        const auto writeVerdictPhase = [&](const wchar_t* name, const clawhud::VrrCsvSummary& summary)
        {
            const auto percentage = clawhud::IndependentFlipPercentageIfAvailable(summary);
            const auto dominantMode = clawhud::DominantPresentMode(summary);
            log << name << L"\nIndependent Flip: "
                << (percentage ? std::to_wstring(*percentage) + L"%" : L"Unavailable")
                << L"\nDominant PresentMode: "
                << (dominantMode.empty() ? L"Unavailable" : std::wstring(dominantMode.begin(), dominantMode.end())) << L"\n";
        };
        log << L"=== VRR VERDICT ===\nDiagnostic Status: " << (phaseOk ? L"COMPLETE" : L"FAILED")
            << L"\nVRR Verdict: " << clawhud::VrrDiagnosticVerdictName(finalVerdict)
            << L"\nReason: " << std::wstring(finalReason.begin(), finalReason.end()) << L"\n";
        writeVerdictPhase(L"HUD OFF", off.csv);
        writeVerdictPhase(L"STATIC HUD", staticHud.csv);
        writeVerdictPhase(L"DYNAMIC HUD", dynamicHud.csv);
        log << L"Supporting evidence: IGCL VBlank timing is not an authoritative VRR-active signal.\n"
            << L"Result: " << clawhud::VrrDiagnosticVerdictName(finalVerdict) << L"\n";
        Status(phaseOk ? (finalVerdict == clawhud::VrrDiagnosticVerdict::Pass ? L"Passed" : L"Failed") : L"Failed");
        return phaseOk;
    }
    catch (...) { if (log.is_open()) log << L"RESULT: Failed\n"; Status(L"Failed"); return false; }
}
