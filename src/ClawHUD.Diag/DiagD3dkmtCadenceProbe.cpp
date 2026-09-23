#include "DiagD3dkmtCadenceProbe.h"

#include <algorithm>
#include <limits>

namespace
{
constexpr std::size_t kMaximumSamples = 100000;
constexpr NTSTATUS kStatusWaitForVblank = 0x00000000L;
constexpr NTSTATUS kStatusCancelled = 0x00000001L;

bool SameLuid(const LUID& left, const LUID& right) noexcept
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}
}

DiagD3dkmtCadenceProbe::~DiagD3dkmtCadenceProbe()
{
    Shutdown();
}

bool DiagD3dkmtCadenceProbe::Initialize(
    HMONITOR monitor, const VrrDisplayPath& expectedPath) noexcept
{
    Shutdown();
    failure_ = DiagD3dkmtCaptureFailure::None;
    failureStatus_.reset();
    timestamps_.clear();
    if (!monitor)
    {
        SetFailure(DiagD3dkmtCaptureFailure::InvalidTarget);
        return false;
    }

    gdi32_ = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (gdi32_)
    {
        waitForVerticalBlankEvent2_ = reinterpret_cast<WaitForVerticalBlankEvent2>(
            GetProcAddress(gdi32_, "D3DKMTWaitForVerticalBlankEvent2"));
        openAdapterFromHdc_ = reinterpret_cast<OpenAdapterFromHdc>(
            GetProcAddress(gdi32_, "D3DKMTOpenAdapterFromHdc"));
        closeAdapter_ = reinterpret_cast<CloseAdapter>(
            GetProcAddress(gdi32_, "D3DKMTCloseAdapter"));
    }
    if (!gdi32_ || !waitForVerticalBlankEvent2_ || !openAdapterFromHdc_ || !closeAdapter_)
    {
        SetFailure(DiagD3dkmtCaptureFailure::ApiUnavailable);
        Shutdown();
        return false;
    }

    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
    {
        SetFailure(DiagD3dkmtCaptureFailure::QpcUnavailable);
        Shutdown();
        return false;
    }
    qpcFrequency_ = frequency.QuadPart;

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&monitorInfo)))
    {
        SetFailure(DiagD3dkmtCaptureFailure::InvalidTarget);
        Shutdown();
        return false;
    }

    HDC dc = CreateDCW(monitorInfo.szDevice, monitorInfo.szDevice, nullptr, nullptr);
    if (!dc)
    {
        SetFailure(DiagD3dkmtCaptureFailure::InvalidTarget);
        Shutdown();
        return false;
    }
    D3DKMT_OPENADAPTERFROMHDC open{};
    open.hDc = dc;
    const auto openStatus = openAdapterFromHdc_(&open);
    DeleteDC(dc);
    if (openStatus != 0)
    {
        SetFailure(DiagD3dkmtCaptureFailure::AdapterOpenFailed,
            static_cast<std::int32_t>(openStatus));
        Shutdown();
        return false;
    }

    adapterHandle_ = open.hAdapter;
    adapterLuid_ = open.AdapterLuid;
    vidPnSourceId_ = open.VidPnSourceId;
    if (CompareStringOrdinal(monitorInfo.szDevice, -1,
            expectedPath.monitorDeviceName.c_str(), -1, TRUE) != CSTR_EQUAL ||
        !SameLuid(adapterLuid_, expectedPath.sourceAdapterLuid) ||
        vidPnSourceId_ != expectedPath.sourceId)
    {
        SetFailure(DiagD3dkmtCaptureFailure::DisplayPathMismatch);
        Shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

bool DiagD3dkmtCadenceProbe::Start() noexcept
{
    if (!initialized_ || !adapterHandle_ || sampling_ || sampler_.joinable()) return false;
    {
        std::lock_guard lock(mutex_);
        failure_ = DiagD3dkmtCaptureFailure::None;
        failureStatus_.reset();
        timestamps_.clear();
        try
        {
            timestamps_.reserve(4096);
        }
        catch (...)
        {
            failure_ = DiagD3dkmtCaptureFailure::SampleStorageFailed;
            return false;
        }
    }
    cancellationEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!cancellationEvent_)
    {
        SetFailure(DiagD3dkmtCaptureFailure::CancelEventFailed,
            static_cast<std::int32_t>(GetLastError()));
        return false;
    }
    stopRequested_.store(false, std::memory_order_release);
    try
    {
        sampler_ = std::thread(&DiagD3dkmtCadenceProbe::SampleLoop, this);
    }
    catch (...)
    {
        stopRequested_.store(true, std::memory_order_release);
        CloseHandle(cancellationEvent_);
        cancellationEvent_ = nullptr;
        SetFailure(DiagD3dkmtCaptureFailure::SamplerStartFailed);
        return false;
    }
    sampling_ = true;
    return true;
}

void DiagD3dkmtCadenceProbe::SampleLoop() noexcept
{
    for (;;)
    {
        if (stopRequested_.load(std::memory_order_acquire)) return;

        D3DKMT_WAITFORVERTICALBLANKEVENT2 args{};
        args.hAdapter = adapterHandle_;
        args.VidPnSourceId = vidPnSourceId_;
        args.NumObjects = 1;
        args.ObjectHandleArray[0] = cancellationEvent_;
        const auto status = waitForVerticalBlankEvent2_(&args);
        if (status == kStatusCancelled) return;
        if (status != kStatusWaitForVblank)
        {
            SetFailure(DiagD3dkmtCaptureFailure::WaitFailed,
                static_cast<std::int32_t>(status));
            return;
        }

        LARGE_INTEGER counter{};
        if (!QueryPerformanceCounter(&counter))
        {
            SetFailure(DiagD3dkmtCaptureFailure::QueryCounterFailed);
            return;
        }
        try
        {
            std::lock_guard lock(mutex_);
            if (timestamps_.size() >= kMaximumSamples)
            {
                failure_ = DiagD3dkmtCaptureFailure::SampleStorageFailed;
                return;
            }
            timestamps_.push_back(static_cast<std::uint64_t>(counter.QuadPart));
        }
        catch (...)
        {
            SetFailure(DiagD3dkmtCaptureFailure::SampleStorageFailed);
            return;
        }
    }
}

void DiagD3dkmtCadenceProbe::SetFailure(
    DiagD3dkmtCaptureFailure failure, std::optional<std::int32_t> status) noexcept
{
    std::lock_guard lock(mutex_);
    if (failure_ == DiagD3dkmtCaptureFailure::None)
    {
        failure_ = failure;
        failureStatus_ = status;
    }
}

void DiagD3dkmtCadenceProbe::CancelAndJoin() noexcept
{
    stopRequested_.store(true, std::memory_order_release);
    if (cancellationEvent_ && !SetEvent(cancellationEvent_))
        SetFailure(DiagD3dkmtCaptureFailure::CancelEventFailed,
            static_cast<std::int32_t>(GetLastError()));
    if (sampler_.joinable()) sampler_.join();
    sampling_ = false;

    if (adapterHandle_ && closeAdapter_)
    {
        D3DKMT_CLOSEADAPTER close{};
        close.hAdapter = adapterHandle_;
        const auto status = closeAdapter_(&close);
        if (status != 0)
            SetFailure(DiagD3dkmtCaptureFailure::AdapterCloseFailed,
                static_cast<std::int32_t>(status));
    }
    adapterHandle_ = {};
    if (cancellationEvent_)
    {
        CloseHandle(cancellationEvent_);
        cancellationEvent_ = nullptr;
    }
}

DiagD3dkmtCadenceCapture DiagD3dkmtCadenceProbe::Stop()
{
    CancelAndJoin();
    DiagD3dkmtCadenceCapture result;
    {
        std::lock_guard lock(mutex_);
        result.available = initialized_ && failure_ == DiagD3dkmtCaptureFailure::None;
        result.failure = failure_;
        result.failureStatus = failureStatus_;
        result.timestamps = timestamps_;
    }
    result.qpcFrequency = qpcFrequency_;
    result.adapterLuidLow = adapterLuid_.LowPart;
    result.adapterLuidHigh = adapterLuid_.HighPart;
    result.vidPnSourceId = vidPnSourceId_;
    initialized_ = false;
    return result;
}

void DiagD3dkmtCadenceProbe::Shutdown() noexcept
{
    CancelAndJoin();
    initialized_ = false;
    qpcFrequency_ = 0;
    vidPnSourceId_ = 0;
    adapterLuid_ = {};
    waitForVerticalBlankEvent2_ = nullptr;
    openAdapterFromHdc_ = nullptr;
    closeAdapter_ = nullptr;
    if (gdi32_)
    {
        FreeLibrary(gdi32_);
        gdi32_ = nullptr;
    }
}
