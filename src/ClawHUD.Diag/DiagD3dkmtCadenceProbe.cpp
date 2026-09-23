#include "DiagD3dkmtCadenceProbe.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
constexpr std::size_t kMaximumSamples = 100000;
constexpr NTSTATUS kStatusWaitForVblank = 0x00000000L;

bool SameLuid(const LUID& left, const LUID& right) noexcept
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

std::optional<std::int32_t> LastErrorStatus() noexcept
{
    const auto error = GetLastError();
    if (error == ERROR_SUCCESS) return std::nullopt;
    return static_cast<std::int32_t>(error);
}
}

DiagD3dkmtCadenceProbe::~DiagD3dkmtCadenceProbe()
{
    Shutdown();
}

DiagD3dkmtCadenceProbe::DiagD3dkmtCadenceProbe(DiagD3dkmtCadenceProbeApi api)
    : api_(std::move(api))
{
}

bool DiagD3dkmtCadenceProbe::Initialize(
    HMONITOR monitor, const VrrDisplayPath& expectedPath) noexcept
{
    Shutdown();
    attempted_ = true;
    failure_ = DiagD3dkmtCaptureFailure::None;
    failureDetail_ = {};
    failureStatusDomain_ = DiagD3dkmtFailureStatusDomain::None;
    failureStatus_.reset();
    timestamps_.clear();
    qpcFrequency_ = 0;
    adapterLuid_ = {};
    vidPnSourceId_ = 0;
    targetIdentified_ = false;
    if (!monitor)
    {
        SetFailure(DiagD3dkmtCaptureFailure::InvalidTarget, "monitor handle is null");
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    gdi32_ = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!gdi32_)
    {
        const auto error = LastErrorStatus();
        SetFailure(DiagD3dkmtCaptureFailure::ApiUnavailable,
            "LoadLibraryExW(gdi32.dll)", error,
            error ? DiagD3dkmtFailureStatusDomain::Win32 :
                DiagD3dkmtFailureStatusDomain::None);
        Shutdown();
        return false;
    }

    waitForVerticalBlankEvent2_ = reinterpret_cast<WaitForVerticalBlankEvent2>(
        GetProcAddress(gdi32_, "D3DKMTWaitForVerticalBlankEvent2"));
    openAdapterFromHdc_ = reinterpret_cast<OpenAdapterFromHdc>(
        GetProcAddress(gdi32_, "D3DKMTOpenAdapterFromHdc"));
    closeAdapter_ = reinterpret_cast<CloseAdapter>(
        GetProcAddress(gdi32_, "D3DKMTCloseAdapter"));
    if (!waitForVerticalBlankEvent2_)
    {
        SetFailure(DiagD3dkmtCaptureFailure::ApiUnavailable,
            "missing gdi32.dll export D3DKMTWaitForVerticalBlankEvent2");
        Shutdown();
        return false;
    }
    if (!openAdapterFromHdc_)
    {
        SetFailure(DiagD3dkmtCaptureFailure::ApiUnavailable,
            "missing gdi32.dll export D3DKMTOpenAdapterFromHdc");
        Shutdown();
        return false;
    }
    if (!closeAdapter_)
    {
        SetFailure(DiagD3dkmtCaptureFailure::ApiUnavailable,
            "missing gdi32.dll export D3DKMTCloseAdapter");
        Shutdown();
        return false;
    }

    LARGE_INTEGER frequency{};
    SetLastError(ERROR_SUCCESS);
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
    {
        const auto error = LastErrorStatus();
        SetFailure(DiagD3dkmtCaptureFailure::QpcUnavailable,
            "QueryPerformanceFrequency", error,
            error ? DiagD3dkmtFailureStatusDomain::Win32 :
                DiagD3dkmtFailureStatusDomain::None);
        Shutdown();
        return false;
    }
    qpcFrequency_ = frequency.QuadPart;

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    SetLastError(ERROR_SUCCESS);
    if (!GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&monitorInfo)))
    {
        const auto error = LastErrorStatus();
        SetFailure(DiagD3dkmtCaptureFailure::InvalidTarget, "GetMonitorInfoW", error,
            error ? DiagD3dkmtFailureStatusDomain::Win32 :
                DiagD3dkmtFailureStatusDomain::None);
        Shutdown();
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    HDC dc = CreateDCW(monitorInfo.szDevice, monitorInfo.szDevice, nullptr, nullptr);
    if (!dc)
    {
        const auto error = LastErrorStatus();
        SetFailure(DiagD3dkmtCaptureFailure::InvalidTarget,
            "CreateDCW for target monitor", error,
            error ? DiagD3dkmtFailureStatusDomain::Win32 :
                DiagD3dkmtFailureStatusDomain::None);
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
            "D3DKMTOpenAdapterFromHdc", static_cast<std::int32_t>(openStatus),
            DiagD3dkmtFailureStatusDomain::NtStatus);
        Shutdown();
        return false;
    }
    targetIdentified_ = true;
    adapterLuid_ = open.AdapterLuid;
    vidPnSourceId_ = open.VidPnSourceId;

    if (!InitializeResolvedTarget(monitorInfo.szDevice, open.hAdapter,
            open.AdapterLuid, open.VidPnSourceId, qpcFrequency_, expectedPath))
    {
        Shutdown();
        return false;
    }

    return true;
}

bool DiagD3dkmtCadenceProbe::InitializeResolvedTarget(
    std::wstring_view monitorDeviceName, D3DKMT_HANDLE adapterHandle,
    LUID adapterLuid, UINT32 vidPnSourceId, std::int64_t qpcFrequency,
    const VrrDisplayPath& expectedPath) noexcept
{
    attempted_ = true;
    if (initialized_ || sampling_ || sampler_.joinable())
    {
        SetFailure(DiagD3dkmtCaptureFailure::InvalidTarget,
            "probe is already initialized or sampling");
        return false;
    }
    failure_ = DiagD3dkmtCaptureFailure::None;
    failureDetail_ = {};
    failureStatusDomain_ = DiagD3dkmtFailureStatusDomain::None;
    failureStatus_.reset();
    adapterHandle_ = adapterHandle;
    adapterLuid_ = adapterLuid;
    vidPnSourceId_ = vidPnSourceId;
    targetIdentified_ = adapterHandle != 0;
    qpcFrequency_ = qpcFrequency;
    if (monitorDeviceName.empty() || expectedPath.monitorDeviceName.empty() ||
        monitorDeviceName.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        expectedPath.monitorDeviceName.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        CompareStringOrdinal(monitorDeviceName.data(), static_cast<int>(monitorDeviceName.size()),
            expectedPath.monitorDeviceName.data(),
            static_cast<int>(expectedPath.monitorDeviceName.size()), TRUE) != CSTR_EQUAL ||
        !SameLuid(adapterLuid, expectedPath.sourceAdapterLuid) ||
        vidPnSourceId != expectedPath.sourceId)
    {
        SetFailure(DiagD3dkmtCaptureFailure::DisplayPathMismatch,
            "monitor, adapter LUID, or VidPnSourceId does not match display path");
        return false;
    }
    if (!adapterHandle || qpcFrequency <= 0)
    {
        SetFailure(qpcFrequency <= 0 ? DiagD3dkmtCaptureFailure::QpcUnavailable :
            DiagD3dkmtCaptureFailure::AdapterOpenFailed,
            qpcFrequency <= 0 ? "non-positive QPC frequency" : "adapter handle is null");
        return false;
    }
    if ((!api_.waitForVerticalBlankEvent2 && !waitForVerticalBlankEvent2_) ||
        (!api_.closeAdapter && !closeAdapter_))
    {
        SetFailure(DiagD3dkmtCaptureFailure::ApiUnavailable,
            "required D3DKMT wait or close function is unavailable");
        return false;
    }

    timestamps_.clear();

    initialized_ = true;
    return true;
}

bool DiagD3dkmtCadenceProbe::Start() noexcept
{
    if (!initialized_ || !adapterHandle_ || sampling_ || sampler_.joinable()) return false;
    {
        std::lock_guard lock(mutex_);
        failure_ = DiagD3dkmtCaptureFailure::None;
        failureDetail_ = {};
        failureStatusDomain_ = DiagD3dkmtFailureStatusDomain::None;
        failureStatus_.reset();
        timestamps_.clear();
        try
        {
            timestamps_.reserve(4096);
        }
        catch (...)
        {
            failure_ = DiagD3dkmtCaptureFailure::SampleStorageFailed;
            failureDetail_ = "reserve initial VBlank sample storage";
            return false;
        }
    }
    stopRequested_.store(false, std::memory_order_release);
    try
    {
        sampler_ = std::thread(&DiagD3dkmtCadenceProbe::SampleLoop, this);
    }
    catch (...)
    {
        stopRequested_.store(true, std::memory_order_release);
        SetFailure(DiagD3dkmtCaptureFailure::SamplerStartFailed,
            "create D3DKMT sampler thread");
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
        args.NumObjects = 0;
        const auto status = api_.waitForVerticalBlankEvent2 ?
            api_.waitForVerticalBlankEvent2(&args) : waitForVerticalBlankEvent2_(&args);
        if (status != kStatusWaitForVblank)
        {
            SetFailure(DiagD3dkmtCaptureFailure::WaitFailed,
                "D3DKMTWaitForVerticalBlankEvent2", static_cast<std::int32_t>(status),
                DiagD3dkmtFailureStatusDomain::NtStatus);
            return;
        }
        if (stopRequested_.load(std::memory_order_acquire)) return;

        LARGE_INTEGER counter{};
        SetLastError(ERROR_SUCCESS);
        const auto counterAvailable = api_.queryPerformanceCounter ?
            api_.queryPerformanceCounter(&counter) : QueryPerformanceCounter(&counter);
        if (!counterAvailable)
        {
            const auto error = LastErrorStatus();
            SetFailure(DiagD3dkmtCaptureFailure::QueryCounterFailed,
                "QueryPerformanceCounter after VBlank", error,
                error ? DiagD3dkmtFailureStatusDomain::Win32 :
                    DiagD3dkmtFailureStatusDomain::None);
            return;
        }
        try
        {
            std::lock_guard lock(mutex_);
            if (timestamps_.size() >= kMaximumSamples)
            {
                if (failure_ == DiagD3dkmtCaptureFailure::None)
                {
                    failure_ = DiagD3dkmtCaptureFailure::SampleStorageFailed;
                    failureDetail_ = "maximum VBlank sample count reached";
                }
                return;
            }
            timestamps_.push_back(static_cast<std::uint64_t>(counter.QuadPart));
        }
        catch (...)
        {
            SetFailure(DiagD3dkmtCaptureFailure::SampleStorageFailed,
                "append VBlank QPC sample");
            return;
        }
    }
}

void DiagD3dkmtCadenceProbe::SetFailure(
    DiagD3dkmtCaptureFailure failure, std::string_view detail,
    std::optional<std::int32_t> status,
    DiagD3dkmtFailureStatusDomain statusDomain) noexcept
{
    std::lock_guard lock(mutex_);
    if (failure_ == DiagD3dkmtCaptureFailure::None)
    {
        failure_ = failure;
        failureDetail_ = detail;
        failureStatusDomain_ = statusDomain;
        failureStatus_ = status;
    }
}

void DiagD3dkmtCadenceProbe::StopAndJoin() noexcept
{
    stopRequested_.store(true, std::memory_order_release);
    if (sampler_.joinable()) sampler_.join();
    sampling_ = false;

    if (adapterHandle_ && (api_.closeAdapter || closeAdapter_))
    {
        D3DKMT_CLOSEADAPTER close{};
        close.hAdapter = adapterHandle_;
        const auto status = api_.closeAdapter ? api_.closeAdapter(&close) : closeAdapter_(&close);
        if (status != 0)
            SetFailure(DiagD3dkmtCaptureFailure::AdapterCloseFailed,
                "D3DKMTCloseAdapter", static_cast<std::int32_t>(status),
                DiagD3dkmtFailureStatusDomain::NtStatus);
    }
    adapterHandle_ = {};
}

DiagD3dkmtCadenceCapture DiagD3dkmtCadenceProbe::Stop()
{
    StopAndJoin();
    DiagD3dkmtCadenceCapture result;
    {
        std::lock_guard lock(mutex_);
        result.available = initialized_ && failure_ == DiagD3dkmtCaptureFailure::None;
        result.attempted = attempted_;
        result.targetIdentified = targetIdentified_;
        result.failure = failure_;
        result.failureDetail = failureDetail_;
        result.failureStatusDomain = failureStatusDomain_;
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
    StopAndJoin();
    initialized_ = false;
    waitForVerticalBlankEvent2_ = nullptr;
    openAdapterFromHdc_ = nullptr;
    closeAdapter_ = nullptr;
    if (gdi32_)
    {
        FreeLibrary(gdi32_);
        gdi32_ = nullptr;
    }
}
