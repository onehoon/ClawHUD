#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace clawhud
{
class BatteryPowerEstimator
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void Observe(bool onBattery, std::optional<double> powerW, TimePoint now);
    std::optional<double> AveragePowerW() const noexcept;
    std::optional<int> EstimateRemainingMinutes(
        std::optional<std::uint64_t> remainingCapacityMWh) const;
    void Reset() noexcept;
    std::size_t SampleCount() const noexcept { return samples_.size(); }
    bool Ready() const noexcept;

private:
    struct Sample { TimePoint timestamp; double powerW{}; };
    std::deque<Sample> samples_;
};
}
