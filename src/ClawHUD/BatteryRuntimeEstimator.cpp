#include "BatteryRuntimeEstimator.h"

#include <cmath>

namespace clawhud
{
namespace
{
constexpr auto kMinimumBatteryEstimateWindow = std::chrono::seconds(30);
constexpr auto kMaximumBatterySampleGap = std::chrono::seconds(60);
constexpr auto kBatterySampleRetention = std::chrono::minutes(3);

BatteryEstimateResult EstimateFromAcceptedPower(
    std::uint64_t startCapacity, std::uint64_t currentCapacity,
    double elapsedSeconds, double watts, std::optional<int> lastEstimate,
    BatteryEstimateState state = BatteryEstimateState::Held,
    const wchar_t* reason = nullptr)
{
    BatteryEstimateResult result{};
    result.state = state;
    result.anchorCapacity = startCapacity;
    result.currentCapacity = currentCapacity;
    result.elapsedSeconds = elapsedSeconds;
    result.averageDischargeWatts = watts;
    result.resetReason = reason;
    result.remainingMinutes = lastEstimate;
    if (state != BatteryEstimateState::Updated && lastEstimate)
        return result;
    if (!(watts > 0.0) || !std::isfinite(watts))
        return result;

    const double remainingHours =
        (static_cast<double>(currentCapacity) / 1000.0) / watts;
    const double remainingMinutes = remainingHours * 60.0;
    if (!std::isfinite(remainingMinutes) || remainingMinutes < 0.0)
        return result;
    result.remainingMinutes = static_cast<int>(std::lround(remainingMinutes));
    return result;
}
}

BatteryEstimateResult BatteryRuntimeEstimator::Observe(
    bool onBattery,
    std::optional<std::uint64_t> remainingCapacityMWh,
    TimePoint now)
{
    if (!onBattery)
    {
        if (samples_.empty() && !lastAcceptedDischargeWatts_)
            return {};
        Reset();
        BatteryEstimateResult result{};
        result.state = BatteryEstimateState::Reset;
        result.resetReason = L"ac-connected";
        return result;
    }
    if (!remainingCapacityMWh)
    {
        if (samples_.empty() && !lastAcceptedDischargeWatts_)
            return {};
        Reset();
        BatteryEstimateResult result{};
        result.state = BatteryEstimateState::Reset;
        result.resetReason = L"invalid-capacity";
        return result;
    }

    const auto currentCapacity = *remainingCapacityMWh;

    if (!samples_.empty() && now - samples_.back().timestamp >
        kMaximumBatterySampleGap)
    {
        samples_.clear();
        samples_.push_back({now, currentCapacity});
        return EstimateFromAcceptedPower(
            currentCapacity, currentCapacity, 0.0,
            lastAcceptedDischargeWatts_.value_or(0.0),
            lastValidEstimateMinutes_, BatteryEstimateState::Reset,
            L"sample-gap");
    }

    if (!samples_.empty() && currentCapacity >
        samples_.back().remainingCapacityMWh)
    {
        samples_.clear();
        samples_.push_back({now, currentCapacity});
        return EstimateFromAcceptedPower(
            currentCapacity, currentCapacity, 0.0,
            lastAcceptedDischargeWatts_.value_or(0.0),
            lastValidEstimateMinutes_, BatteryEstimateState::Reset,
            L"capacity-correction");
    }

    samples_.push_back({now, currentCapacity});
    while (!samples_.empty() && now - samples_.front().timestamp >
        kBatterySampleRetention)
        samples_.pop_front();

    if (samples_.size() == 1)
    {
        BatteryEstimateResult result{};
        result.state = BatteryEstimateState::AnchorCreated;
        result.anchorCapacity = currentCapacity;
        result.currentCapacity = currentCapacity;
        return result;
    }

    const auto& newest = samples_.back();
    const BatteryCapacitySample* historical = nullptr;
    for (auto candidate = samples_.rbegin() + 1;
         candidate != samples_.rend(); ++candidate)
    {
        if (newest.timestamp - candidate->timestamp >=
            kMinimumBatteryEstimateWindow)
        {
            historical = &*candidate;
            break;
        }
    }

    if (historical == nullptr)
    {
        return EstimateFromAcceptedPower(
            samples_.front().remainingCapacityMWh, currentCapacity, 0.0,
            lastAcceptedDischargeWatts_.value_or(0.0),
            lastValidEstimateMinutes_, BatteryEstimateState::Waiting);
    }

    if (currentCapacity < samples_[samples_.size() - 2].remainingCapacityMWh)
    {
        const auto previousCapacity = samples_[samples_.size() - 2]
            .remainingCapacityMWh;
        auto plateauStart = samples_.end() - 2;
        while (plateauStart != samples_.begin())
        {
            const auto candidate = plateauStart - 1;
            if (candidate->remainingCapacityMWh != previousCapacity)
                break;
            plateauStart = candidate;
        }

        if (newest.timestamp - plateauStart->timestamp >=
            kMinimumBatteryEstimateWindow)
        {
            historical = &*plateauStart;
        }
    }

    const double elapsedSeconds = std::chrono::duration<double>(
        newest.timestamp - historical->timestamp).count();
    const auto historicalCapacity = historical->remainingCapacityMWh;
    if (currentCapacity == historicalCapacity)
    {
        return EstimateFromAcceptedPower(
            historicalCapacity, currentCapacity, elapsedSeconds,
            lastAcceptedDischargeWatts_.value_or(0.0),
            lastValidEstimateMinutes_, lastValidEstimateMinutes_
                ? BatteryEstimateState::Held : BatteryEstimateState::Waiting,
            lastValidEstimateMinutes_ ? L"capacity-unchanged" : nullptr);
    }

    if (currentCapacity > historicalCapacity)
        return EstimateFromAcceptedPower(
            historicalCapacity, currentCapacity, elapsedSeconds,
            lastAcceptedDischargeWatts_.value_or(0.0),
            lastValidEstimateMinutes_, BatteryEstimateState::Reset,
            L"capacity-correction");

    const double deltaMWh = static_cast<double>(historicalCapacity - currentCapacity);
    const double elapsedHours = elapsedSeconds / 3600.0;
    const double averageDischargeWatts = deltaMWh / elapsedHours / 1000.0;
    if (averageDischargeWatts > 0.0 && std::isfinite(averageDischargeWatts))
    {
        lastAcceptedDischargeWatts_ = averageDischargeWatts;
        BatteryEstimateResult result = EstimateFromAcceptedPower(
            historicalCapacity, currentCapacity, elapsedSeconds,
            averageDischargeWatts, lastValidEstimateMinutes_,
            BatteryEstimateState::Updated);
        result.deltaMWh = deltaMWh;
        lastValidEstimateMinutes_ = result.remainingMinutes;
        return result;
    }

    return EstimateFromAcceptedPower(
        historicalCapacity, currentCapacity, elapsedSeconds,
        lastAcceptedDischargeWatts_.value_or(0.0), lastValidEstimateMinutes_);
}

void BatteryRuntimeEstimator::Reset() noexcept
{
    samples_.clear();
    lastAcceptedDischargeWatts_.reset();
    lastValidEstimateMinutes_.reset();
}
}
