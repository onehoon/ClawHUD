#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace clawhud
{
inline constexpr auto kVblankPollInterval = std::chrono::milliseconds(1);

struct VblankSeries
{
    std::size_t output{};
    std::size_t target{};
    std::vector<std::uint64_t> timestamps;
    std::size_t resetCount{};
};

struct VblankSummary
{
    std::size_t uniqueSamples{};
    std::size_t validDeltas{};
    std::uint64_t first{};
    std::uint64_t last{};
    double averageDeltaUs{};
    double medianDeltaUs{};
    double minimumDeltaUs{};
    double maximumDeltaUs{};
    std::optional<double> measuredHz;
};

void RecordVblankTimestamp(VblankSeries& series, std::uint64_t timestamp);
VblankSummary SummarizeVblank(const VblankSeries& series);
std::optional<double> UsableVblankMedian(const std::vector<VblankSummary>& summaries);
std::string IntelCtlResultName(std::uint32_t result);

class IntelVrrDiagnosticProbe
{
public:
    ~IntelVrrDiagnosticProbe();

    bool Initialize(std::wofstream& log);
    void LogState(std::wofstream& log);
    void StartSampling();
    std::vector<VblankSummary> StopSampling(std::wofstream& log, const wchar_t* phase);
    void Shutdown();

private:
    struct Output
    {
        void* handle{};
        std::size_t index{};
        std::uint32_t lastVblankError{};
        std::size_t vblankErrorCount{};
        std::size_t vblankSuccessCount{};
        std::vector<VblankSeries> series;
    };
    void SampleLoop();
    void LogResult(std::wofstream& log, const wchar_t* operation, std::uint32_t result) const;

    HMODULE library_{};
    void* apiHandle_{};
    std::vector<Output> outputs_;
    std::wofstream* log_{};
    std::atomic_bool sampling_{};
    std::thread sampler_;
    bool initialized_{};
    bool vblankAvailable_{};
};
}
