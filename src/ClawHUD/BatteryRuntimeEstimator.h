#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <deque>

namespace clawhud
{
enum class BatteryEstimateState
{
    None,
    AnchorCreated,
    Waiting,
    Updated,
    Held,
    Reset
};

struct BatteryEstimateResult
{
    BatteryEstimateState state{BatteryEstimateState::None};
    std::optional<int> remainingMinutes;
    std::uint64_t anchorCapacity{};
    std::uint64_t currentCapacity{};
    double elapsedSeconds{};
    double deltaMWh{};
    double averageDischargeWatts{};
    const wchar_t* resetReason{};
};

class BatteryRuntimeEstimator
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    BatteryEstimateResult Observe(
        bool onBattery,
        std::optional<std::uint64_t> remainingCapacityMWh,
        TimePoint now);
    void Reset() noexcept;
    std::size_t SampleCount() const noexcept { return samples_.size(); }

private:
    struct BatteryCapacitySample
    {
        TimePoint timestamp;
        std::uint64_t remainingCapacityMWh{};
    };

    std::deque<BatteryCapacitySample> samples_;
    std::optional<double> lastAcceptedDischargeWatts_;
    std::optional<int> lastValidEstimateMinutes_;
};
}
