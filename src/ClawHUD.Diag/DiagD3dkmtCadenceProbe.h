#pragma once

#include "DiagD3dkmtCadenceAnalysis.h"
#include "DisplayPathProbe.h"

#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

struct DiagD3dkmtCadenceProbeApi
{
    std::function<NTSTATUS(D3DKMT_WAITFORVERTICALBLANKEVENT2*)> waitForVerticalBlankEvent2;
    std::function<BOOL(LARGE_INTEGER*)> queryPerformanceCounter;
    std::function<NTSTATUS(D3DKMT_CLOSEADAPTER*)> closeAdapter;
};

struct DiagD3dkmtCadenceProbeTestAccess;

class DiagD3dkmtCadenceProbe
{
public:
    DiagD3dkmtCadenceProbe() = default;
    explicit DiagD3dkmtCadenceProbe(DiagD3dkmtCadenceProbeApi api);
    ~DiagD3dkmtCadenceProbe();

    DiagD3dkmtCadenceProbe(const DiagD3dkmtCadenceProbe&) = delete;
    DiagD3dkmtCadenceProbe& operator=(const DiagD3dkmtCadenceProbe&) = delete;

    bool Initialize(HMONITOR monitor, const VrrDisplayPath& expectedPath) noexcept;
    bool Start() noexcept;
    DiagD3dkmtCadenceCapture Stop();
    void Shutdown() noexcept;

private:
    friend struct DiagD3dkmtCadenceProbeTestAccess;

    bool InitializeResolvedTarget(std::wstring_view monitorDeviceName,
        D3DKMT_HANDLE adapterHandle, LUID adapterLuid, UINT32 vidPnSourceId,
        std::int64_t qpcFrequency, const VrrDisplayPath& expectedPath) noexcept;
    void SampleLoop() noexcept;
    void CancelAndJoin() noexcept;
    void SetFailure(DiagD3dkmtCaptureFailure failure,
        std::optional<std::int32_t> status = std::nullopt) noexcept;

    using WaitForVerticalBlankEvent2 = PFND3DKMT_WAITFORVERTICALBLANKEVENT2;
    using OpenAdapterFromHdc = PFND3DKMT_OPENADAPTERFROMHDC;
    using CloseAdapter = PFND3DKMT_CLOSEADAPTER;

    HMODULE gdi32_{};
    WaitForVerticalBlankEvent2 waitForVerticalBlankEvent2_{};
    OpenAdapterFromHdc openAdapterFromHdc_{};
    CloseAdapter closeAdapter_{};
    DiagD3dkmtCadenceProbeApi api_;
    D3DKMT_HANDLE adapterHandle_{};
    LUID adapterLuid_{};
    UINT32 vidPnSourceId_{};
    std::int64_t qpcFrequency_{};
    HANDLE cancellationEvent_{};
    std::atomic_bool stopRequested_{ true };
    std::thread sampler_;
    mutable std::mutex mutex_;
    DiagD3dkmtCaptureFailure failure_{ DiagD3dkmtCaptureFailure::None };
    std::optional<std::int32_t> failureStatus_;
    std::vector<std::uint64_t> timestamps_;
    bool initialized_{};
    bool sampling_{};
};
