#ifdef NDEBUG
#undef NDEBUG
#endif

#include "DiagD3dkmtCadenceProbe.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <utility>

struct DiagD3dkmtCadenceProbeTestAccess
{
    static bool Initialize(DiagD3dkmtCadenceProbe& probe, D3DKMT_HANDLE adapter)
    {
        VrrDisplayPath path;
        path.monitorDeviceName = L"DISPLAY1";
        path.sourceAdapterLuid = LUID{ 42, 7 };
        path.sourceId = 3;
        return probe.InitializeResolvedTarget(path.monitorDeviceName, adapter,
            path.sourceAdapterLuid, path.sourceId, 1000, path);
    }

    static bool InitializeMismatchedPath(DiagD3dkmtCadenceProbe& probe,
        D3DKMT_HANDLE adapter)
    {
        VrrDisplayPath path;
        path.monitorDeviceName = L"DISPLAY2";
        path.sourceAdapterLuid = LUID{ 42, 7 };
        path.sourceId = 3;
        return probe.InitializeResolvedTarget(L"DISPLAY1", adapter,
            path.sourceAdapterLuid, path.sourceId, 1000, path);
    }
};

namespace
{
constexpr NTSTATUS kStatusWaitForVblank = 0;
constexpr NTSTATUS kStatusCancelled = 1;
constexpr NTSTATUS kStatusInvalidParameter = static_cast<NTSTATUS>(0xC000000Du);
constexpr DWORD kSignalTimeoutMs = 5000;

bool WaitForSignal(HANDLE event)
{
    return WaitForSingleObject(event, kSignalTimeoutMs) == WAIT_OBJECT_0;
}

void TestWaitForVblankRecordsSampleAndCancellationIsClean()
{
    HANDLE sampleRecorded = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE cancellationWaitEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE cancellationObserved = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(sampleRecorded && cancellationWaitEntered && cancellationObserved);

    std::atomic<int> waitCalls{};
    std::atomic<int> adapterCloses{};
    DiagD3dkmtCadenceProbeApi api;
    api.waitForVerticalBlankEvent2 = [&](D3DKMT_WAITFORVERTICALBLANKEVENT2* args)
    {
        assert(args->hAdapter == 17);
        assert(args->VidPnSourceId == 3);
        assert(args->NumObjects == 1);
        if (waitCalls.fetch_add(1) == 0)
        {
            SetEvent(sampleRecorded);
            return kStatusWaitForVblank;
        }
        SetEvent(cancellationWaitEntered);
        const auto result = WaitForSingleObject(args->ObjectHandleArray[0], INFINITE);
        if (result != WAIT_OBJECT_0) return kStatusInvalidParameter;
        SetEvent(cancellationObserved);
        return kStatusCancelled;
    };
    api.queryPerformanceCounter = [](LARGE_INTEGER* counter)
    {
        counter->QuadPart = 12345;
        return TRUE;
    };
    api.closeAdapter = [&](D3DKMT_CLOSEADAPTER* args)
    {
        assert(args->hAdapter == 17);
        ++adapterCloses;
        return static_cast<NTSTATUS>(0);
    };

    DiagD3dkmtCadenceProbe probe(std::move(api));
    assert(DiagD3dkmtCadenceProbeTestAccess::Initialize(probe, 17));
    assert(probe.Start());
    assert(WaitForSignal(sampleRecorded));
    assert(WaitForSignal(cancellationWaitEntered));
    const auto capture = probe.Stop();

    assert(WaitForSignal(cancellationObserved));
    assert(capture.available);
    assert(capture.attempted);
    assert(capture.targetIdentified);
    assert(capture.failure == DiagD3dkmtCaptureFailure::None);
    assert(capture.timestamps.size() == 1);
    assert(capture.timestamps.front() == 12345);
    assert(adapterCloses == 1);
    CloseHandle(cancellationObserved);
    CloseHandle(cancellationWaitEntered);
    CloseHandle(sampleRecorded);
}

void TestWaitFailureTerminatesAndIsReported()
{
    HANDLE failureReturned = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(failureReturned);

    std::atomic<int> adapterCloses{};
    DiagD3dkmtCadenceProbeApi api;
    api.waitForVerticalBlankEvent2 = [&](D3DKMT_WAITFORVERTICALBLANKEVENT2*)
    {
        SetEvent(failureReturned);
        return kStatusInvalidParameter;
    };
    api.queryPerformanceCounter = [](LARGE_INTEGER* counter)
    {
        counter->QuadPart = 1;
        return TRUE;
    };
    api.closeAdapter = [&](D3DKMT_CLOSEADAPTER*)
    {
        ++adapterCloses;
        return static_cast<NTSTATUS>(0);
    };

    DiagD3dkmtCadenceProbe probe(std::move(api));
    assert(DiagD3dkmtCadenceProbeTestAccess::Initialize(probe, 18));
    assert(probe.Start());
    assert(WaitForSignal(failureReturned));
    const auto capture = probe.Stop();

    assert(!capture.available);
    assert(capture.failure == DiagD3dkmtCaptureFailure::WaitFailed);
    assert(capture.failureDetail == "D3DKMTWaitForVerticalBlankEvent2");
    assert(capture.failureStatusDomain == DiagD3dkmtFailureStatusDomain::NtStatus);
    assert(capture.failureStatus == static_cast<std::int32_t>(kStatusInvalidParameter));
    assert(adapterCloses == 1);
    CloseHandle(failureReturned);
}

void TestDisplayPathMismatchPreservesAdapterIdentity()
{
    std::atomic<int> adapterCloses{};
    DiagD3dkmtCadenceProbeApi api;
    api.waitForVerticalBlankEvent2 = [](D3DKMT_WAITFORVERTICALBLANKEVENT2*)
    {
        return kStatusCancelled;
    };
    api.closeAdapter = [&](D3DKMT_CLOSEADAPTER*)
    {
        ++adapterCloses;
        return static_cast<NTSTATUS>(0);
    };

    DiagD3dkmtCadenceProbe probe(std::move(api));
    assert(!DiagD3dkmtCadenceProbeTestAccess::InitializeMismatchedPath(probe, 21));
    const auto capture = probe.Stop();
    assert(capture.attempted);
    assert(!capture.available);
    assert(capture.targetIdentified);
    assert(capture.failure == DiagD3dkmtCaptureFailure::DisplayPathMismatch);
    assert(capture.failureDetail.find("does not match display path") != std::string_view::npos);
    assert(capture.adapterLuidLow == 42);
    assert(capture.adapterLuidHigh == 7);
    assert(capture.vidPnSourceId == 3);
    assert(adapterCloses == 1);
}

void TestCancellationAllowsASecondSamplerRun()
{
    std::atomic<int> waitCalls{};
    std::atomic<int> cancellations{};
    std::atomic<int> adapterCloses{};
    std::mutex sampleMutex;
    std::condition_variable sampleChanged;
    int recordedSamples{};
    int cancellationWaitsEntered{};

    DiagD3dkmtCadenceProbeApi api;
    api.waitForVerticalBlankEvent2 = [&](D3DKMT_WAITFORVERTICALBLANKEVENT2* args)
    {
        if ((waitCalls.fetch_add(1) % 2) == 0) return kStatusWaitForVblank;
        {
            std::lock_guard lock(sampleMutex);
            ++cancellationWaitsEntered;
        }
        sampleChanged.notify_one();
        const auto result = WaitForSingleObject(args->ObjectHandleArray[0], INFINITE);
        if (result != WAIT_OBJECT_0) return kStatusInvalidParameter;
        ++cancellations;
        return kStatusCancelled;
    };
    api.queryPerformanceCounter = [&](LARGE_INTEGER* counter)
    {
        {
            std::lock_guard lock(sampleMutex);
            counter->QuadPart = 1000 + recordedSamples;
            ++recordedSamples;
        }
        sampleChanged.notify_one();
        return TRUE;
    };
    api.closeAdapter = [&](D3DKMT_CLOSEADAPTER*)
    {
        ++adapterCloses;
        return static_cast<NTSTATUS>(0);
    };

    DiagD3dkmtCadenceProbe probe(std::move(api));
    const D3DKMT_HANDLE adapters[]{ 19, 20 };
    for (const auto adapter : adapters)
    {
        assert(DiagD3dkmtCadenceProbeTestAccess::Initialize(probe, adapter));
        assert(probe.Start());
        {
            std::unique_lock lock(sampleMutex);
            const auto expectedSamples = adapter == 19 ? 1 : 2;
            assert(sampleChanged.wait_for(lock, std::chrono::seconds(5), [&]
            {
                return recordedSamples >= expectedSamples &&
                    cancellationWaitsEntered >= expectedSamples;
            }));
        }
        const auto capture = probe.Stop();
        assert(capture.available);
        assert(capture.failure == DiagD3dkmtCaptureFailure::None);
        assert(capture.timestamps.size() == 1);
    }

    assert(cancellations == 2);
    assert(adapterCloses == 2);
}
}

int main()
{
    TestWaitForVblankRecordsSampleAndCancellationIsClean();
    TestWaitFailureTerminatesAndIsReported();
    TestDisplayPathMismatchPreservesAdapterIdentity();
    TestCancellationAllowsASecondSamplerRun();
    return 0;
}
