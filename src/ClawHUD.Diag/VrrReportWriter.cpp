#include "VrrReportWriter.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>

namespace
{
std::string FormatDouble(double value, int precision = 2)
{
    if (!std::isfinite(value)) return "n/a";
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(precision) << value;
    return text.str();
}

std::string OptionalDouble(const std::optional<double>& value, int precision = 2)
{
    return value && std::isfinite(*value) ? FormatDouble(*value, precision) : "n/a";
}

std::string OptionalBoolean(const std::optional<bool>& value)
{
    return value ? (*value ? "TRUE" : "FALSE") : "";
}

std::string Utf8(std::wstring_view value)
{
    if (value.empty()) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, value.data(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr))
        return {};
    return result;
}

std::string Hex(std::uint64_t value)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << "0x" << std::uppercase << std::hex << value;
    return text.str();
}

std::string Hex32(std::uint32_t value)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << "0x" << std::uppercase << std::hex << std::setfill('0')
         << std::setw(8) << value;
    return text.str();
}

std::string Luid(const LUID& value)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << "0x" << std::uppercase << std::hex << std::setfill('0')
         << std::setw(8) << static_cast<std::uint32_t>(value.HighPart)
         << ':' << std::setw(8) << value.LowPart;
    return text.str();
}

const char* OverallName(VrrOverallStatus value) noexcept
{
    switch (value)
    {
    case VrrOverallStatus::Off: return "OFF";
    case VrrOverallStatus::LikelyFixed: return "LIKELY FIXED";
    case VrrOverallStatus::LikelyActive: return "LIKELY ACTIVE";
    default: return "INCONCLUSIVE";
    }
}

const char* ConfidenceName(VrrConfidence value) noexcept
{
    switch (value)
    {
    case VrrConfidence::High: return "HIGH";
    case VrrConfidence::Medium: return "MEDIUM";
    default: return "LOW";
    }
}

const char* IgclName(VrrIgclClass value) noexcept
{
    switch (value)
    {
    case VrrIgclClass::Enabled: return "ENABLED";
    case VrrIgclClass::Off: return "OFF";
    case VrrIgclClass::Ambiguous: return "AMBIGUOUS";
    default: return "UNKNOWN";
    }
}

const char* IgclProfileName(std::uint32_t value) noexcept
{
    switch (value)
    {
    case 1: return "RECOMMENDED";
    case 2: return "EXCELLENT";
    case 3: return "GOOD";
    case 4: return "COMPATIBLE";
    case 5: return "OFF";
    case 6: return "VESA";
    case 7: return "CUSTOM";
    default: return "INVALID/UNKNOWN";
    }
}

const char* MappingName(DiagIgclTargetMappingStatus value) noexcept
{
    switch (value)
    {
    case DiagIgclTargetMappingStatus::Exact: return "EXACT";
    case DiagIgclTargetMappingStatus::Ambiguous: return "AMBIGUOUS";
    default: return "UNKNOWN";
    }
}

const char* IgclFailureStageName(DiagIgclProbeFailureStage value) noexcept
{
    switch (value)
    {
    case DiagIgclProbeFailureStage::NotInitialized: return "not_initialized";
    case DiagIgclProbeFailureStage::LoadLibrary: return "load_library";
    case DiagIgclProbeFailureStage::ResolveFunction: return "resolve_function";
    case DiagIgclProbeFailureStage::Initialize: return "ctlInit";
    case DiagIgclProbeFailureStage::EnumerateDevices: return "ctlEnumerateDevices";
    case DiagIgclProbeFailureStage::GetDeviceProperties: return "ctlGetDeviceProperties";
    case DiagIgclProbeFailureStage::EnumerateDisplayOutputs: return "ctlEnumerateDisplayOutputs";
    case DiagIgclProbeFailureStage::GetDisplayProperties: return "ctlGetDisplayProperties";
    case DiagIgclProbeFailureStage::TargetMapping: return "target_mapping";
    case DiagIgclProbeFailureStage::GetArcSyncInfo: return "ctlGetIntelArcSyncInfoForMonitor";
    case DiagIgclProbeFailureStage::GetArcSyncProfile: return "ctlGetIntelArcSyncProfile";
    case DiagIgclProbeFailureStage::InternalError: return "internal_error";
    default: return "none";
    }
}

const char* IgclResultDomainName(DiagIgclProbeResultDomain value) noexcept
{
    switch (value)
    {
    case DiagIgclProbeResultDomain::Win32: return "Win32";
    case DiagIgclProbeResultDomain::ControlLibrary: return "CTL_RESULT";
    default: return "none";
    }
}

const char* D3dkmtFailureName(DiagD3dkmtCaptureFailure value) noexcept
{
    switch (value)
    {
    case DiagD3dkmtCaptureFailure::ApiUnavailable: return "api_unavailable";
    case DiagD3dkmtCaptureFailure::InvalidTarget: return "invalid_target";
    case DiagD3dkmtCaptureFailure::DisplayPathMismatch: return "display_path_mismatch";
    case DiagD3dkmtCaptureFailure::AdapterOpenFailed: return "adapter_open_failed";
    case DiagD3dkmtCaptureFailure::AdapterCloseFailed: return "adapter_close_failed";
    case DiagD3dkmtCaptureFailure::QpcUnavailable: return "qpc_unavailable";
    case DiagD3dkmtCaptureFailure::SamplerStartFailed: return "sampler_start_failed";
    case DiagD3dkmtCaptureFailure::WaitFailed: return "wait_failed";
    case DiagD3dkmtCaptureFailure::QueryCounterFailed: return "query_counter_failed";
    case DiagD3dkmtCaptureFailure::SampleStorageFailed: return "sample_storage_failed";
    default: return "none";
    }
}

const char* D3dkmtStatusDomainName(DiagD3dkmtFailureStatusDomain value) noexcept
{
    switch (value)
    {
    case DiagD3dkmtFailureStatusDomain::Win32: return "Win32";
    case DiagD3dkmtFailureStatusDomain::NtStatus: return "NTSTATUS";
    default: return "none";
    }
}

std::string IgclFailureResult(const DiagIgclProbeFailure& failure)
{
    if (!failure.result) return "n/a";
    return std::string(IgclResultDomainName(failure.resultDomain)) + " " +
        Hex32(*failure.result);
}

const char* PresentationName(VrrPresentationClass value) noexcept
{
    switch (value)
    {
    case VrrPresentationClass::VrrSafe: return "VRR_SAFE";
    case VrrPresentationClass::Marginal: return "MARGINAL";
    case VrrPresentationClass::NotVrrSafe: return "NOT_VRR_SAFE";
    default: return "UNKNOWN";
    }
}

const char* CadenceName(DiagD3dkmtCadenceClass value) noexcept
{
    switch (value)
    {
    case DiagD3dkmtCadenceClass::FixedLike: return "FIXED_LIKE";
    case DiagD3dkmtCadenceClass::VariableLike: return "VARIABLE_LIKE";
    case DiagD3dkmtCadenceClass::Indeterminate: return "INDETERMINATE";
    default: return "UNAVAILABLE";
    }
}

const char* PresentModeName(std::int32_t value) noexcept
{
    switch (value)
    {
    case PM_PRESENT_MODE_HARDWARE_LEGACY_FLIP: return "HARDWARE_LEGACY_FLIP";
    case PM_PRESENT_MODE_HARDWARE_LEGACY_COPY_TO_FRONT_BUFFER: return "HARDWARE_LEGACY_COPY";
    case PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP: return "HARDWARE_INDEPENDENT_FLIP";
    case PM_PRESENT_MODE_COMPOSED_FLIP: return "COMPOSED_FLIP";
    case PM_PRESENT_MODE_COMPOSED_COPY_WITH_GPU_GDI: return "COMPOSED_GPU_GDI";
    case PM_PRESENT_MODE_COMPOSED_COPY_WITH_CPU_GDI: return "COMPOSED_CPU_GDI";
    case PM_PRESENT_MODE_HARDWARE_COMPOSED_INDEPENDENT_FLIP: return "COMPOSED_INDEPENDENT_FLIP";
    default: return "UNKNOWN";
    }
}

const char* ReasonName(VrrAnalysisReason value) noexcept
{
    switch (value)
    {
    case VrrAnalysisReason::InsufficientEvidence: return "insufficient_evidence";
    case VrrAnalysisReason::IdentityUnstable: return "identity_unstable";
    case VrrAnalysisReason::DisplayPathUnavailable: return "display_path_unavailable";
    case VrrAnalysisReason::IgclUnknown: return "igcl_unknown";
    case VrrAnalysisReason::IgclAmbiguous: return "igcl_ambiguous";
    case VrrAnalysisReason::NoDominantSwapchain: return "no_dominant_swapchain";
    case VrrAnalysisReason::InsufficientPresentModeSamples: return "present_mode_unavailable";
    case VrrAnalysisReason::CadenceUnavailable: return "d3dkmt_unavailable";
    case VrrAnalysisReason::InsufficientCadenceWindows: return "insufficient_cadence_windows";
    case VrrAnalysisReason::EvidenceDisagreement: return "evidence_disagreement";
    case VrrAnalysisReason::GameRateNearNominal: return "game_rate_near_nominal";
    case VrrAnalysisReason::FrameGenerationConfidenceReduction: return "frame_generation_confidence_reduced";
    default: return "unknown_reason";
    }
}

const char* MetricName(VrrPacingMetric value) noexcept
{
    switch (value)
    {
    case VrrPacingMetric::BetweenPresents: return "BetweenPresents";
    case VrrPacingMetric::BetweenDisplayChange: return "BetweenDisplayChange";
    case VrrPacingMetric::DisplayedTime: return "DisplayedTime";
    case VrrPacingMetric::UntilDisplayed: return "UntilDisplayed";
    case VrrPacingMetric::DisplayLatency: return "DisplayLatency";
    case VrrPacingMetric::RenderPresentLatency: return "RenderPresentLatency";
    case VrrPacingMetric::FlipDelay: return "FlipDelay";
    default: return "Unknown";
    }
}

std::string ReasonList(const VrrDiagnosticReportData& data)
{
    std::ostringstream text;
    bool first = true;
    const auto append = [&](std::string_view value)
    {
        if (!first) text << ", ";
        text << value;
        first = false;
    };
    for (const auto reason : data.analysis.reasons) append(ReasonName(reason));
    for (const auto& reason : data.additionalReasons) append(reason);
    return first ? "none" : text.str();
}

std::string Range(std::optional<double> minimum, std::optional<double> maximum)
{
    if (!minimum || !maximum || !std::isfinite(*minimum) || !std::isfinite(*maximum))
        return "n/a";
    return FormatDouble(*minimum) + "-" + FormatDouble(*maximum) + " Hz";
}

std::optional<double> ElapsedMilliseconds(std::uint64_t qpc,
    const VrrDiagnosticReportData& report)
{
    if (report.qpcFrequency <= 0 || qpc < report.measurementStartQpc)
        return std::nullopt;
    return 1000.0 * static_cast<double>(qpc - report.measurementStartQpc) /
        static_cast<double>(report.qpcFrequency);
}

std::string CsvOptionalDouble(const std::optional<double>& value, int precision)
{
    return value && std::isfinite(*value) ? FormatDouble(*value, precision) : "";
}

std::string CsvElapsed(std::optional<std::uint64_t> qpc,
    const VrrDiagnosticReportData& report)
{
    return qpc ? CsvOptionalDouble(ElapsedMilliseconds(*qpc, report), 3) : "";
}

bool WriteReport(const std::filesystem::path& path,
    const VrrDiagnosticReportData& data,
    const DiagD3dkmtCadenceCapture& d3dkmt)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.imbue(std::locale::classic());
    const auto& presentation = data.analysis.presentation;
    const auto& cadence = data.analysis.cadence;
    const auto& monitor = data.target.monitorInfo;
    const auto capability = data.igcl.capability;
    const auto profile = data.igcl.profile;

    output << "=== ClawHUD VRR Diagnostic ===\n\n"
           << "Target\n"
           << "  Executable                   " << Utf8(data.target.process.executableName) << '\n'
           << "  PID                          " << data.target.process.processId << '\n'
           << "  HWND                         " << Hex(reinterpret_cast<std::uintptr_t>(data.target.window)) << '\n'
           << "  Monitor                      " << Utf8(monitor.szDevice) << '\n'
           << "  Windows target ID            " << (data.displayPathAvailable ? std::to_string(data.displayPath.targetId) : "n/a") << '\n'
           << "  Nominal refresh              " << (data.displayPathAvailable
                ? FormatDouble(data.displayPath.nominalRefreshHz, 3) + " Hz" : "n/a") << "\n\n"
           << "PresentMon\n"
           << "  API                          " << data.presentMonMajor << '.' << data.presentMonMinor << '\n'
           << "  Capture duration             " << FormatDouble(data.captureDurationSeconds, 1) << " s\n"
           << "  Dominant swapchain           " << (presentation.dominantSwapchain ? Hex(*presentation.dominantSwapchain) : "n/a") << '\n'
           << "  Dominant share               " << FormatDouble(100.0 * presentation.dominantShare, 1) << "%\n"
           << "  Frames                       " << presentation.frameCount << '\n'
           << "  Dropped                      " << presentation.droppedCount << " / " << presentation.droppedFlagSamples
           << " (" << OptionalDouble(presentation.droppedPercentage, 1) << "%)\n"
           << "  Independent Flip             " << OptionalDouble(presentation.independentFlipPercentage, 1) << "%\n"
           << "  Present path                 " << PresentationName(presentation.classification) << '\n'
           << "  AllowsTearing true           " << presentation.allowsTearingTrue << " / "
           << (presentation.allowsTearingTrue + presentation.allowsTearingFalse +
               presentation.allowsTearingUnknown) << "\n"
           << "  PresentMode distribution    ";
    if (presentation.presentModeCounts.empty()) output << "n/a\n";
    else
    {
        bool first = true;
        for (const auto& [mode, count] : presentation.presentModeCounts)
        {
            if (!first) output << ", ";
            output << PresentModeName(mode) << '=' << count;
            first = false;
        }
        output << '\n';
    }
    output << "  PresentRuntime distribution ";
    if (presentation.presentRuntimeCounts.empty()) output << "n/a\n";
    else
    {
        bool first = true;
        for (const auto& [runtime, count] : presentation.presentRuntimeCounts)
        {
            if (!first) output << ", ";
            output << runtime << '=' << count;
            first = false;
        }
        output << '\n';
    }
    output << "  FrameType distribution      ";
    if (presentation.frameTypeCounts.empty()) output << "n/a\n";
    else
    {
        bool first = true;
        for (const auto& [frameType, count] : presentation.frameTypeCounts)
        {
            if (!first) output << ", ";
            output << frameType << '=' << count;
            first = false;
        }
        output << '\n';
    }
    output << "  SyncInterval distribution  ";
    if (presentation.syncIntervalCounts.empty()) output << "n/a\n";
    else
    {
        bool first = true;
        for (const auto& [interval, count] : presentation.syncIntervalCounts)
        {
            if (!first) output << ", ";
            output << interval << '=' << count;
            first = false;
        }
        output << '\n';
    }

    output << "\nDisplay pacing\n";
    for (const auto& metric : presentation.pacingMetrics)
        output << "  " << MetricName(metric.metric) << " P50/P95/P99/Max "
               << OptionalDouble(metric.p50) << " / " << OptionalDouble(metric.p95) << " / "
               << OptionalDouble(metric.p99) << " / " << OptionalDouble(metric.maximum) << " ms"
               << " (n=" << metric.finiteSampleCount << ")\n";
    output << "  Display-change rate          "
           << OptionalDouble(presentation.displayChangeRateHz) << " Hz\n\n"
           << "Intel Arc Sync\n"
           << "  Probe status                 "
           << (data.igcl.initialized ? "INITIALIZED" :
               (data.igcl.attempted ? "UNAVAILABLE" : "NOT RUN")) << '\n'
           << "  Target adapter LUID          " << (data.displayPathAvailable
                ? Luid(data.igcl.windowsTargetAdapterLuid) : "n/a") << '\n'
           << "  Target ID                    " << (data.displayPathAvailable
                ? std::to_string(data.igcl.windowsTargetId) : "n/a") << '\n'
           << "  Target mapping               " << MappingName(data.igcl.mappingStatus) << '\n'
           << "  Adapter count                " << (data.igcl.initialized
                ? std::to_string(data.igcl.adapterCount) : "n/a") << '\n'
           << "  Display outputs enumerated   " << (data.igcl.initialized
                ? std::to_string(data.igcl.displayOutputCount) : "n/a") << '\n'
           << "  Display properties fetched   " << (data.igcl.initialized
                ? std::to_string(data.igcl.displayPropertiesSuccessCount) : "n/a") << '\n'
           << "  Exact target matches         " << (data.igcl.initialized
                ? std::to_string(data.igcl.targetMatchCount) : "n/a") << '\n'
           << "  Enumeration complete         " << (data.igcl.initialized
                ? (data.igcl.enumerationComplete ? "YES" : "NO") : "n/a") << '\n'
           << "  Supported                    "
           << (capability ? (capability->supported ? "YES" : "NO") : "UNKNOWN") << '\n'
           << "  Capability API result        " << (data.igcl.capabilityResult
                ? Hex32(*data.igcl.capabilityResult) : "n/a") << '\n'
           << "  Profile                      "
           << (profile ? IgclProfileName(profile->profile) : "n/a") << '\n'
           << "  Profile API result           " << (data.igcl.profileResult
                ? Hex32(*data.igcl.profileResult) : "n/a") << '\n'
           << "  Capability range             "
           << Range(capability ? std::optional<double>(capability->minimumHz) : std::nullopt,
                capability ? std::optional<double>(capability->maximumHz) : std::nullopt) << '\n'
           << "  Active range                 "
           << Range(profile ? std::optional<double>(profile->minimumHz) : std::nullopt,
                profile ? std::optional<double>(profile->maximumHz) : std::nullopt) << '\n'
            << "  Configuration                " << IgclName(data.analysis.igcl) << '\n'
            << "  Probe failures               " << data.igcl.failureRecordCount
            << " (suppressed=" << data.igcl.suppressedFailureCount << ")\n";
    if (data.igcl.failureRecordCount == 0)
        output << "    none\n";
    else
    {
        for (std::size_t i = 0; i < data.igcl.failureRecordCount; ++i)
        {
            const auto& failure = data.igcl.failures[i];
            output << "    [" << i << "] " << IgclFailureStageName(failure.stage)
                   << " result=" << IgclFailureResult(failure);
            if (failure.adapterIndex != DiagIgclProbeFailure::NoIndex)
                output << " adapter=" << failure.adapterIndex;
            if (failure.outputIndex != DiagIgclProbeFailure::NoIndex)
                output << " output=" << failure.outputIndex;
            if (!failure.detail.empty()) output << " detail=" << failure.detail;
            output << '\n';
        }
    }
    output << '\n'
            << "D3DKMT\n"
            << "  Adapter LUID                 " << (d3dkmt.targetIdentified
                 ? Luid(LUID{ d3dkmt.adapterLuidLow, d3dkmt.adapterLuidHigh }) : "n/a") << '\n'
            << "  VidPnSourceId                " << (d3dkmt.targetIdentified
                 ? std::to_string(d3dkmt.vidPnSourceId) : "n/a") << '\n'
            << "  QPC frequency                " << (d3dkmt.qpcFrequency > 0
                 ? std::to_string(d3dkmt.qpcFrequency) : "n/a") << '\n'
            << "  Sampler mode                 " << (d3dkmt.attempted
                ? "Event2 NumObjects=0" : "NOT RUN") << '\n'
            << "  Samples                      " << d3dkmt.timestamps.size() << '\n'
            << "  Complete 1s windows          " << cadence.windows.size() << '\n'
           << "  Window range                 " << Range(cadence.minimumHz, cadence.maximumHz) << '\n'
           << "  Near-nominal ratio           " << OptionalDouble(cadence.nearNominalRatio
                ? std::optional<double>(*cadence.nearNominalRatio * 100.0) : std::nullopt, 1) << "%\n"
            << "  Cadence                      " << CadenceName(cadence.classification) << '\n'
            << "  Capture status               "
            << (d3dkmt.available ? "AVAILABLE" :
                (d3dkmt.attempted ? "UNAVAILABLE" : "NOT RUN")) << '\n'
            << "  Failure                      " << (d3dkmt.attempted
                 ? D3dkmtFailureName(d3dkmt.failure) : "n/a") << '\n'
            << "  Failure detail               " << (d3dkmt.failureDetail.empty()
                 ? "n/a" : d3dkmt.failureDetail) << '\n'
            << "  Failure status               ";
    if (d3dkmt.failureStatus)
        output << D3dkmtStatusDomainName(d3dkmt.failureStatusDomain) << ' '
               << Hex32(static_cast<std::uint32_t>(*d3dkmt.failureStatus)) << '\n';
    else
        output << "n/a\n";
    output << "\nOverall\n"
            << "  VRR Status                   " << OverallName(data.analysis.status) << '\n'
           << "  Confidence                   " << ConfidenceName(data.analysis.confidence) << '\n'
           << "  Reasons                      " << ReasonList(data) << "\n\n"
           << "Note\n"
           << "  This is a multi-signal diagnostic inference.\n"
           << "  It is not a direct physical scanout truth API.\n";
    output.flush();
    return static_cast<bool>(output);
}

bool WriteFramesCsv(const std::filesystem::path& path,
    const VrrDiagnosticReportData& report, std::span<const VrrFrameSample> frames)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.imbue(std::locale::classic());
    output << "ElapsedMs,ProcessId,SwapChainAddress,PresentStartQpc,PresentMode,PresentRuntime,FrameType,"
              "AllowsTearing,Dropped,SyncInterval,PresentFlags,BetweenPresentsMs,BetweenDisplayChangeMs,"
              "DisplayedTimeMs,UntilDisplayedMs,DisplayLatencyMs,RenderPresentLatencyMs,FlipDelayMs\n";
    for (const auto& frame : frames)
    {
        output << CsvElapsed(frame.presentStartQpc, report) << ','
               << frame.processId << ',' << Hex(frame.swapChainAddress) << ','
               << (frame.presentStartQpc ? std::to_string(*frame.presentStartQpc) : "") << ','
               << frame.presentMode << ','
               << (frame.presentRuntime ? std::to_string(*frame.presentRuntime) : "") << ','
               << (frame.frameType ? std::to_string(*frame.frameType) : "") << ','
               << OptionalBoolean(frame.allowsTearing) << ',' << OptionalBoolean(frame.dropped) << ','
               << (frame.syncInterval ? std::to_string(*frame.syncInterval) : "") << ','
               << (frame.presentFlags ? std::to_string(*frame.presentFlags) : "") << ','
               << CsvOptionalDouble(frame.betweenPresentsMs, 4) << ','
               << CsvOptionalDouble(frame.betweenDisplayChangeMs, 4) << ','
               << CsvOptionalDouble(frame.displayedTimeMs, 4) << ','
               << CsvOptionalDouble(frame.untilDisplayedMs, 4) << ','
               << CsvOptionalDouble(frame.displayLatencyMs, 4) << ','
               << CsvOptionalDouble(frame.renderPresentLatencyMs, 4) << ','
               << CsvOptionalDouble(frame.flipDelayMs, 4) << '\n';
    }
    output.flush();
    return static_cast<bool>(output);
}

bool WriteVblankCsv(const std::filesystem::path& path,
    const VrrDiagnosticReportData& report, const DiagD3dkmtCadenceCapture& capture)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.imbue(std::locale::classic());
    output << "EventIndex,Qpc,ElapsedMs\n";
    for (std::size_t i = 0; i < capture.timestamps.size(); ++i)
    {
        output << i << ',' << capture.timestamps[i] << ','
               << CsvElapsed(capture.timestamps[i], report) << '\n';
    }
    output.flush();
    return static_cast<bool>(output);
}
}

std::optional<VrrDiagnosticOutputFiles> WriteVrrDiagnosticFiles(
    const std::filesystem::path& baseDirectory,
    const VrrDiagnosticReportData& report,
    std::span<const VrrFrameSample> frames,
    const DiagD3dkmtCadenceCapture& d3dkmt)
{
    std::error_code error;
    const auto base = baseDirectory.empty() ? std::filesystem::current_path() : baseDirectory;
    std::filesystem::create_directories(base, error);
    if (error) return std::nullopt;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t directoryName[48]{};
    if (swprintf_s(directoryName, L"vrr-%04u%02u%02u-%02u%02u%02u", now.wYear,
            now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond) < 0)
        return std::nullopt;

    std::filesystem::path directory;
    bool created{};
    for (unsigned int suffix = 0; suffix < 1000; ++suffix)
    {
        auto name = std::wstring(directoryName);
        if (suffix)
        {
            wchar_t suffixText[16]{};
            swprintf_s(suffixText, L"-%02u", suffix);
            name += suffixText;
        }
        directory = base / name;
        error.clear();
        if (std::filesystem::create_directory(directory, error))
        {
            created = true;
            break;
        }
        if (error == std::errc::file_exists)
        {
            error.clear();
            continue;
        }
        if (error || suffix == 999) return std::nullopt;
    }
    if (!created) return std::nullopt;

    VrrDiagnosticOutputFiles files;
    files.directory = directory;
    files.report = directory / L"report.txt";
    files.framesCsv = directory / L"frames.csv";
    files.vblankCsv = directory / L"vblank.csv";
    if (!WriteReport(files.report, report, d3dkmt) ||
        !WriteFramesCsv(files.framesCsv, report, frames) ||
        !WriteVblankCsv(files.vblankCsv, report, d3dkmt))
    {
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
        return std::nullopt;
    }
    return files;
}
