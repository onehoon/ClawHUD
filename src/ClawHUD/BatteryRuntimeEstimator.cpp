#include "BatteryRuntimeEstimator.h"

#include <cmath>

namespace clawhud
{
namespace
{
constexpr double kMinimumMeasurementSeconds = 30.0;
constexpr double kMaximumSampleGapSeconds = 60.0;

BatteryEstimateResult EstimateFromAcceptedPower(
    std::uint64_t currentCapacity, double watts)
{
    BatteryEstimateResult result{};
    result.currentCapacity = currentCapacity;
    result.averageDischargeWatts = watts;
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
        if (!activeSession_ && !lastAcceptedDischargeWatts_)
            return {};
        Reset();
        BatteryEstimateResult result{};
        result.state = BatteryEstimateState::Reset;
        result.resetReason = L"ac-connected";
        return result;
    }
    if (!remainingCapacityMWh)
        return {};

    const auto currentCapacity = *remainingCapacityMWh;
    if (!activeSession_)
    {
        activeSession_ = true;
        anchorCapacity_ = currentCapacity;
        anchorTimestamp_ = now;
        lastSampleTimestamp_ = now;
        anchorMatured_ = false;
        BatteryEstimateResult result{};
        result.state = BatteryEstimateState::AnchorCreated;
        result.anchorCapacity = currentCapacity;
        result.currentCapacity = currentCapacity;
        return result;
    }

    const double sampleGap = std::chrono::duration<double>(
        now - lastSampleTimestamp_).count();
    lastSampleTimestamp_ = now;
    if (sampleGap > kMaximumSampleGapSeconds)
    {
        anchorCapacity_ = currentCapacity;
        anchorTimestamp_ = now;
        anchorMatured_ = false;
        BatteryEstimateResult result = EstimateFromAcceptedPower(
            currentCapacity, lastAcceptedDischargeWatts_.value_or(0.0));
        result.state = BatteryEstimateState::Reset;
        result.anchorCapacity = currentCapacity;
        result.resetReason = L"sample-gap";
        return result;
    }

    const double elapsedSeconds = std::chrono::duration<double>(
        now - anchorTimestamp_).count();
    const auto anchorCapacity = *anchorCapacity_;
    if (currentCapacity > anchorCapacity)
    {
        anchorCapacity_ = currentCapacity;
        anchorTimestamp_ = now;
        anchorMatured_ = false;
        BatteryEstimateResult result = EstimateFromAcceptedPower(
            currentCapacity, lastAcceptedDischargeWatts_.value_or(0.0));
        result.state = BatteryEstimateState::Reset;
        result.anchorCapacity = currentCapacity;
        result.elapsedSeconds = elapsedSeconds;
        result.resetReason = L"capacity-correction";
        return result;
    }

    if (elapsedSeconds >= kMinimumMeasurementSeconds &&
        !anchorMatured_ && currentCapacity == anchorCapacity)
    {
        anchorMatured_ = true;
        BatteryEstimateResult result = EstimateFromAcceptedPower(
            currentCapacity, lastAcceptedDischargeWatts_.value_or(0.0));
        result.state = BatteryEstimateState::Waiting;
        result.anchorCapacity = anchorCapacity;
        result.elapsedSeconds = elapsedSeconds;
        return result;
    }

    if (currentCapacity < anchorCapacity &&
        elapsedSeconds >= kMinimumMeasurementSeconds && elapsedSeconds > 0.0)
    {
        const double deltaMWh = static_cast<double>(anchorCapacity - currentCapacity);
        const double elapsedHours = elapsedSeconds / 3600.0;
        const double averageDischargeWatts = deltaMWh / elapsedHours / 1000.0;
        if (averageDischargeWatts > 0.0 && std::isfinite(averageDischargeWatts))
        {
            lastAcceptedDischargeWatts_ = averageDischargeWatts;
            anchorCapacity_ = currentCapacity;
            anchorTimestamp_ = now;
            anchorMatured_ = false;
            BatteryEstimateResult result = EstimateFromAcceptedPower(
                currentCapacity, averageDischargeWatts);
            result.state = BatteryEstimateState::Updated;
            result.anchorCapacity = anchorCapacity;
            result.elapsedSeconds = elapsedSeconds;
            result.deltaMWh = deltaMWh;
            return result;
        }
    }

    return EstimateFromAcceptedPower(
        currentCapacity, lastAcceptedDischargeWatts_.value_or(0.0));
}

void BatteryRuntimeEstimator::Reset() noexcept
{
    anchorCapacity_.reset();
    anchorTimestamp_ = {};
    lastSampleTimestamp_ = {};
    lastAcceptedDischargeWatts_.reset();
    activeSession_ = false;
    anchorMatured_ = false;
}
}
