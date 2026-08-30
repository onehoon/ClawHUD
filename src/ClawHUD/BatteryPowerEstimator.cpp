#include "BatteryPowerEstimator.h"

#include <cmath>
#include <cstdint>
#include <numeric>

namespace clawhud
{
namespace
{
constexpr std::size_t kMinimumSamples = 10;
constexpr auto kMinimumHistory = std::chrono::seconds(9);
constexpr auto kRollingWindow = std::chrono::seconds(20);
}

void BatteryPowerEstimator::Observe(bool onBattery, std::optional<double> powerW,
    TimePoint now)
{
    if (!onBattery)
    {
        Reset();
        return;
    }
    if (!powerW || !(*powerW > 0.0) || !std::isfinite(*powerW))
        return;
    samples_.push_back({ now, *powerW });
    while (!samples_.empty() && now - samples_.front().timestamp > kRollingWindow)
        samples_.pop_front();
}

bool BatteryPowerEstimator::Ready() const noexcept
{
    return samples_.size() >= kMinimumSamples &&
        samples_.back().timestamp - samples_.front().timestamp >= kMinimumHistory;
}

std::optional<double> BatteryPowerEstimator::AveragePowerW() const noexcept
{
    if (!Ready())
        return std::nullopt;
    double total = 0.0;
    for (const auto& sample : samples_)
        total += sample.powerW;
    const double average = total / static_cast<double>(samples_.size());
    return std::isfinite(average) && average > 0.0
        ? std::optional<double>(average) : std::nullopt;
}

std::optional<int> BatteryPowerEstimator::EstimateRemainingMinutes(
    std::optional<std::uint64_t> remainingCapacityMWh) const
{
    const auto average = AveragePowerW();
    if (!average || !remainingCapacityMWh)
        return std::nullopt;
    const double minutes = (static_cast<double>(*remainingCapacityMWh) / 1000.0) /
        *average * 60.0;
    if (!std::isfinite(minutes) || minutes < 0.0)
        return std::nullopt;
    return static_cast<int>(std::lround(minutes));
}

void BatteryPowerEstimator::Reset() noexcept
{
    samples_.clear();
}
}
