#include "VrrDiagnostic.h"

#include "App.h"
#include "IntelVrrDiagnosticProbe.h"
#include "ProductionTargetPolicy.h"
#include "VrrDiagnosticAnalysis.h"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <mmsystem.h>
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
    log << L"Independent Flip: " << std::fixed << std::setprecision(1)
        << clawhud::IndependentFlipPercentage(summary) << L"%\n";
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

bool Launch(const std::wstring& command, PROCESS_INFORMATION& process)
{
    std::vector<wchar_t> line(command.begin(), command.end()); line.push_back(L'\0'); STARTUPINFOW startup{ sizeof(startup) };
    return CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
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

void VrrDiagnostic::NotifyCompletion() const
{
    if (notifyWindow_)
        PostMessageW(notifyWindow_, kVrrDiagnosticCompleted, 0, 0);
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
    NotifyCompletion();
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

bool VrrDiagnostic::Capture(const std::filesystem::path& executable, DWORD pid, const std::filesystem::path& csv,
    const std::string& session, std::wofstream& log)
{
    const std::wstring command = L"\"" + executable.wstring() + L"\" --process_id " + std::to_wstring(pid) +
        L" --output_file \"" + csv.wstring() + L"\" --timed " + std::to_wstring(kCaptureDuration.count()) +
        L" --terminate_after_timed --no_console_stats --qpc_time_ms --session_name " + std::wstring(session.begin(), session.end());
    PROCESS_INFORMATION process{}; if (!Launch(command, process)) { log << L"PresentMon: FAILED\nReason: CreateProcess failed\n"; return false; }
    const auto deadline = std::chrono::steady_clock::now() + kCaptureWatchdog; bool timeout = false;
    while (!stop_)
    {
        const DWORD wait = WaitForSingleObject(process.hProcess, 100); if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED || std::chrono::steady_clock::now() >= deadline) { timeout = true; break; }
    }
    if ((stop_ || timeout) && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) { TerminateProcess(process.hProcess, stop_ ? 2 : 3); WaitForSingleObject(process.hProcess, 5000); }
    DWORD code = STILL_ACTIVE; GetExitCodeProcess(process.hProcess, &code); CloseHandle(process.hThread); CloseHandle(process.hProcess);
    const bool csvCreated = std::filesystem::exists(csv) && std::filesystem::file_size(csv) != 0;
    log << L"PresentMon Exit Code: " << code << L"\nCapture CSV Created: " << (csvCreated ? L"YES" : L"NO") << L"\n";
    if (stop_) { log << L"PresentMon: Cancelled\n"; return false; }
    if (timeout || code != 0 || !csvCreated) { log << L"PresentMon: FAILED\n"; return false; }
    return true;
}

bool VrrDiagnostic::RunImpl(DWORD targetPid, HWND foregroundWindow,
    const std::wstring& targetPath)
{
    std::wofstream log;
    clawhud::IntelVrrDiagnosticProbe igcl;
    try
    {
        const auto folder = LogFolder(app_); const auto stamp = Now(true); const auto txt = folder / (L"vrr-" + stamp + L".txt");
        const auto offCsv = folder / (L"vrr-" + stamp + L"-off.csv");
        const auto staticCsv = folder / (L"vrr-" + stamp + L"-static.csv");
        const auto dynamicCsv = folder / (L"vrr-" + stamp + L"-dynamic.csv"); log.open(txt);
        if (!log.is_open()) { Status(L"Failed"); return false; }
        log << L"=== CLAWHUD VRR DIAGNOSTIC ===\nTimestamp: " << Now() << L"\nPresentMon: 2.5.1\n"
            << L"Display tracking: enabled (no --no_track_display)\n"
            << L"Pinned v2.5.1 console asset does not expose --write_display_metadata; standard display timing columns are retained.\n"
            << L"VRR Analysis: automatic PASS/FAIL/INCONCLUSIVE verdict follows; manual review remains recommended.\nNOTE: PresentMon captures application presents and OS-visible display timing.\n"
            << L"Intel UMD XeFG-generated output frames may not all be observable here.\nDo not treat this capture as authoritative true XeFG displayed FPS.\n\n";
        LogMpoCapability(log);
        igcl.Initialize(log);
        igcl.LogState(log);
        const auto pm = std::filesystem::path(app_.ExecutablePath()).parent_path() / L"tools" / L"PresentMon.exe";
        if (!std::filesystem::exists(pm)) { log << L"PresentMon: FAILED\nReason: tools\\PresentMon.exe not found\n"; Status(L"Failed"); return false; }
        log << L"=== VRR DIAGNOSTIC TRIGGER ===\nTrigger: F8\nForeground HWND: "
            << reinterpret_cast<const void*>(foregroundWindow) << L"\nTarget PID: " << targetPid
            << L"\nTarget Process: " << targetPath << L"\n\n";
        const std::string sessionStamp = Narrow(stamp);
        struct PhaseResult { clawhud::VrrCsvSummary csv; std::vector<clawhud::VblankSummary> vblank; bool captureOk{}; bool targetAlive{}; bool hudVisible{}; };
        auto runPhase = [&](const wchar_t* title, const wchar_t* status, DiagnosticHudMode mode,
            const std::filesystem::path& csvPath, const char* sessionMode,
            std::string_view preferredSwapChain, PhaseResult& result) -> bool
        {
            log << L"=== " << title << L" ===\nStart: " << Now() << L"\nCSV: " << csvPath.wstring() << L"\n";
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
            log << L"Main HUD mode: confirmed\n";
            if (stop_) return false;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            igcl.StartSampling();
            result.captureOk = Capture(pm, targetPid, csvPath,
                "ClawHUD-VRR-" + std::to_string(targetPid) + "-" + sessionMode + "-" + sessionStamp, log);
            result.vblank = igcl.StopSampling(log, title);
            result.targetAlive = Alive(targetPid);
            result.hudVisible = app_.RequestDiagnosticHudVisibilityMatches(expectedVisible);
            result.csv = clawhud::ParseVrrCsvFile(csvPath, preferredSwapChain);
            WriteSummary(log, title, csvPath, result.csv);
            log << L"End: " << Now() << L"\n";
            if (!result.targetAlive) log << L"Reason: Target process exited\n";
            if (!result.hudVisible) log << L"Reason: Main HUD visibility did not match phase\n";
            return !stop_ && result.captureOk && result.targetAlive && result.hudVisible;
        };

        PhaseResult off, staticHud, dynamicHud;
        const bool offOk = runPhase(L"PHASE A - HUD OFF", L"HUD OFF", DiagnosticHudMode::Off, offCsv, "OFF", {}, off);
        if (stop_) { log << L"RESULT: Cancelled\n"; Status(L"Cancelled"); return false; }
        if (!offOk) { log << L"RESULT: Failed\nReason: HUD-OFF phase failed\n"; Status(L"Failed"); return false; }
        const bool staticOk = runPhase(L"PHASE B - STATIC HUD", L"STATIC HUD", DiagnosticHudMode::Static, staticCsv, "STATIC", off.csv.dominantSwapChain, staticHud);
        if (stop_) { log << L"RESULT: Cancelled\n"; Status(L"Cancelled"); return false; }
        if (!staticOk) { log << L"RESULT: Failed\nReason: STATIC HUD phase failed\n"; Status(L"Failed"); return false; }
        const bool dynamicOk = runPhase(L"PHASE C - DYNAMIC HUD", L"DYNAMIC HUD", DiagnosticHudMode::Dynamic, dynamicCsv, "DYNAMIC", off.csv.dominantSwapChain, dynamicHud);
        if (stop_) { log << L"RESULT: Cancelled\n"; Status(L"Cancelled"); return false; }
        auto writeComparison = [&](const wchar_t* name, const PhaseResult& left, const PhaseResult& right)
        {
            const auto leftMedian = clawhud::UsableVblankMedian(left.vblank);
            const auto rightMedian = clawhud::UsableVblankMedian(right.vblank);
            log << name << L"\nPresentMode set comparison is informational; distribution is authoritative.\n"
                << L"Game swapchain continuity: "
                << ((!left.csv.valid || !right.csv.valid) ? L"Unavailable" :
                    (left.csv.dominantSwapChain == right.csv.dominantSwapChain ? L"SAME" : L"RECREATED"))
                << L"\n";
            if (left.csv.valid && right.csv.valid)
            {
                log << L"Average MsBetweenDisplayChange: " << left.csv.displayAverage << L" vs " << right.csv.displayAverage
                    << L"\nMin/Max MsBetweenDisplayChange: " << left.csv.displayMin << L"/" << left.csv.displayMax
                    << L" vs " << right.csv.displayMin << L"/" << right.csv.displayMax
                    << L"\nDisplayed / not-displayed: " << left.csv.displayed << L"/" << left.csv.notDisplayed
                    << L" vs " << right.csv.displayed << L"/" << right.csv.notDisplayed << L"\n";
            }
            else
                log << L"PresentMon timing comparison: Unavailable\n";
            log << L"VBlank median: " << (leftMedian ? std::to_wstring(*leftMedian) : L"Unavailable")
                << L" us vs " << (rightMedian ? std::to_wstring(*rightMedian) : L"Unavailable") << L" us\n\n";
        };
        log << L"=== COMPARISON ===\n";
        writeComparison(L"OFF vs STATIC", off, staticHud);
        writeComparison(L"STATIC vs DYNAMIC", staticHud, dynamicHud);
        writeComparison(L"OFF vs DYNAMIC", off, dynamicHud);
        const auto evaluation = clawhud::EvaluateVrrComparison(off.csv, staticHud.csv, dynamicHud.csv);
        const bool phaseOk = !stop_ && offOk && staticOk && dynamicOk;
        const auto finalVerdict = phaseOk ? evaluation.verdict : clawhud::VrrDiagnosticVerdict::Fail;
        const std::string finalReason = phaseOk ? evaluation.reason :
            "A diagnostic phase failed before the VRR result was authoritative.";
        const auto writeVerdictPhase = [&](const wchar_t* name, const clawhud::VrrCsvSummary& summary)
        {
            const auto dominantMode = clawhud::DominantPresentMode(summary);
            log << name << L"\nIndependent Flip: " << std::fixed << std::setprecision(1)
                << clawhud::IndependentFlipPercentage(summary) << L"%\n"
                << L"Dominant PresentMode: "
                << std::wstring(dominantMode.begin(), dominantMode.end())
                << L"\n";
        };
        log << L"=== VRR VERDICT ===\n";
        writeVerdictPhase(L"HUD OFF", off.csv);
        writeVerdictPhase(L"STATIC HUD", staticHud.csv);
        log << L"Delta vs OFF: " << std::fixed << std::setprecision(1)
            << clawhud::IndependentFlipPercentage(staticHud.csv) - clawhud::IndependentFlipPercentage(off.csv) << L" pp\n";
        writeVerdictPhase(L"DYNAMIC HUD", dynamicHud.csv);
        log << L"Delta vs OFF: " << std::fixed << std::setprecision(1)
            << clawhud::IndependentFlipPercentage(dynamicHud.csv) - clawhud::IndependentFlipPercentage(off.csv) << L" pp\n"
            << L"Final Verdict: " << clawhud::VrrDiagnosticVerdictName(finalVerdict)
            << L"\nReason: " << std::wstring(finalReason.begin(), finalReason.end()) << L"\n"
            << L"Supporting evidence: IGCL VBlank timing is not an authoritative VRR-active signal.\n";
        log << L"Result: " << clawhud::VrrDiagnosticVerdictName(finalVerdict) << L"\n";
        Status(stop_ ? L"Cancelled" :
            finalVerdict == clawhud::VrrDiagnosticVerdict::Pass ? L"Passed" :
            finalVerdict == clawhud::VrrDiagnosticVerdict::Inconclusive ? L"Inconclusive" : L"Failed");
        return phaseOk;
    }
    catch (...) { if (log.is_open()) log << L"RESULT: Failed\n"; Status(L"Failed"); return false; }
}
