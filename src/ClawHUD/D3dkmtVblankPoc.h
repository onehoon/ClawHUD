#pragma once

#include <windows.h>

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

D3dkmtVblankStatistics CalculateD3dkmtVblankStatistics(
    const std::vector<std::uint64_t>& timestamps,
    std::size_t failedWaits,
    std::int64_t qpcFrequency) noexcept;

class D3dkmtVblankPoc
{
public:
    D3dkmtVblankPoc() = default;
    ~D3dkmtVblankPoc();

    bool Initialize(std::wofstream& log, HMONITOR monitor);
    void Start();
    D3dkmtVblankStatistics Stop(std::wofstream& log, const wchar_t* phase);
    void Shutdown() noexcept;

private:
    void SampleLoop();

    using WaitForVerticalBlankEvent = long(__stdcall*)(void*);
    using OpenAdapterFromHdc = long(__stdcall*)(void*);
    using CloseAdapter = long(__stdcall*)(void*);

    void* adapterHandle_{};
    unsigned int vidPnSourceId_{};
    std::uint64_t adapterLuid_{ };
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
    long lastFailureStatus_{};
    std::int64_t qpcFrequency_{};
};
