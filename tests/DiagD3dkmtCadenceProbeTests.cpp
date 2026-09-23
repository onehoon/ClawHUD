#ifdef NDEBUG
#undef NDEBUG
#endif

#include "DiagD3dkmtCadenceProbe.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>
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

    static bool StopRequested(const DiagD3dkmtCadenceProbe& probe)
    {
        return probe.stopRequested_.load(std::memory_order_acquire);
    }
};

namespace
{
constexpr NTSTATUS kStatusWaitForVblank = 0;
constexpr NTSTATUS kStatusInvalidParameter = static_cast<NTSTATUS>(0xC000000Du);
constexpr NTSTATUS kStatusAccessDenied = static_cast<NTSTATUS>(0xC0000022u);
constexpr DWORD kSignalTimeoutMs = 5000;

bool WaitForSignal(HANDLE event)
{
    return WaitForSingleObject(event, kSignalTimeoutMs) == WAIT_OBJECT_0;
}

bool WaitForStopRequest(const DiagD3dkmtCadenceProbe& probe)
{
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kSignalTimeoutMs);
    while (!DiagD3dkmtCadenceProbeTestAccess::StopRequested(probe) &&
        std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    return DiagD3dkmtCadenceProbeTestAccess::StopRequested(probe);
}

void TestWaitForVblankRecordsSampleAndStopsAfterNextReturn()
{
    HANDLE sampleRecorded = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE finalWaitEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE releaseFinalWait = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE finalWaitReturned = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(sampleRecorded && finalWaitEntered && releaseFinalWait && finalWaitReturned);

    std::atomic<int> waitCalls{};
    std::atomic<int> recordedSamples{};
    std::atomic<int> adapterCloses{};
    DiagD3dkmtCadenceProbeApi api;
    api.waitForVerticalBlankEvent2 = [&](D3DKMT_WAITFORVERTICALBLANKEVENT2* args)
    {
        assert(args->hAdapter == 17);
        assert(args->VidPnSourceId == 3);
        assert(args->NumObjects == 0);
        const auto call = waitCalls.fetch_add(1);
        if (call < 2) return kStatusWaitForVblank;
        if (call == 2)
        {
            SetEvent(finalWaitEntered);
            if (WaitForSingleObject(releaseFinalWait, kSignalTimeoutMs) != WAIT_OBJECT_0)
                return kStatusInvalidParameter;
            SetEvent(finalWaitReturned);
            return kStatusWaitForVblank;
        }
        return kStatusInvalidParameter;
    };
    api.queryPerformanceCounter = [&](LARGE_INTEGER* counter)
    {
        const auto sample = recordedSamples.fetch_add(1);
        counter->QuadPart = 12345 + sample;
        if (sample == 1) SetEvent(sampleRecorded);
        return TRUE;
    };
    api.closeAdapter = [&](D3DKMT_CLOSEADAPTER* args)
    {
        assert(args->hAdapter == 17);
        assert(WaitForSingleObject(finalWaitReturned, 0) == WAIT_OBJECT_0);
        ++adapterCloses;
        return static_cast<NTSTATUS>(0);
    };

    DiagD3dkmtCadenceProbe probe(std::move(api));
    assert(DiagD3dkmtCadenceProbeTestAccess::Initialize(probe, 17));
    assert(probe.Start());
    assert(WaitForSignal(sampleRecorded));
    assert(WaitForSignal(finalWaitEntered));

    DiagD3dkmtCadenceCapture capture;
    std::thread stopper([&] { capture = probe.Stop(); });
    assert(WaitForStopRequest(probe));
    assert(adapterCloses == 0);
    SetEvent(releaseFinalWait);
    stopper.join();

    assert(capture.available);
    assert(capture.attempted);
    assert(capture.targetIdentified);
    assert(capture.failure == DiagD3dkmtCaptureFailure::None);
    assert(capture.timestamps.size() == 2);
    assert(capture.timestamps.front() == 12345);
    assert(capture.timestamps.back() == 12346);
    assert(adapterCloses == 1);
    CloseHandle(finalWaitReturned);
    CloseHandle(releaseFinalWait);
    CloseHandle(finalWaitEntered);
    CloseHandle(sampleRecorded);
}

void TestWaitFailuresTerminateAndAreReported()
{
    const NTSTATUS failures[]{ kStatusAccessDenied, kStatusInvalidParameter };
    for (const auto expectedStatus : failures)
    {
        HANDLE failureReturned = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        assert(failureReturned);

        std::atomic<int> adapterCloses{};
        DiagD3dkmtCadenceProbeApi api;
        api.waitForVerticalBlankEvent2 = [&](D3DKMT_WAITFORVERTICALBLANKEVENT2* args)
        {
            assert(args->NumObjects == 0);
            SetEvent(failureReturned);
            return expectedStatus;
        };
        api.queryPerformanceCounter = [](LARGE_INTEGER*)
        {
            assert(false);
            return FALSE;
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
        assert(capture.failureStatus == static_cast<std::int32_t>(expectedStatus));
        assert(capture.timestamps.empty());
        assert(adapterCloses == 1);
        CloseHandle(failureReturned);
    }
}

void TestDisplayPathMismatchPreservesAdapterIdentity()
{
    std::atomic<int> adapterCloses{};
    DiagD3dkmtCadenceProbeApi api;
    api.waitForVerticalBlankEvent2 = [](D3DKMT_WAITFORVERTICALBLANKEVENT2*)
    {
        assert(false);
        return kStatusInvalidParameter;
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

void TestCooperativeStopAllowsASecondSamplerRun()
{
    HANDLE sampleRecorded[2]{
        CreateEventW(nullptr, TRUE, FALSE, nullptr),
        CreateEventW(nullptr, TRUE, FALSE, nullptr) };
    HANDLE secondWaitEntered[2]{
        CreateEventW(nullptr, TRUE, FALSE, nullptr),
        CreateEventW(nullptr, TRUE, FALSE, nullptr) };
    HANDLE releaseSecondWait[2]{
        CreateEventW(nullptr, TRUE, FALSE, nullptr),
        CreateEventW(nullptr, TRUE, FALSE, nullptr) };
    HANDLE secondWaitReturned[2]{
        CreateEventW(nullptr, TRUE, FALSE, nullptr),
        CreateEventW(nullptr, TRUE, FALSE, nullptr) };
    for (int i = 0; i < 2; ++i)
        assert(sampleRecorded[i] && secondWaitEntered[i] && releaseSecondWait[i] &&
            secondWaitReturned[i]);

    std::atomic<int> adapterCloses{};
    std::atomic<int> runIndex{};
    std::atomic<int> waitCallsInRun{};
    std::atomic<std::uint64_t> nextQpc{ 1000 };
    DiagD3dkmtCadenceProbeApi api;
    api.waitForVerticalBlankEvent2 = [&](D3DKMT_WAITFORVERTICALBLANKEVENT2* args)
    {
        assert(args->NumObjects == 0);
        const auto run = runIndex.load();
        const auto call = waitCallsInRun.fetch_add(1);
        if (call == 0) return kStatusWaitForVblank;
        assert(call == 1);
        SetEvent(secondWaitEntered[run]);
        if (WaitForSingleObject(releaseSecondWait[run], kSignalTimeoutMs) != WAIT_OBJECT_0)
            return kStatusInvalidParameter;
        SetEvent(secondWaitReturned[run]);
        return kStatusWaitForVblank;
    };
    api.queryPerformanceCounter = [&](LARGE_INTEGER* counter)
    {
        const auto run = runIndex.load();
        counter->QuadPart = static_cast<LONGLONG>(nextQpc.fetch_add(1));
        SetEvent(sampleRecorded[run]);
        return TRUE;
    };
    api.closeAdapter = [&](D3DKMT_CLOSEADAPTER*)
    {
        const auto run = runIndex.load();
        assert(WaitForSingleObject(secondWaitReturned[run], 0) == WAIT_OBJECT_0);
        ++adapterCloses;
        waitCallsInRun.store(0);
        runIndex.fetch_add(1);
        return static_cast<NTSTATUS>(0);
    };

    DiagD3dkmtCadenceProbe probe(std::move(api));
    const D3DKMT_HANDLE adapters[]{ 19, 20 };
    for (int run = 0; run < 2; ++run)
    {
        assert(DiagD3dkmtCadenceProbeTestAccess::Initialize(probe, adapters[run]));
        assert(probe.Start());
        assert(WaitForSignal(sampleRecorded[run]));
        assert(WaitForSignal(secondWaitEntered[run]));

        DiagD3dkmtCadenceCapture capture;
        std::thread stopper([&] { capture = probe.Stop(); });
        assert(WaitForStopRequest(probe));
        assert(adapterCloses == run);
        SetEvent(releaseSecondWait[run]);
        stopper.join();

        assert(capture.available);
        assert(capture.failure == DiagD3dkmtCaptureFailure::None);
        assert(capture.timestamps.size() == 1);
        assert(adapterCloses == run + 1);
    }

    assert(runIndex == 2);
    for (int i = 0; i < 2; ++i)
    {
        CloseHandle(secondWaitReturned[i]);
        CloseHandle(releaseSecondWait[i]);
        CloseHandle(secondWaitEntered[i]);
        CloseHandle(sampleRecorded[i]);
    }
}
}

int main()
{
    TestWaitForVblankRecordsSampleAndStopsAfterNextReturn();
    TestWaitFailuresTerminateAndAreReported();
    TestDisplayPathMismatchPreservesAdapterIdentity();
    TestCooperativeStopAllowsASecondSamplerRun();
    return 0;
}
