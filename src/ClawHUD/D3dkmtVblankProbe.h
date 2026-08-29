#pragma once

#include <windows.h>
#include <bcrypt.h>
#include <d3dkmthk.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct D3dkmtVblankStatistics
{
    std::size_t sampleCount{};
    std::size_t successfulWaits{};
    std::size_t failedWaits{};
    std::uint64_t firstQpc{};
    std::uint64_t lastQpc{};
    double durationSeconds{};
    double minimumDeltaMs{};
    double medianDeltaMs{};
    double averageDeltaMs{};
    double maximumDeltaMs{};
    std::optional<double> measuredHzElapsed;
    std::optional<double> measuredHzMedian;
};

struct D3dkmtVblankWindow
{
    double startSeconds{};
    double endSeconds{};
    std::size_t eventCount{};
    double elapsedSeconds{};
    double measuredHz{};
};

struct D3dkmtVblankResult
{
    bool available{};
    D3dkmtVblankStatistics statistics;
    std::vector<D3dkmtVblankWindow> windows;
    std::optional<NTSTATUS> failureStatus;
};

D3dkmtVblankStatistics CalculateD3dkmtVblankStatistics(
    const std::vector<std::uint64_t>& timestamps,
    std::size_t failedWaits,
    std::int64_t qpcFrequency) noexcept;

std::vector<D3dkmtVblankWindow> CalculateD3dkmtVblankWindows(
    const std::vector<std::uint64_t>& timestamps,
    std::int64_t qpcFrequency,
    double windowSeconds = 1.0);

class D3dkmtVblankProbe
{
public:
    D3dkmtVblankProbe() = default;
    ~D3dkmtVblankProbe();

    bool Initialize(std::wofstream& log, HMONITOR monitor);
    void Start();
    D3dkmtVblankResult Stop();
    void Shutdown() noexcept;

private:
    void SampleLoop();

    using WaitForVerticalBlankEvent = PFND3DKMT_WAITFORVERTICALBLANKEVENT;
    using OpenAdapterFromHdc = PFND3DKMT_OPENADAPTERFROMHDC;
    using CloseAdapter = PFND3DKMT_CLOSEADAPTER;

    D3DKMT_HANDLE adapterHandle_{};
    UINT vidPnSourceId_{};
    LUID adapterLuid_{};
    std::wstring displayName_;
    std::wstring monitorName_;
    WaitForVerticalBlankEvent waitForVerticalBlankEvent_{};
    OpenAdapterFromHdc openAdapterFromHdc_{};
    CloseAdapter closeAdapter_{};
    std::atomic_bool stopRequested_{true};
    std::atomic_bool sampling_{false};
    std::thread sampler_;
    std::mutex samplesMutex_;
    std::vector<std::uint64_t> timestamps_;
    std::size_t failedWaits_{};
    std::optional<NTSTATUS> lastFailureStatus_;
    std::int64_t qpcFrequency_{};
    bool available_{};
};

void WriteD3dkmtVblankDiagnostic(
    std::wofstream& log,
    const wchar_t* phase,
    const D3dkmtVblankResult& result);
