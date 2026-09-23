#include "VrrDiagnosticCommand.h"

#include "DiagD3dkmtCadenceProbe.h"
#include "DiagIntelVrrStateProbe.h"
#include "DisplayPathProbe.h"
#include "VrrAnalysis.h"
#include "VrrApi2FrameCapture.h"
#include "VrrCaptureInvalidation.h"
#include "VrrReportWriter.h"
#include "VrrTargetAcquisition.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr auto kTargetWait = std::chrono::seconds(60);
constexpr auto kSettleDuration = std::chrono::seconds(2);
constexpr auto kCaptureDuration = std::chrono::seconds(15);
constexpr auto kValidationInterval = std::chrono::milliseconds(100);

bool SameLuid(const LUID& left, const LUID& right) noexcept
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

bool SameDisplayPath(const VrrDisplayPath& left, const VrrDisplayPath& right) noexcept
{
    return CompareStringOrdinal(left.monitorDeviceName.c_str(), -1,
               right.monitorDeviceName.c_str(), -1, TRUE) == CSTR_EQUAL &&
        SameLuid(left.sourceAdapterLuid, right.sourceAdapterLuid) &&
        left.sourceId == right.sourceId &&
        SameLuid(left.targetAdapterLuid, right.targetAdapterLuid) &&
        left.targetId == right.targetId &&
        left.refreshRateNumerator == right.refreshRateNumerator &&
        left.refreshRateDenominator == right.refreshRateDenominator;
}

bool SameIgclState(const DiagIntelVrrState& left, const DiagIntelVrrState& right) noexcept
{
    if (left.mappingStatus != right.mappingStatus ||
        left.windowsTargetId != right.windowsTargetId ||
        left.capabilityResult != right.capabilityResult ||
        left.profileResult != right.profileResult ||
        left.capability.has_value() != right.capability.has_value() ||
        left.profile.has_value() != right.profile.has_value())
        return false;

    if (left.capability &&
        (left.capability->supported != right.capability->supported ||
         left.capability->minimumHz != right.capability->minimumHz ||
         left.capability->maximumHz != right.capability->maximumHz ||
         left.capability->maxFrameTimeIncreaseUs != right.capability->maxFrameTimeIncreaseUs ||
         left.capability->maxFrameTimeDecreaseUs != right.capability->maxFrameTimeDecreaseUs))
        return false;
    if (left.profile &&
        (left.profile->profile != right.profile->profile ||
         left.profile->maximumHz != right.profile->maximumHz ||
         left.profile->minimumHz != right.profile->minimumHz ||
         left.profile->maxFrameTimeIncreaseUs != right.profile->maxFrameTimeIncreaseUs ||
         left.profile->maxFrameTimeDecreaseUs != right.profile->maxFrameTimeDecreaseUs))
        return false;
    return true;
}

DWORD ForegroundProcessId(HWND window) noexcept
{
    DWORD processId{};
    if (!window || !GetWindowThreadProcessId(window, &processId)) return 0;
    return processId;
}

void AddReason(std::vector<std::string>& reasons, std::string reason)
{
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end())
        reasons.push_back(std::move(reason));
}

std::string TargetAcquireFailure(VrrTargetAcquireStatus status)
{
    switch (status)
    {
    case VrrTargetAcquireStatus::ApiUnavailable: return "presentmon_api_unavailable";
    case VrrTargetAcquireStatus::CaptureFailed: return "presentmon_consume_failed";
    case VrrTargetAcquireStatus::MonitorUnavailable: return "monitor_unavailable";
    default: return "target_wait_timed_out";
    }
}

std::string OverallName(VrrOverallStatus status)
{
    switch (status)
    {
    case VrrOverallStatus::Off: return "OFF";
    case VrrOverallStatus::LikelyFixed: return "LIKELY FIXED";
    case VrrOverallStatus::LikelyActive: return "LIKELY ACTIVE";
    default: return "INCONCLUSIVE";
    }
}

std::string ConfidenceName(VrrConfidence confidence)
{
    switch (confidence)
    {
    case VrrConfidence::High: return "HIGH";
    case VrrConfidence::Medium: return "MEDIUM";
    default: return "LOW";
    }
}
}

void VrrDiagnosticCommand::Run() noexcept
{
    try
    {
        RunImpl();
    }
    catch (const std::exception& error)
    {
        std::cerr << "VRR Diagnostic failed safely: " << error.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "VRR Diagnostic failed safely.\n";
    }
}

void VrrDiagnosticCommand::RunImpl()
{
    std::cout << "\n=== VRR Diagnostic ===\n\n"
              << "Start the game before running this test.\n\n"
              << "Return to the game now.\n"
              << "The diagnostic will automatically lock the next verified foreground renderer.\n\n"
              << "Waiting for game...\n" << std::flush;

    VrrApi2FrameCapture frameCapture;
    if (!frameCapture.Initialize())
    {
        std::cout << "INCONCLUSIVE: PresentMon API 3.4 frame capture is unavailable.\n";
        return;
    }

    VrrTargetAcquisition acquisition;
    const auto acquireResult = acquisition.Acquire(frameCapture, kTargetWait);
    if (acquireResult.status != VrrTargetAcquireStatus::Locked || !acquireResult.target)
    {
        frameCapture.Shutdown();
        std::cout << "INCONCLUSIVE: " << TargetAcquireFailure(acquireResult.status)
                  << ". Returning to menu.\n";
        return;
    }

    const auto target = *acquireResult.target;
    frameCapture.StopTracking();
    std::wcout << L"Locked target: " << target.process.executableName << L" (PID "
               << target.process.processId << L"). Settling for 2 seconds...\n";
    std::this_thread::sleep_for(kSettleDuration);

    std::vector<std::string> reasons;
    const auto settledIdentity = QueryVrrProcessIdentity(target.process.processId);
    if (!settledIdentity)
    {
        std::cout << "INCONCLUSIVE: process_exited during settle. Returning to menu.\n";
        return;
    }
    if (!SameVrrProcessGeneration(target.process, *settledIdentity))
    {
        std::cout << "INCONCLUSIVE: process_generation_changed during settle. Returning to menu.\n";
        return;
    }

    const auto settledForeground = GetForegroundWindow();
    if (ForegroundProcessId(settledForeground) != target.process.processId)
    {
        std::cout << "INCONCLUSIVE: foreground_changed during settle. Returning to menu.\n";
        return;
    }
    if (MonitorFromWindow(settledForeground, MONITOR_DEFAULTTONULL) != target.monitor)
    {
        std::cout << "INCONCLUSIVE: monitor_changed during settle. Returning to menu.\n";
        return;
    }

    DisplayPathProbe displayPathProbe;
    const auto initialPathResult = displayPathProbe.Probe(target.monitor);
    std::optional<VrrDisplayPath> initialPath;
    if (initialPathResult.status == DisplayPathProbeStatus::Matched && initialPathResult.path)
        initialPath = *initialPathResult.path;
    else
        AddReason(reasons, "display_path_unavailable");

    DiagIntelVrrStateProbe igclProbe;
    DiagIntelVrrState initialIgcl;
    bool igclReady{};
    if (initialPath)
    {
        igclReady = igclProbe.Initialize();
        if (igclReady)
            initialIgcl = igclProbe.Query(initialPath->targetId);
        else
            AddReason(reasons, "igcl_unavailable");
    }

    VrrForegroundEventHook foregroundHook;
    if (!foregroundHook.Start(target.process.processId))
    {
        std::cout << "INCONCLUSIVE: foreground_event_unavailable; measurement was not started.\n";
        return;
    }

    DiagD3dkmtCadenceProbe d3dkmtProbe;
    bool d3dkmtReady{};
    if (initialPath)
    {
        d3dkmtReady = d3dkmtProbe.Initialize(target.monitor, *initialPath) &&
            d3dkmtProbe.Start();
        if (!d3dkmtReady) AddReason(reasons, "d3dkmt_unavailable");
    }
    else
    {
        AddReason(reasons, "d3dkmt_unavailable");
    }

    if (!frameCapture.StartTracking(target.process.processId))
    {
        foregroundHook.Stop();
        d3dkmtProbe.Stop();
        std::cout << "INCONCLUSIVE: PresentMon tracking/flush could not start.\n";
        return;
    }

    LARGE_INTEGER qpcFrequency{};
    LARGE_INTEGER measurementStart{};
    const bool qpcReady = QueryPerformanceFrequency(&qpcFrequency) &&
        qpcFrequency.QuadPart > 0 && QueryPerformanceCounter(&measurementStart);
    if (!qpcReady) AddReason(reasons, "qpc_unavailable");

    foregroundHook.BeginMeasurementEpoch();
    const auto startedAt = std::chrono::steady_clock::now();
    const auto deadline = startedAt + kCaptureDuration;
    std::string invalidationReason;
    std::cout << "Measuring passively for 15 seconds. No hotkey is required.\n";
    auto nextValidation = startedAt;
    int lastCountdown = -1;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (!foregroundHook.Running())
        {
            invalidationReason = "foreground_event_unavailable";
            break;
        }
        if (foregroundHook.ForeignForegroundObserved())
        {
            invalidationReason = "foreground_changed";
            break;
        }

        if (!frameCapture.DrainFrames())
        {
            invalidationReason = "presentmon_consume_failed";
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextValidation)
        {
            const auto currentIdentity = QueryVrrProcessIdentity(target.process.processId);
            if (!currentIdentity)
            {
                invalidationReason = "process_exited";
                break;
            }
            if (!SameVrrProcessGeneration(target.process, *currentIdentity))
            {
                invalidationReason = "process_generation_changed";
                break;
            }

            const auto foreground = GetForegroundWindow();
            const auto foregroundPid = ForegroundProcessId(foreground);
            if (foreground && foregroundPid != target.process.processId)
            {
                invalidationReason = "foreground_changed";
                break;
            }
            if (foregroundPid == target.process.processId &&
                MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL) != target.monitor)
            {
                invalidationReason = "monitor_changed";
                break;
            }

            if (initialPath)
            {
                const auto currentPath = displayPathProbe.Probe(target.monitor);
                if (currentPath.status != DisplayPathProbeStatus::Matched || !currentPath.path ||
                    !SameDisplayPath(*initialPath, *currentPath.path))
                {
                    invalidationReason = "display_path_changed";
                    break;
                }
            }
            nextValidation = now + kValidationInterval;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            deadline - std::chrono::steady_clock::now()).count() + 1;
        if (remaining != lastCountdown)
        {
            lastCountdown = static_cast<int>(remaining);
            std::cout << "\rMeasurement remaining: " << lastCountdown << " s   " << std::flush;
        }
        std::this_thread::sleep_for(kValidationInterval);
    }
    std::cout << '\n';
    if (invalidationReason.empty() && foregroundHook.ForeignForegroundObserved())
        invalidationReason = "foreground_changed";
    if (invalidationReason.empty() && !foregroundHook.Running())
        invalidationReason = "foreground_event_unavailable";
    foregroundHook.Stop();

    auto d3dkmtCapture = d3dkmtProbe.Stop();
    if (!frameCapture.DrainFrames() && invalidationReason.empty())
        invalidationReason = "presentmon_consume_failed";
    frameCapture.StopTracking();

    const auto duration = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startedAt).count();

    bool processStable = true;
    const auto finalIdentity = QueryVrrProcessIdentity(target.process.processId);
    if (!finalIdentity)
    {
        processStable = false;
        AddReason(reasons, "process_exited");
    }
    else if (!SameVrrProcessGeneration(target.process, *finalIdentity))
    {
        processStable = false;
        AddReason(reasons, "process_generation_changed");
    }

    const HWND finalForeground = GetForegroundWindow();
    const DWORD finalForegroundPid = ForegroundProcessId(finalForeground);
    if (!finalForeground || finalForegroundPid != target.process.processId)
        AddReason(reasons, "foreground_changed");
    bool monitorStable = finalForeground &&
        MonitorFromWindow(finalForeground, MONITOR_DEFAULTTONULL) == target.monitor;
    if (!monitorStable) AddReason(reasons, "monitor_changed");

    bool displayPathStable = initialPath.has_value();
    if (initialPath)
    {
        const auto finalPath = displayPathProbe.Probe(target.monitor);
        displayPathStable = finalPath.status == DisplayPathProbeStatus::Matched &&
            finalPath.path && SameDisplayPath(*initialPath, *finalPath.path);
        if (!displayPathStable) AddReason(reasons, "display_path_changed");
    }

    if (igclReady && initialPath)
    {
        const auto finalIgcl = igclProbe.Query(initialPath->targetId);
        if (!SameIgclState(initialIgcl, finalIgcl))
            AddReason(reasons, "igcl_state_changed");
    }

    const auto nominalHz = initialPath ? initialPath->nominalRefreshHz : 0.0;
    auto analysis = AnalyzeVrrSession(frameCapture.Samples(), target.process.processId,
        d3dkmtCapture, nominalHz, initialIgcl, processStable, monitorStable,
        displayPathStable);
    if (!invalidationReason.empty())
        AddReason(reasons, invalidationReason);
    if (!reasons.empty())
    {
        analysis.status = VrrOverallStatus::Inconclusive;
        analysis.confidence = VrrConfidence::Low;
    }

    VrrDiagnosticReportData report;
    report.target = target;
    if (initialPath) report.displayPath = *initialPath;
    report.igcl = initialIgcl;
    report.analysis = std::move(analysis);
    report.additionalReasons = std::move(reasons);
    report.presentMonMajor = frameCapture.ApiVersion().major;
    report.presentMonMinor = frameCapture.ApiVersion().minor;
    report.displayPathAvailable = initialPath.has_value();
    report.qpcFrequency = qpcReady ? qpcFrequency.QuadPart : 0;
    report.measurementStartQpc = qpcReady
        ? static_cast<std::uint64_t>(measurementStart.QuadPart) : 0;
    report.captureDurationSeconds = duration;

    const auto output = WriteVrrDiagnosticFiles(std::filesystem::current_path(),
        report, frameCapture.Samples(), d3dkmtCapture);
    std::cout << "VRR capture complete.\n"
              << "Result: " << OverallName(report.analysis.status) << " ("
              << ConfidenceName(report.analysis.confidence) << ")\n";
    if (output)
        std::cout << "Report: " << output->report.string() << '\n';
    else
        std::cout << "Report files could not be written.\n";
    MessageBeep(MB_OK);
}
