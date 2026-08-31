#include "D3dkmtVblankProbe.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace
{
using WaitArgs = D3DKMT_WAITFORVERTICALBLANKEVENT;
using OpenArgs = D3DKMT_OPENADAPTERFROMHDC;
using CloseArgs = D3DKMT_CLOSEADAPTER;

template <typename T>
T ResolveGdi32(const char* name)
{
    auto* module = GetModuleHandleW(L"gdi32.dll");
    return module ? reinterpret_cast<T>(GetProcAddress(module, name)) : nullptr;
}

bool QueryQpc(std::uint64_t& value)
{
    LARGE_INTEGER counter{};
    if (!QueryPerformanceCounter(&counter)) return false;
    value = static_cast<std::uint64_t>(counter.QuadPart);
    return true;
}

std::wstring Hex(std::uint64_t value)
{
    std::wostringstream output;
    output << L"0x" << std::hex << value << std::dec;
    return output.str();
}
}

D3dkmtVblankStatistics CalculateD3dkmtVblankStatistics(
    const std::vector<std::uint64_t>& timestamps,
    std::size_t failedWaits,
    std::int64_t qpcFrequency) noexcept
{
    D3dkmtVblankStatistics result{};
    result.sampleCount = timestamps.size();
    result.successfulWaits = timestamps.size();
    result.failedWaits = failedWaits;
    if (timestamps.empty()) return result;
    result.firstQpc = timestamps.front();
    result.lastQpc = timestamps.back();
    if (qpcFrequency > 0 && result.lastQpc >= result.firstQpc)
        result.durationSeconds = static_cast<double>(result.lastQpc - result.firstQpc) /
            static_cast<double>(qpcFrequency);
    if (timestamps.size() < 2 || qpcFrequency <= 0) return result;

    std::vector<double> deltasMs;
    deltasMs.reserve(timestamps.size() - 1);
    for (std::size_t index = 1; index < timestamps.size(); ++index)
    {
        if (timestamps[index] <= timestamps[index - 1]) continue;
        deltasMs.push_back(1000.0 * static_cast<double>(
            timestamps[index] - timestamps[index - 1]) / qpcFrequency);
    }
    if (deltasMs.empty()) return result;
    result.minimumDeltaMs = *std::min_element(deltasMs.begin(), deltasMs.end());
    result.maximumDeltaMs = *std::max_element(deltasMs.begin(), deltasMs.end());
    result.averageDeltaMs = std::accumulate(deltasMs.begin(), deltasMs.end(), 0.0) /
        static_cast<double>(deltasMs.size());
    std::sort(deltasMs.begin(), deltasMs.end());
    const auto middle = deltasMs.size() / 2;
    result.medianDeltaMs = deltasMs.size() % 2
        ? deltasMs[middle] : (deltasMs[middle - 1] + deltasMs[middle]) / 2.0;
    const double elapsedSeconds = result.durationSeconds;
    if (elapsedSeconds > 0.0)
        result.measuredHzElapsed = static_cast<double>(deltasMs.size()) / elapsedSeconds;
    if (result.medianDeltaMs > 0.0)
        result.measuredHzMedian = 1000.0 / result.medianDeltaMs;
    return result;
}

std::vector<D3dkmtVblankWindow> CalculateD3dkmtVblankWindows(
    const std::vector<std::uint64_t>& timestamps,
    std::int64_t qpcFrequency,
    double windowSeconds)
{
    std::vector<D3dkmtVblankWindow> result;
    if (timestamps.empty() || qpcFrequency <= 0 || !std::isfinite(windowSeconds) ||
        windowSeconds <= 0.0 || timestamps.back() < timestamps.front())
        return result;

    const double durationSeconds = static_cast<double>(timestamps.back() - timestamps.front()) /
        static_cast<double>(qpcFrequency);
    const auto windowCount = static_cast<std::size_t>(
        std::floor(durationSeconds / windowSeconds));
    result.reserve(windowCount);
    for (std::size_t index = 0; index < windowCount; ++index)
    {
        const double start = static_cast<double>(index) * windowSeconds;
        const double end = start + windowSeconds;
        const auto count = static_cast<std::size_t>(std::count_if(
            timestamps.begin(), timestamps.end(), [&](std::uint64_t timestamp)
            {
                const double relative = static_cast<double>(timestamp - timestamps.front()) /
                    static_cast<double>(qpcFrequency);
                return relative >= start && relative < end;
            }));
        result.push_back({ start, end, count, windowSeconds,
            static_cast<double>(count) / windowSeconds });
    }
    return result;
}

D3dkmtVblankProbe::~D3dkmtVblankProbe()
{
    Shutdown();
}

bool D3dkmtVblankProbe::Initialize(std::wofstream& log, HMONITOR monitor)
{
    Shutdown();
    available_ = false;
    waitForVerticalBlankEvent_ = ResolveGdi32<WaitForVerticalBlankEvent>(
        "D3DKMTWaitForVerticalBlankEvent");
    openAdapterFromHdc_ = ResolveGdi32<OpenAdapterFromHdc>(
        "D3DKMTOpenAdapterFromHdc");
    closeAdapter_ = ResolveGdi32<CloseAdapter>("D3DKMTCloseAdapter");
    if (!waitForVerticalBlankEvent_ || !openAdapterFromHdc_ || !closeAdapter_)
    {
        log << L"D3DKMT VBlank Cadence: Unavailable\nReason: Required GDI32 API unavailable\n\n";
        return false;
    }

    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency))
    {
        log << L"D3DKMT VBlank Cadence: Unavailable\nReason: QueryPerformanceFrequency failed\n\n";
        return false;
    }
    qpcFrequency_ = frequency.QuadPart;

    MONITORINFOEXW monitorInfo{ sizeof(monitorInfo) };
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo))
    {
        log << L"D3DKMT VBlank Cadence: Unavailable\nReason: Target monitor mapping failed\n\n";
        return false;
    }
    monitorName_ = monitorInfo.szDevice;
    DISPLAY_DEVICEW display{ sizeof(display) };
    if (!EnumDisplayDevicesW(monitorInfo.szDevice, 0, &display, 0))
    {
        log << L"D3DKMT VBlank Cadence: Unavailable\nReason: Display device mapping failed\n\n";
        return false;
    }
    displayName_ = display.DeviceName;
    HDC dc = CreateDCW(monitorInfo.szDevice, monitorInfo.szDevice, nullptr, nullptr);
    if (!dc)
    {
        log << L"D3DKMT VBlank Cadence: Unavailable\nReason: CreateDC failed\n\n";
        return false;
    }
    OpenArgs open{};
    open.hDc = dc;
    const NTSTATUS status = openAdapterFromHdc_(&open);
    DeleteDC(dc);
    if (status != 0)
    {
        log << L"D3DKMT VBlank Cadence: Unavailable\nReason: D3DKMTOpenAdapterFromHdc failed\nNTSTATUS: 0x"
            << std::hex << static_cast<unsigned long>(status) << std::dec << L"\n\n";
        return false;
    }
    adapterHandle_ = open.hAdapter;
    vidPnSourceId_ = open.VidPnSourceId;
    adapterLuid_ = open.AdapterLuid;
    const auto luidValue = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(adapterLuid_.HighPart)) << 32) |
        static_cast<std::uint32_t>(adapterLuid_.LowPart);
    log << L"D3DKMT Adapter LUID: " << Hex(luidValue) << L"\n"
        << L"D3DKMT Adapter Handle: " << adapterHandle_ << L"\n"
        << L"VidPnSourceId: " << vidPnSourceId_ << L"\n"
        << L"Display DeviceName: " << displayName_ << L"\n"
        << L"Monitor: " << monitorName_ << L"\n\n";
    available_ = true;
    return true;
}

void D3dkmtVblankProbe::Start()
{
    if (!adapterHandle_ || !waitForVerticalBlankEvent_ || sampling_.exchange(true)) return;
    {
        std::lock_guard lock(samplesMutex_);
        timestamps_.clear();
        failedWaits_ = 0;
        lastFailureStatus_.reset();
        timestamps_.reserve(4096);
    }
    stopRequested_ = false;
    sampler_ = std::thread(&D3dkmtVblankProbe::SampleLoop, this);
}

void D3dkmtVblankProbe::SampleLoop()
{
    while (!stopRequested_)
    {
        WaitArgs args{};
        args.hAdapter = adapterHandle_;
        args.VidPnSourceId = vidPnSourceId_;
        const NTSTATUS status = waitForVerticalBlankEvent_(&args);
        if (status != 0)
        {
            // Stop on an immediate error so an unavailable path cannot perturb
            // the game or VRR diagnostic with a tight retry loop.
            std::lock_guard lock(samplesMutex_);
            ++failedWaits_;
            lastFailureStatus_ = status;
            break;
        }

        std::uint64_t qpc{};
        if (!QueryQpc(qpc))
            continue;
        std::lock_guard lock(samplesMutex_);
        if (timestamps_.size() < 100000)
            timestamps_.push_back(qpc);
    }
}

D3dkmtVblankResult D3dkmtVblankProbe::Stop()
{
    stopRequested_ = true;
    if (sampler_.joinable()) sampler_.join();
    sampling_ = false;
    std::vector<std::uint64_t> timestamps;
    std::size_t failures{};
    std::optional<NTSTATUS> lastFailure;
    {
        std::lock_guard lock(samplesMutex_);
        timestamps = timestamps_;
        failures = failedWaits_;
        lastFailure = lastFailureStatus_;
    }
    return { available_ && !lastFailure.has_value(),
        CalculateD3dkmtVblankStatistics(timestamps, failures, qpcFrequency_),
        CalculateD3dkmtVblankWindows(timestamps, qpcFrequency_), lastFailure };
}

void WriteD3dkmtVblankDiagnostic(
    std::wofstream& log, const wchar_t* phase, const D3dkmtVblankResult& result)
{
    log << L"=== D3DKMT VBLANK CADENCE - " << phase << L" ===\n"
        << L"Status: " << (result.available ? L"Available" : L"Unavailable") << L"\n"
        << L"API: D3DKMTWaitForVerticalBlankEvent\n";
    if (!result.available)
    {
        log << L"Reason: " << (result.failureStatus
            ? L"D3DKMTWaitForVerticalBlankEvent failed"
            : L"Adapter/source or required API initialization failed") << L"\n";
        if (result.failureStatus)
            log << L"Failure NTSTATUS: 0x" << std::hex
                << static_cast<unsigned long>(*result.failureStatus) << std::dec << L"\n\n";
        return;
    }
    const auto& statistics = result.statistics;
    log << L"Sample Count: " << statistics.sampleCount << L"\n"
        << L"Successful Waits: " << statistics.successfulWaits << L"\n"
        << L"Failed Waits: " << statistics.failedWaits << L"\n"
        << L"Cadence Hz (elapsed/event-count): "
        << (statistics.measuredHzElapsed ? std::to_wstring(*statistics.measuredHzElapsed) : L"Unavailable") << L"\n"
        << L"Median wait interval: " << statistics.medianDeltaMs << L" ms\n"
        << L"Median interval-derived rate: "
        << (statistics.measuredHzMedian ? std::to_wstring(*statistics.measuredHzMedian) : L"Unavailable")
        << L" Hz (non-authoritative)\n";
    if (statistics.sampleCount > 0)
        log << L"First QPC: " << statistics.firstQpc << L"\n"
            << L"Last QPC: " << statistics.lastQpc << L"\n"
            << L"Duration: " << std::fixed << std::setprecision(3)
            << statistics.durationSeconds << L" s\n"
            << L"Minimum Delta: " << statistics.minimumDeltaMs << L" ms\n"
            << L"Average Delta: " << statistics.averageDeltaMs << L" ms\n"
            << L"Maximum Delta: " << statistics.maximumDeltaMs << L" ms\n";
    log << L"Windowed cadence (1.000 s):\n";
    double minimumHz{}, maximumHz{}, totalHz{};
    if (result.windows.empty()) log << L"  No complete windows\n";
    for (std::size_t index = 0; index < result.windows.size(); ++index)
    {
        const auto& window = result.windows[index];
        if (index == 0) minimumHz = maximumHz = window.measuredHz;
        else { minimumHz = std::min(minimumHz, window.measuredHz); maximumHz = std::max(maximumHz, window.measuredHz); }
        totalHz += window.measuredHz;
        log << L"  " << std::fixed << std::setprecision(3) << window.startSeconds << L"-"
            << window.endSeconds << L" s: events=" << window.eventCount
            << L", Hz=" << std::setprecision(1) << window.measuredHz << L"\n";
    }
    if (!result.windows.empty())
        log << L"Windowed Hz Min: " << minimumHz << L"\n"
            << L"Windowed Hz Max: " << maximumHz << L"\n"
            << L"Windowed Hz Avg: " << totalHz / result.windows.size() << L"\n";
    if (result.failureStatus)
        log << L"Failure status: 0x" << std::hex
            << static_cast<unsigned long>(*result.failureStatus) << std::dec << L"\n";
    log << L"Interpretation: Supporting cadence evidence; not authoritative physical scanout timing.\n\n";
}

void D3dkmtVblankProbe::Shutdown() noexcept
{
    stopRequested_ = true;
    if (sampler_.joinable()) sampler_.join();
    sampling_ = false;
    if (adapterHandle_ && closeAdapter_)
    {
        CloseArgs close{};
        close.hAdapter = adapterHandle_;
        closeAdapter_(&close);
    }
    adapterHandle_ = {};
    available_ = false;
}
