#include "IntelVrrDiagnosticProbe.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace clawhud
{
namespace
{
using Result = std::uint32_t;
using ApiHandle = void*;
using DeviceHandle = void*;
using OutputHandle = void*;
using InitFn = Result(__cdecl*)(void*, ApiHandle*);
using CloseFn = Result(__cdecl*)(ApiHandle);
using EnumerateDevicesFn = Result(__cdecl*)(ApiHandle, std::uint32_t*, DeviceHandle*);
using EnumerateOutputsFn = Result(__cdecl*)(DeviceHandle, std::uint32_t*, OutputHandle*);

// These are the minimal native layouts from Intel's official IGCL definitions:
// https://github.com/intel/drivers.gpu.control-library/blob/master/include/igcl_api.h
// Keep Size/Version, native bool, float ranges, and uint32_t version fields intact.
struct ApplicationId { std::uint32_t Data1{}; std::uint16_t Data2{}; std::uint16_t Data3{}; std::uint8_t Data4[8]{}; };
struct InitArgs
{
    std::uint32_t Size{};
    std::uint8_t Version{};
    std::uint32_t AppVersion{};
    std::uint32_t Flags{};
    std::uint32_t SupportedVersion{};
    ApplicationId ApplicationUID{};
};
struct MonitorParams
{
    std::uint32_t Size{};
    std::uint8_t Version{};
    bool IsIntelArcSyncSupported{};
    float MinimumRefreshRateInHz{};
    float MaximumRefreshRateInHz{};
    std::uint32_t MaxFrameTimeIncreaseInUs{};
    std::uint32_t MaxFrameTimeDecreaseInUs{};
};
enum class ArcProfile : std::uint32_t { Invalid, Recommended, Excellent, Good, Compatible, Off, Vesa, Custom };
struct ProfileParams
{
    std::uint32_t Size{};
    std::uint8_t Version{};
    ArcProfile IntelArcSyncProfile{};
    float MaxRefreshRateInHz{};
    float MinRefreshRateInHz{};
    std::uint32_t MaxFrameTimeIncreaseInUs{};
    std::uint32_t MaxFrameTimeDecreaseInUs{};
};
struct VblankArgs
{
    std::uint32_t Size{};
    std::uint8_t Version{};
    std::uint8_t NumOfTargets{};
    std::uint64_t VblankTS[16]{};
};
using ArcInfoFn = Result(__cdecl*)(OutputHandle, MonitorParams*);
using ArcProfileFn = Result(__cdecl*)(OutputHandle, ProfileParams*);
using VblankFn = Result(__cdecl*)(OutputHandle, VblankArgs*);

static_assert(offsetof(VblankArgs, VblankTS) == 8);
static_assert(sizeof(VblankArgs) == 136);

constexpr Result kSuccess = 0;
constexpr std::uint32_t kErrorNotAvailable = 0x40000007;
constexpr std::uint32_t kIgclApiVersion = (1u << 16) | 1u;

constexpr std::uint32_t MakeVersion(std::uint32_t major, std::uint32_t minor) noexcept
{
    return (major << 16) | (minor & 0xFFFFu);
}

template <typename T>
T Resolve(HMODULE module, const char* name) { return reinterpret_cast<T>(GetProcAddress(module, name)); }

const char* ProfileName(ArcProfile profile)
{
    switch (profile)
    {
    case ArcProfile::Recommended: return "RECOMMENDED";
    case ArcProfile::Excellent: return "EXCELLENT";
    case ArcProfile::Good: return "GOOD";
    case ArcProfile::Compatible: return "COMPATIBLE";
    case ArcProfile::Off: return "OFF";
    case ArcProfile::Vesa: return "VESA";
    case ArcProfile::Custom: return "CUSTOM";
    default: return "INVALID/UNKNOWN";
    }
}

std::wstring Wide(const std::string& value) { return { value.begin(), value.end() }; }
}

void RecordVblankTimestamp(VblankSeries& series, std::uint64_t timestamp)
{
    if (timestamp == 0) return;
    if (series.timestamps.empty() || timestamp > series.timestamps.back()) series.timestamps.push_back(timestamp);
    else if (timestamp < series.timestamps.back())
    {
        ++series.resetCount;
        series.timestamps.clear();
        series.timestamps.push_back(timestamp);
    }
}

VblankSummary SummarizeVblank(const VblankSeries& series)
{
    VblankSummary result{}; result.uniqueSamples = series.timestamps.size();
    if (result.uniqueSamples) { result.first = series.timestamps.front(); result.last = series.timestamps.back(); }
    if (result.uniqueSamples < 2) return result;
    std::vector<double> deltas;
    for (std::size_t i = 1; i < series.timestamps.size(); ++i)
        deltas.push_back(static_cast<double>(series.timestamps[i] - series.timestamps[i - 1]));
    result.validDeltas = deltas.size();
    std::sort(deltas.begin(), deltas.end());
    result.minimumDeltaUs = deltas.front(); result.maximumDeltaUs = deltas.back();
    result.averageDeltaUs = std::accumulate(deltas.begin(), deltas.end(), 0.0) / deltas.size();
    result.medianDeltaUs = deltas[deltas.size() / 2];
    if (deltas.size() % 2 == 0) result.medianDeltaUs = (deltas[deltas.size() / 2 - 1] + result.medianDeltaUs) / 2.0;
    if (result.medianDeltaUs > 0) result.measuredHz = 1000000.0 / result.medianDeltaUs;
    return result;
}

std::optional<double> UsableVblankMedian(const std::vector<VblankSummary>& summaries)
{
    if (summaries.size() != 1 || summaries.front().validDeltas == 0 || !summaries.front().measuredHz)
        return std::nullopt;
    return summaries.front().medianDeltaUs;
}

std::string IntelCtlResultName(std::uint32_t result)
{
    switch (result)
    {
    case 0: return "CTL_RESULT_SUCCESS";
    case 0x40000001: return "CTL_RESULT_ERROR_NOT_INITIALIZED";
    case 0x40000002: return "CTL_RESULT_ERROR_ALREADY_INITIALIZED";
    case 0x40000003: return "CTL_RESULT_ERROR_DEVICE_LOST";
    case 0x40000004: return "CTL_RESULT_ERROR_OUT_OF_HOST_MEMORY";
    case 0x40000005: return "CTL_RESULT_ERROR_OUT_OF_DEVICE_MEMORY";
    case 0x40000006: return "CTL_RESULT_ERROR_INSUFFICIENT_PERMISSIONS";
    case kErrorNotAvailable: return "CTL_RESULT_ERROR_NOT_AVAILABLE";
    case 0x40000008: return "CTL_RESULT_ERROR_UNINITIALIZED";
    case 0x40000009: return "CTL_RESULT_ERROR_UNSUPPORTED_VERSION";
    case 0x4000000A: return "CTL_RESULT_ERROR_UNSUPPORTED_FEATURE";
    case 0x4000000B: return "CTL_RESULT_ERROR_INVALID_ARGUMENT";
    case 0x4000000C: return "CTL_RESULT_ERROR_INVALID_API_HANDLE";
    case 0x4000000D: return "CTL_RESULT_ERROR_INVALID_NULL_HANDLE";
    case 0x4000000E: return "CTL_RESULT_ERROR_INVALID_NULL_POINTER";
    case 0x4000000F: return "CTL_RESULT_ERROR_INVALID_SIZE";
    default: return "UNKNOWN";
    }
}

IntelVrrDiagnosticProbe::~IntelVrrDiagnosticProbe() { Shutdown(); }

bool IntelVrrDiagnosticProbe::Initialize(std::wofstream& log)
{
    log_ = &log;
    library_ = LoadLibraryExW(L"ControlLib.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library_) { log << L"IGCL VRR Evidence: Unavailable\nReason: driver-installed System32 ControlLib.dll not found\n"; return false; }
    const auto init = Resolve<InitFn>(library_, "ctlInit");
    const auto close = Resolve<CloseFn>(library_, "ctlClose");
    const auto enumerateDevices = Resolve<EnumerateDevicesFn>(library_, "ctlEnumerateDevices");
    const auto enumerateOutputs = Resolve<EnumerateOutputsFn>(library_, "ctlEnumerateDisplayOutputs");
    const auto info = Resolve<ArcInfoFn>(library_, "ctlGetIntelArcSyncInfoForMonitor");
    const auto profile = Resolve<ArcProfileFn>(library_, "ctlGetIntelArcSyncProfile");
    const auto vblank = Resolve<VblankFn>(library_, "ctlGetVblankTimestamp");
    if (!init || !close || !enumerateDevices || !enumerateOutputs)
    {
        log << L"IGCL VRR Evidence: Unavailable\nReason: Required IGCL symbol unavailable\n";
        FreeLibrary(library_); library_ = nullptr; return false;
    }
    InitArgs args{}; args.Size = sizeof(args); args.AppVersion = kIgclApiVersion; args.Flags = 0;
    const Result result = init(&args, reinterpret_cast<ApiHandle*>(&apiHandle_));
    LogResult(log, L"ctlInit", result);
    if (result != kSuccess)
    {
        log << L"IGCL VRR Evidence: Unavailable\n";
        FreeLibrary(library_); library_ = nullptr; return false;
    }
    initialized_ = true; vblankAvailable_ = vblank != nullptr;
    log << L"IGCL requested API version: 1.1\nIGCL supported API version: " << (args.SupportedVersion >> 16) << L"." << (args.SupportedVersion & 0xFFFFu)
        << L"\n=== INTEL IGCL VRR STATE ===\nIGCL: Available\nVBlank API: " << (vblankAvailable_ ? L"Available" : L"Unavailable") << L"\n";
    std::uint32_t deviceCount{}; Result enumerateResult = enumerateDevices(apiHandle_, &deviceCount, nullptr);
    if (enumerateResult != kSuccess || !deviceCount) { LogResult(log, L"ctlEnumerateDevices", enumerateResult); return true; }
    std::vector<DeviceHandle> devices(deviceCount); enumerateResult = enumerateDevices(apiHandle_, &deviceCount, devices.data());
    if (enumerateResult != kSuccess) { LogResult(log, L"ctlEnumerateDevices", enumerateResult); return true; }
    for (const auto device : devices)
    {
        std::uint32_t outputCount{}; enumerateResult = enumerateOutputs(device, &outputCount, nullptr); if (enumerateResult != kSuccess) { LogResult(log, L"ctlEnumerateDisplayOutputs", enumerateResult); continue; }
        if (!outputCount) continue;
        std::vector<OutputHandle> handles(outputCount);
        const Result outputResult = enumerateOutputs(device, &outputCount, handles.data());
        if (outputResult != kSuccess) { LogResult(log, L"ctlEnumerateDisplayOutputs", outputResult); continue; }
        for (const auto handle : handles) { Output output{}; output.handle = handle; output.index = outputs_.size(); outputs_.push_back(std::move(output)); }
    }
    log << L"Outputs: " << outputs_.size() << L"\n";
    if (info == nullptr || profile == nullptr) log << L"Arc Sync state APIs: Unavailable\n";
    return true;
}

void IntelVrrDiagnosticProbe::LogState(std::wofstream& log)
{
    if (!initialized_ || !library_) return;
    const auto info = Resolve<ArcInfoFn>(library_, "ctlGetIntelArcSyncInfoForMonitor");
    const auto profile = Resolve<ArcProfileFn>(library_, "ctlGetIntelArcSyncProfile");
    if (!info || !profile) return;
    for (auto& output : outputs_)
    {
        MonitorParams capability{}; capability.Size = sizeof(capability);
        const Result infoResult = info(output.handle, &capability); output.vblankEligible = infoResult == kSuccess;
        log << L"Output[" << output.index << L"]\n";
        if (infoResult != kSuccess) { log << L"Arc Sync Supported: Unavailable\n"; LogResult(log, L"ctlGetIntelArcSyncInfoForMonitor", infoResult); continue; }
        log << L"Arc Sync Supported: " << (capability.IsIntelArcSyncSupported ? L"YES" : L"NO")
            << L"\nCapability Range: " << capability.MinimumRefreshRateInHz << L"-" << capability.MaximumRefreshRateInHz << L" Hz\n"
            << L"Capability Max Frame-Time Increase: " << capability.MaxFrameTimeIncreaseInUs << L" us\n"
            << L"Capability Max Frame-Time Decrease: " << capability.MaxFrameTimeDecreaseInUs << L" us\n";
        ProfileParams current{}; current.Size = sizeof(current); const Result profileResult = profile(output.handle, &current);
        if (profileResult != kSuccess) { LogResult(log, L"ctlGetIntelArcSyncProfile", profileResult); continue; }
        log << L"Current Profile: " << Wide(ProfileName(current.IntelArcSyncProfile)) << L"\nActive Range: " << current.MinRefreshRateInHz << L"-" << current.MaxRefreshRateInHz << L" Hz\n"
            << L"Profile Max Frame-Time Increase: " << current.MaxFrameTimeIncreaseInUs << L" us\nProfile Max Frame-Time Decrease: " << current.MaxFrameTimeDecreaseInUs << L" us\n";
    }
}

void IntelVrrDiagnosticProbe::StartSampling()
{
    if (!initialized_ || !vblankAvailable_ || outputs_.empty()) return;
    for (auto& output : outputs_)
    {
        output.series.clear(); output.series.resize(16);
        output.lastVblankError = 0; output.vblankErrorCount = 0; output.vblankSuccessCount = 0;
        for (std::size_t i = 0; i < output.series.size(); ++i) { output.series[i].output = output.index; output.series[i].target = i; }
    }
    sampling_ = true; sampler_ = std::thread(&IntelVrrDiagnosticProbe::SampleLoop, this);
}

void IntelVrrDiagnosticProbe::SampleLoop()
{
    const auto vblank = Resolve<VblankFn>(library_, "ctlGetVblankTimestamp");
    if (!vblank) return;
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_MODIFY_STATE | SYNCHRONIZE);
    LARGE_INTEGER due{}; due.QuadPart = -10'000;
    const bool timerReady = timer != nullptr && SetWaitableTimer(timer, &due, 1, nullptr, nullptr, FALSE) != FALSE;
    if (!timerReady && timer) { CloseHandle(timer); timer = nullptr; }
    while (sampling_)
    {
        for (auto& output : outputs_)
        {
            if (!output.vblankEligible) continue;
            VblankArgs args{}; args.Size = sizeof(args); const Result result = vblank(output.handle, &args);
            if (result != kSuccess)
            {
                output.lastVblankError = result; ++output.vblankErrorCount; continue;
            }
            ++output.vblankSuccessCount;
            const auto count = std::min<std::size_t>(args.NumOfTargets, 16);
            for (std::size_t i = 0; i < count; ++i) RecordVblankTimestamp(output.series[i], args.VblankTS[i]);
        }
        if (timer) WaitForSingleObject(timer, 10);
        else std::this_thread::sleep_for(kVblankPollInterval);
    }
    if (timer) CloseHandle(timer);
}

std::vector<VblankSummary> IntelVrrDiagnosticProbe::StopSampling(std::wofstream& log, const wchar_t* phase)
{
    sampling_ = false; if (sampler_.joinable()) sampler_.join(); std::vector<VblankSummary> summaries;
    log << L"IGCL VBlank (" << phase << L"):\n";
    if (!initialized_ || !vblankAvailable_) { log << L"Unavailable\n"; return summaries; }
    for (const auto& output : outputs_)
    {
        if (!output.vblankEligible)
        {
            log << L"Output[" << output.index << L"] VBlank sampling skipped: Arc Sync output query unavailable\n";
        }
        if (output.vblankErrorCount > 0)
        {
            log << L"Output[" << output.index << L"] VBlank calls succeeded: " << output.vblankSuccessCount << L"\n";
            log << L"Output[" << output.index << L"] VBlank call failures: " << output.vblankErrorCount << L"\n";
            LogResult(log, L"ctlGetVblankTimestamp", output.lastVblankError);
        }
        else if (output.vblankEligible)
        {
            log << L"Output[" << output.index << L"] VBlank calls succeeded: " << output.vblankSuccessCount << L"\n";
        }
        for (const auto& series : output.series)
        {
            if (series.timestamps.empty()) continue; const auto summary = SummarizeVblank(series); summaries.push_back(summary);
            log << L"Output[" << series.output << L"] Target[" << series.target << L"]\nUnique timestamps: " << summary.uniqueSamples << L"\nValid deltas: " << summary.validDeltas
                << L"\nFirst timestamp: " << summary.first << L"\nLast timestamp: " << summary.last << L"\n";
            if (summary.validDeltas > 0)
            {
                log << L"Average delta us: " << summary.averageDeltaUs << L"\nMedian delta us: " << summary.medianDeltaUs
                    << L"\nMin/Max delta us: " << summary.minimumDeltaUs << L"/" << summary.maximumDeltaUs
                    << L"\nMeasured VBlank Hz: " << *summary.measuredHz << L"\n";
            }
            else
            {
                log << L"Average delta us: Unavailable\nMedian delta us: Unavailable\nMin/Max delta us: Unavailable\nMeasured VBlank Hz: Unavailable\n";
            }
            log << L"Reset events: " << series.resetCount << L"\n";
        }
    }
    if (summaries.empty()) log << L"Unavailable\n";
    return summaries;
}

void IntelVrrDiagnosticProbe::LogResult(std::wofstream& log, const wchar_t* operation, std::uint32_t result) const
{
    std::ostringstream value; value << "0x" << std::hex << std::uppercase << result;
    log << operation << L": " << Wide(IntelCtlResultName(result)) << L" (" << Wide(value.str()) << L")\n";
}

void IntelVrrDiagnosticProbe::Shutdown()
{
    sampling_ = false; if (sampler_.joinable()) sampler_.join();
    if (initialized_ && apiHandle_ && library_)
    {
        const auto close = Resolve<CloseFn>(library_, "ctlClose");
        if (close)
        {
            const Result result = close(apiHandle_);
            if (result != kSuccess && log_) LogResult(*log_, L"ctlClose", result);
        }
    }
    apiHandle_ = nullptr; initialized_ = false; outputs_.clear();
    if (library_) { FreeLibrary(library_); library_ = nullptr; }
    log_ = nullptr;
}
}
