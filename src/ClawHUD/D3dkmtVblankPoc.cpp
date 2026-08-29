#include "D3dkmtVblankPoc.h"

#include <windows.h>
#include <bcrypt.h>
#include <d3dkmthk.h>

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

D3dkmtVblankPoc::~D3dkmtVblankPoc()
{
    Shutdown();
}

bool D3dkmtVblankPoc::Initialize(std::wofstream& log)
{
    Shutdown();
    waitForVerticalBlankEvent_ = ResolveGdi32<WaitForVerticalBlankEvent>(
        "D3DKMTWaitForVerticalBlankEvent");
    openAdapterFromHdc_ = ResolveGdi32<OpenAdapterFromHdc>(
        "D3DKMTOpenAdapterFromHdc");
    closeAdapter_ = ResolveGdi32<CloseAdapter>("D3DKMTCloseAdapter");
    if (!waitForVerticalBlankEvent_ || !openAdapterFromHdc_ || !closeAdapter_)
    {
        log << L"D3DKMT VBlank: Unavailable\nReason: Required GDI32 API unavailable\n\n";
        return false;
    }

    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency))
    {
        log << L"D3DKMT VBlank: Unavailable\nReason: QueryPerformanceFrequency failed\n\n";
        return false;
    }
    qpcFrequency_ = frequency.QuadPart;

    const HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW monitorInfo{ sizeof(monitorInfo) };
    if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo))
    {
        log << L"D3DKMT VBlank: Unavailable\nReason: Active monitor mapping failed\n\n";
        return false;
    }
    monitorName_ = monitorInfo.szDevice;
    DISPLAY_DEVICEW display{ sizeof(display) };
    if (!EnumDisplayDevicesW(monitorInfo.szDevice, 0, &display, 0))
    {
        log << L"D3DKMT VBlank: Unavailable\nReason: Display device mapping failed\n\n";
        return false;
    }
    displayName_ = display.DeviceName;
    HDC dc = CreateDCW(monitorInfo.szDevice, monitorInfo.szDevice, nullptr, nullptr);
    if (!dc)
    {
        log << L"D3DKMT VBlank: Unavailable\nReason: CreateDC failed\n\n";
        return false;
    }
    OpenArgs open{};
    open.hDc = dc;
    const long status = openAdapterFromHdc_(&open);
    DeleteDC(dc);
    if (status != 0)
    {
        log << L"D3DKMT VBlank: Unavailable\nReason: D3DKMTOpenAdapterFromHdc failed 0x"
            << std::hex << static_cast<unsigned long>(status) << std::dec << L"\n\n";
        return false;
    }
    adapterHandle_ = reinterpret_cast<void*>(static_cast<std::uintptr_t>(open.hAdapter));
    vidPnSourceId_ = open.VidPnSourceId;
    adapterLuid_ = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(open.AdapterLuid.HighPart)) << 32) |
        static_cast<std::uint32_t>(open.AdapterLuid.LowPart);
    log << L"D3DKMT Adapter LUID: " << Hex(adapterLuid_) << L"\n"
        << L"D3DKMT Adapter Handle: " << Hex(reinterpret_cast<std::uintptr_t>(adapterHandle_)) << L"\n"
        << L"VidPnSourceId: " << vidPnSourceId_ << L"\n"
        << L"Display DeviceName: " << displayName_ << L"\n"
        << L"Monitor: " << monitorName_ << L"\n\n";
    return true;
}

void D3dkmtVblankPoc::Start()
{
    if (!adapterHandle_ || !waitForVerticalBlankEvent_ || sampling_.exchange(true)) return;
    {
        std::lock_guard lock(samplesMutex_);
        timestamps_.clear();
        failedWaits_ = 0;
        lastFailureStatus_ = 0;
        timestamps_.reserve(4096);
    }
    stopRequested_ = false;
    sampler_ = std::thread(&D3dkmtVblankPoc::SampleLoop, this);
}

void D3dkmtVblankPoc::SampleLoop()
{
    while (!stopRequested_)
    {
        WaitArgs args{};
        args.hAdapter = static_cast<D3DKMT_HANDLE>(reinterpret_cast<std::uintptr_t>(adapterHandle_));
        args.VidPnSourceId = vidPnSourceId_;
        const long status = waitForVerticalBlankEvent_(&args);
        std::uint64_t qpc{};
        std::lock_guard lock(samplesMutex_);
        if (status == 0)
        {
            if (QueryQpc(qpc) && timestamps_.size() < 100000)
                timestamps_.push_back(qpc);
        }
        else
        {
            ++failedWaits_;
            lastFailureStatus_ = status;
        }
    }
}

D3dkmtVblankStatistics D3dkmtVblankPoc::Stop(
    std::wofstream& log, const wchar_t* phase)
{
    stopRequested_ = true;
    if (sampler_.joinable()) sampler_.join();
    sampling_ = false;
    std::vector<std::uint64_t> timestamps;
    std::size_t failures{};
    long lastFailure{};
    {
        std::lock_guard lock(samplesMutex_);
        timestamps = timestamps_;
        failures = failedWaits_;
        lastFailure = lastFailureStatus_;
    }
    const auto result = CalculateD3dkmtVblankStatistics(timestamps, failures, qpcFrequency_);
    log << L"=== D3DKMT VBLANK POC - " << phase << L" ===\n"
        << L"API: D3DKMTWaitForVerticalBlankEvent\n"
        << L"Sample Count: " << result.sampleCount << L"\n"
        << L"Successful Waits: " << result.successfulWaits << L"\n"
        << L"Failed Waits: " << result.failedWaits << L"\n";
    if (result.sampleCount > 0)
        log << L"First QPC: " << result.firstQpc << L"\n"
            << L"Last QPC: " << result.lastQpc << L"\n"
            << L"Duration: " << std::fixed << std::setprecision(3)
            << result.durationSeconds << L" s\n";
    if (result.sampleCount >= 2)
        log << std::fixed << std::setprecision(3)
            << L"Minimum Delta: " << result.minimumDeltaMs << L" ms\n"
            << L"Median Delta: " << result.medianDeltaMs << L" ms\n"
            << L"Average Delta: " << result.averageDeltaMs << L" ms\n"
            << L"Maximum Delta: " << result.maximumDeltaMs << L" ms\n"
            << L"Measured Hz (elapsed): " << (result.measuredHzElapsed ? std::to_wstring(*result.measuredHzElapsed) : L"Unavailable") << L"\n"
            << L"Measured Hz (median): " << (result.measuredHzMedian ? std::to_wstring(*result.measuredHzMedian) : L"Unavailable") << L"\n";
    else
        log << L"Measured Hz: Unavailable\n";
    if (lastFailure)
        log << L"Last failure status: 0x" << std::hex
            << static_cast<unsigned long>(lastFailure) << std::dec << L"\n";
    log << L"Interpretation: EXPERIMENTAL / MANUAL REVIEW REQUIRED\n\n";
    return result;
}

void D3dkmtVblankPoc::Shutdown() noexcept
{
    stopRequested_ = true;
    if (sampler_.joinable()) sampler_.join();
    sampling_ = false;
    if (adapterHandle_ && closeAdapter_)
    {
        CloseArgs close{};
        close.hAdapter = static_cast<D3DKMT_HANDLE>(reinterpret_cast<std::uintptr_t>(adapterHandle_));
        closeAdapter_(&close);
    }
    adapterHandle_ = nullptr;
}
