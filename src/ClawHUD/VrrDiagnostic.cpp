#include "VrrDiagnostic.h"

#include "App.h"
#include "IntelVrrDiagnosticProbe.h"
#include "ProductionTargetPolicy.h"
#include "RuntimeLogger.h"
#include "VrrDiagnosticAnalysis.h"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
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
constexpr auto kCaptureWatchdog = std::chrono::seconds(35);

std::wstring Now(bool fileName = false)
{
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &time);
    std::wstringstream out;
    out << std::put_time(&local, fileName ? L"%Y%m%d-%H%M%S" : L"%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string Narrow(const std::wstring& value)
{
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value)
        result.push_back(static_cast<char>(character));
    return result;
}

struct ForegroundTargetInfo
{
    HWND window{};
    DWORD processId{};
    std::wstring processPath;
};

std::optional<ForegroundTargetInfo> CaptureForegroundTarget()
{
    const HWND window = GetForegroundWindow();
    if (!window) return std::nullopt;

    DWORD processId{};
    GetWindowThreadProcessId(window, &processId);
    if (!processId || processId == GetCurrentProcessId()) return std::nullopt;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return std::nullopt;

    wchar_t path[MAX_PATH]{};
    DWORD size = ARRAYSIZE(path);
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
    CloseHandle(process);
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
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    DWORD code{};
    const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}

void WriteSummary(std::wofstream& log, const wchar_t* phase,
    const std::filesystem::path& csv, const clawhud::VrrCsvSummary& summary)
{
    log << L"=== " << phase << L" SUMMARY ===\nCSV: " << csv.wstring() << L"\n";
    if (!summary.valid)
    {
        log << L"Summary: unavailable\nReason: "
            << std::wstring(summary.reason.begin(), summary.reason.end()) << L"\n";
        return;
    }

    if (summary.hasCoverageRange)
    {
        log << L"QPC coverage: " << summary.coverageMs << L" ms ("
            << summary.coverageRatio * 100.0 << L"%)\nCoverage sufficient: "
            << (summary.sufficientCoverage ? L"YES" : L"NO") << L"\n";
    }

    log << L"Rows: " << summary.rows
        << L"\nDominant SwapChain: "
        << std::wstring(summary.dominantSwapChain.begin(), summary.dominantSwapChain.end())
        << L"\nDominant Rows: " << summary.dominantRows
        << L"\nUsable PresentMode samples: " << summary.presentModeSamples
        << L"\nPresentMode distribution:\n";

    const auto totalModes = std::accumulate(summary.modes.begin(), summary.modes.end(), std::size_t{},
        [](std::size_t total, const auto& item) { return total + item.second; });
    for (const auto& [mode, count] : summary.modes)
    {
        const double percentage = totalModes == 0 ? 0.0 :
            100.0 * static_cast<double>(count) / static_cast<double>(totalModes);
        log << L"  " << std::wstring(mode.begin(), mode.end()) << L": " << count
            << L" (" << std::fixed << std::setprecision(1) << percentage << L"%)\n";
    }

    const auto independentFlip = clawhud::IndependentFlipPercentageIfAvailable(summary);
    log << L"Independent Flip: "
        << (independentFlip ? std::to_wstring(*independentFlip) + L"%" : L"Unavailable")
        << L"\nAverage MsBetweenPresents: " << summary.presentAverage
        << L"\nAverage MsBetweenDisplayChange: " << summary.displayAverage
        << L"\nMin MsBetweenDisplayChange: " << summary.displayMin
        << L"\nMax MsBetweenDisplayChange: " << summary.displayMax
        << L"\nDisplayed frames: " << summary.displayed
        << L"\nNot-displayed frames: " << summary.notDisplayed << L"\n";
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

    const HMONITOR primaryMonitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
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
        const HRESULT hr = output3->CheckOverlaySupport(
            DXGI_FORMAT_B8G8R8A8_UNORM, device.Get(), &flags);
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
    {
        log << L"DXGI Overlay Support (BGRA8): Unavailable\n";
    }

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
        {
            log << L"DXGI Hardware Composition Support: Unavailable\nHRESULT: 0x" << std::hex
                << static_cast<unsigned long>(hr) << std::dec << L"\n";
        }
    }
    else
    {
        log << L"DXGI Hardware Composition Support: Unavailable\n";
    }

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

enum class PresentMonLaunchMode
{
    Standard,
    Elevated,
};

struct PresentMonCaptureResult
{
    bool ok{};
    bool csvCreated{};
    DWORD exitCode{};
    PresentMonLaunchMode launchMode{ PresentMonLaunchMode::Standard };
    double beginQpcMs{};
    double endQpcMs{};
};

std::wstring PresentMonParameters(DWORD pid, const std::filesystem::path& csv,
    const std::string& session)
{
    return L"--process_id " + std::to_wstring(pid) + L" --output_file \"" +
        csv.wstring() + L"\" --timed " + std::to_wstring(kCaptureDuration.count()) +
        L" --terminate_after_timed --no_console_stats --qpc_time_ms --session_name " +
        std::wstring(session.begin(), session.end());
}

PresentMonCaptureResult CapturePresentMonAttempt(
    const std::filesystem::path& executable,
    DWORD pid,
    const std::filesystem::path& csv,
    const std::string& session,
    const std::atomic_bool& stop,
    std::wofstream& log,
    const wchar_t* phase,
    bool elevated)
{
    PresentMonCaptureResult result;
    result.launchMode = elevated ? PresentMonLaunchMode::Elevated : PresentMonLaunchMode::Standard;
    std::error_code removeError;
    std::filesystem::remove(csv, removeError);
    const auto parameters = PresentMonParameters(pid, csv, session);
    const std::wstring command = L"\"" + executable.wstring() + L"\" " + parameters;
    HANDLE processHandle{};
    DWORD processId{};
    PROCESS_INFORMATION process{};
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.lpVerb = L"runas";
    execute.lpFile = executable.c_str();
    execute.lpParameters = parameters.c_str();
    execute.nShow = SW_HIDE;

    bool started = false;
    if (elevated)
    {
        started = ShellExecuteExW(&execute) != FALSE;
        processHandle = execute.hProcess;
        if (started && processHandle)
            processId = GetProcessId(processHandle);
        const DWORD errorCode = started ? ERROR_SUCCESS : GetLastError();
        if (!started && errorCode == ERROR_CANCELLED)
        {
            result.exitCode = ERROR_CANCELLED;
            log << L"Elevated retry: CANCELLED\n";
        }
        else if (!started)
        {
            log << L"Elevated retry: FAILED\nError: " << errorCode << L"\n";
        }
    }
    else
    {
        std::vector<wchar_t> line(command.begin(), command.end());
        line.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
        processHandle = process.hProcess;
        processId = process.dwProcessId;
    }

    if (!started)
    {
        if (!elevated)
            log << L"PresentMon: FAILED\nReason: CreateProcess failed\n";
        return result;
    }

    result.beginQpcMs = QpcMilliseconds();
    const auto startedAt = std::chrono::steady_clock::now();
    const auto deadline = startedAt + kCaptureWatchdog;
    bool timeout = false;
    bool waitFailed = false;

    while (!stop.load())
    {
        const DWORD wait = WaitForSingleObject(processHandle, 100);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED)
        {
            waitFailed = true;
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            timeout = true;
            break;
        }
    }

    if ((stop.load() || timeout || waitFailed) &&
        WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT)
    {
        TerminateProcess(processHandle, stop.load() ? 2 : 3);
        WaitForSingleObject(processHandle, 5000);
    }
    result.endQpcMs = QpcMilliseconds();

    DWORD code = STILL_ACTIVE;
    GetExitCodeProcess(processHandle, &code);
    const DWORD presentMonPid = processId;
    if (elevated)
        CloseHandle(processHandle);
    else
    {
        CloseHandle(process.hThread);
        CloseHandle(processHandle);
    }

    std::error_code error;
    const bool csvExists = std::filesystem::exists(csv, error) && !error;
    const auto csvSize = csvExists ? std::filesystem::file_size(csv, error) : 0;
    const double lifetime = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startedAt).count();

    result.exitCode = code;
    result.csvCreated = csvExists && !error && csvSize != 0;
    log << L"=== PRESENTMON CAPTURE ===\nPhase: " << phase
        << L"\nAttempt: " << (elevated ? L"ELEVATED" : L"STANDARD")
        << L"\nCommand: " << command
        << L"\nPresentMon PID: " << presentMonPid
        << L"\nExit Code: " << code
        << L"\nCSV Created: " << (result.csvCreated ? L"YES" : L"NO")
        << L"\nCSV Size: " << (error ? 0 : csvSize)
        << L"\nPresentMon Lifetime: " << lifetime << L" sec"
        << L"\nPresentMon Exit Time: " << Now() << L"\n";
    if (stop.load()) log << L"PresentMon: Cancelled\n";
    if (timeout) log << L"PresentMon: watchdog timeout\n";
    if (waitFailed) log << L"PresentMon: process wait failed\n";

    result.ok = !stop.load() && !timeout && !waitFailed && code == 0 && result.csvCreated;
    return result;
}

PresentMonCaptureResult CapturePresentMonPhase(
    const std::filesystem::path& executable,
    DWORD pid,
    const std::filesystem::path& csv,
    const std::string& session,
    const std::atomic_bool& stop,
    std::wofstream& log,
    const wchar_t* phase,
    bool& elevationRequired)
{
    if (elevationRequired)
        return CapturePresentMonAttempt(executable, pid, csv, session, stop, log, phase, true);

    auto result = CapturePresentMonAttempt(
        executable, pid, csv, session, stop, log, phase, false);
    if (result.ok || stop.load()) return result;

    log << L"Standard capture produced no usable CSV.\n"
        << L"Retrying this phase elevated.\n";
    result = CapturePresentMonAttempt(executable, pid, csv, session, stop, log, phase, true);
    if (result.ok) elevationRequired = true;
    return result;
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
    try
    {
        worker_ = std::thread(&VrrDiagnostic::Run, this);
    }
    catch (...)
    {
        hudStateSaved_ = false;
        state_ = VrrDiagnosticState::Idle;
        running_ = false;
        throw;
    }
    Status(L"Waiting for F8");
    return true;
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
    if (!notifyWindow_) return;
    auto* value = new std::wstring(text);
    if (!PostMessageW(notifyWindow_, kVrrDiagnosticStatus,
        reinterpret_cast<WPARAM>(value), 0))
        delete value;
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
        const auto offCsv = folder / (L"vrr-" + stamp + L"-off.csv");
        const auto staticCsv = folder / (L"vrr-" + stamp + L"-static.csv");
        const auto dynamicCsv = folder / (L"vrr-" + stamp + L"-dynamic.csv");
        log.open(txt);
        if (!log.is_open())
        {
            Status(L"Failed");
            return false;
        }

        log << L"=== CLAWHUD VRR DIAGNOSTIC ===\nTimestamp: " << Now()
            << L"\nPresentMon: 2.5.1\n"
            << L"PresentMon lifecycle: one independent 28-second process per phase\n"
            << L"PresentMon capture strategy:\n"
            << L"  per-phase 28-second captures\n"
            << L"  standard launch first\n"
            << L"  elevated fallback enabled\n"
            << L"Display tracking: enabled (no --no_track_display)\n"
            << L"VRR verdict is authoritative only when all phase captures meet coverage requirements.\n"
            << L"NOTE: PresentMon captures application presents and OS-visible display timing.\n"
            << L"Intel UMD XeFG-generated output frames may not all be observable here.\n"
            << L"Do not treat this capture as authoritative true XeFG displayed FPS.\n\n";

        LogMpoCapability(log);
        igcl.Initialize(log);
        igcl.LogState(log);

        const auto pm = std::filesystem::path(app_.ExecutablePath()).parent_path() /
            L"tools" / L"PresentMon.exe";
        if (!std::filesystem::exists(pm))
        {
            log << L"PresentMon: FAILED\nReason: tools\\PresentMon.exe not found\n";
            Status(L"Failed");
            return false;
        }

        log << L"=== VRR DIAGNOSTIC TRIGGER ===\nTrigger: F8\nForeground HWND: "
            << reinterpret_cast<const void*>(foregroundWindow)
            << L"\nTarget PID: " << targetPid
            << L"\nTarget Process: " << targetPath
            << L"\nTarget Alive: " << (Alive(targetPid) ? L"YES" : L"NO") << L"\n\n";

        const std::string sessionStamp = Narrow(stamp);
        bool elevationRequired = false;
        struct PhaseResult
        {
            clawhud::VrrCsvSummary csv;
            std::vector<clawhud::VblankSummary> vblank;
            bool captureOk{};
            bool targetAlive{};
            bool hudVisible{};
            bool runtimeOk{};
        };

        auto runPhase = [&](const wchar_t* title, const wchar_t* status,
            DiagnosticHudMode mode, const std::filesystem::path& csvPath,
            const char* sessionMode, std::string_view preferredSwapChain,
            PhaseResult& result) -> bool
        {
            log << L"=== " << title << L" ===\nStart: " << Now()
                << L"\nCSV: " << csvPath.wstring() << L"\n";
            Status(status);

            if (!app_.RequestDiagnosticHudMode(mode))
            {
                log << L"Main HUD mode request: FAILED\n";
                return false;
            }
            const bool expectedVisible = mode != DiagnosticHudMode::Off;
            result.hudVisible = app_.RequestDiagnosticHudVisibilityMatches(expectedVisible);
            if (!result.hudVisible)
            {
                log << L"Main HUD visibility: FAILED\n";
                return false;
            }

            log << L"Main HUD mode: confirmed\nTransition exclusion: 1000 ms\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (stop_) return false;

            igcl.StartSampling();
            const auto capture = CapturePresentMonPhase(pm, targetPid, csvPath,
                "ClawHUD-VRR-" + std::to_string(targetPid) + "-" + sessionMode + "-" + sessionStamp,
                stop_, log, title, elevationRequired);
            result.vblank = igcl.StopSampling(log, title);
            result.captureOk = capture.ok;
            result.targetAlive = Alive(targetPid);
            result.hudVisible = app_.RequestDiagnosticHudVisibilityMatches(expectedVisible);
            result.runtimeOk = !stop_ && result.targetAlive && result.hudVisible;

            if (capture.beginQpcMs > 0.0 && capture.endQpcMs >= capture.beginQpcMs)
            {
                result.csv = clawhud::ParseVrrCsvFile(csvPath, preferredSwapChain,
                    capture.beginQpcMs, capture.endQpcMs);
            }
            else
            {
                result.csv.reason = "PresentMon phase did not establish a usable QPC range";
            }

            WriteSummary(log, title, csvPath, result.csv);
            log << L"End: " << Now() << L"\n";
            if (!result.captureOk) log << L"Reason: PresentMon phase capture failed\n";
            if (!result.targetAlive) log << L"Reason: Target process exited\n";
            if (!result.hudVisible) log << L"Reason: Main HUD visibility did not match phase\n";
            return result.runtimeOk;
        };

        PhaseResult off, staticHud, dynamicHud;
        const bool offRuntimeOk = runPhase(
            L"PHASE A - HUD OFF", L"HUD OFF", DiagnosticHudMode::Off,
            offCsv, "OFF", {}, off);
        if (stop_)
        {
            log << L"RESULT: Cancelled\n";
            Status(L"Cancelled");
            return false;
        }

        bool staticRuntimeOk = false;
        bool dynamicRuntimeOk = false;
        if (offRuntimeOk)
        {
            staticRuntimeOk = runPhase(
                L"PHASE B - STATIC HUD", L"STATIC HUD", DiagnosticHudMode::Static,
                staticCsv, "STATIC", off.csv.valid ? off.csv.dominantSwapChain : std::string_view{}, staticHud);
        }
        if (stop_)
        {
            log << L"RESULT: Cancelled\n";
            Status(L"Cancelled");
            return false;
        }

        if (staticRuntimeOk)
        {
            dynamicRuntimeOk = runPhase(
                L"PHASE C - DYNAMIC HUD", L"DYNAMIC HUD", DiagnosticHudMode::Dynamic,
                dynamicCsv, "DYNAMIC", off.csv.valid ? off.csv.dominantSwapChain : std::string_view{}, dynamicHud);
        }
        if (stop_)
        {
            log << L"RESULT: Cancelled\n";
            Status(L"Cancelled");
            return false;
        }

        const bool runtimeOk = offRuntimeOk && staticRuntimeOk && dynamicRuntimeOk;
        const bool captureOk = off.captureOk && staticHud.captureOk && dynamicHud.captureOk;
        const bool coverageOk = off.csv.sufficientCoverage &&
            staticHud.csv.sufficientCoverage && dynamicHud.csv.sufficientCoverage;
        const bool diagnosticComplete = runtimeOk && captureOk && coverageOk;

        const auto evaluation = clawhud::EvaluateVrrComparison(off.csv, staticHud.csv, dynamicHud.csv);
        const auto finalVerdict = diagnosticComplete ? evaluation.verdict :
            clawhud::VrrDiagnosticVerdict::Inconclusive;

        std::string finalReason;
        if (!offRuntimeOk)
            finalReason = "HUD-OFF phase runtime validation failed.";
        else if (!staticRuntimeOk)
            finalReason = "STATIC HUD phase runtime validation failed.";
        else if (!dynamicRuntimeOk)
            finalReason = "DYNAMIC HUD phase runtime validation failed.";
        else if (!off.captureOk)
            finalReason = "HUD-OFF PresentMon capture failed or produced no usable CSV.";
        else if (!staticHud.captureOk)
            finalReason = "STATIC HUD PresentMon capture failed or produced no usable CSV.";
        else if (!dynamicHud.captureOk)
            finalReason = "DYNAMIC HUD PresentMon capture failed or produced no usable CSV.";
        else if (!coverageOk)
            finalReason = "One or more phase CSV files had insufficient QPC sample coverage.";
        else
            finalReason = evaluation.reason;

        const auto writeVerdictPhase = [&](const wchar_t* name,
            const clawhud::VrrCsvSummary& summary)
        {
            const auto percentage = clawhud::IndependentFlipPercentageIfAvailable(summary);
            const auto dominantMode = clawhud::DominantPresentMode(summary);
            log << name << L"\nIndependent Flip: "
                << (percentage ? std::to_wstring(*percentage) + L"%" : L"Unavailable")
                << L"\nDominant PresentMode: "
                << (dominantMode.empty() ? L"Unavailable" :
                    std::wstring(dominantMode.begin(), dominantMode.end()))
                << L"\n";
        };

        log << L"=== VRR VERDICT ===\nDiagnostic Status: "
            << (diagnosticComplete ? L"COMPLETE" : L"FAILED")
            << L"\nVRR Verdict: " << clawhud::VrrDiagnosticVerdictName(finalVerdict)
            << L"\nReason: " << std::wstring(finalReason.begin(), finalReason.end())
            << L"\nPresentMon elevated fallback used: "
            << (elevationRequired ? L"YES" : L"NO") << L"\n";
        writeVerdictPhase(L"HUD OFF", off.csv);
        writeVerdictPhase(L"STATIC HUD", staticHud.csv);
        writeVerdictPhase(L"DYNAMIC HUD", dynamicHud.csv);
        log << L"Supporting evidence: IGCL VBlank timing is not an authoritative VRR-active signal.\n"
            << L"Result: " << clawhud::VrrDiagnosticVerdictName(finalVerdict) << L"\n";

        switch (clawhud::VrrDiagnosticRuntimeStatusForResult(diagnosticComplete, finalVerdict))
        {
        case clawhud::VrrDiagnosticRuntimeStatus::Passed:
            Status(L"Passed");
            break;
        case clawhud::VrrDiagnosticRuntimeStatus::Inconclusive:
            Status(L"Inconclusive");
            break;
        case clawhud::VrrDiagnosticRuntimeStatus::Failed:
            Status(L"Failed");
            break;
        }
        return diagnosticComplete;
    }
    catch (...)
    {
        if (log.is_open()) log << L"RESULT: Failed\n";
        Status(L"Failed");
        return false;
    }
}
