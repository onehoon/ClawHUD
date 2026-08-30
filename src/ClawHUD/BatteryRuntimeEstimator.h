#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace clawhud
{
enum class BatteryEstimateState
{
    None,
    AnchorCreated,
    Waiting,
    Updated,
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

private:
    std::optional<std::uint64_t> anchorCapacity_;
    TimePoint anchorTimestamp_{};
    TimePoint lastSampleTimestamp_{};
    std::optional<double> lastAcceptedDischargeWatts_;
    bool activeSession_{};
    bool anchorMatured_{};
};
}
