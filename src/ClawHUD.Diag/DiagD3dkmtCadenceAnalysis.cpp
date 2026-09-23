#include "DiagD3dkmtCadenceAnalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace
{
constexpr double kCadenceWindowSeconds = 1.0;
constexpr std::uint64_t kMaximumWindowCount = 100000;

std::optional<double> Median(std::vector<double> values)
{
    if (values.empty()) return std::nullopt;
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2
        ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
}
}

double DiagVrrNearNominalToleranceHz(double nominalRefreshHz) noexcept
{
    if (!std::isfinite(nominalRefreshHz) || nominalRefreshHz <= 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    return std::max(1.5, nominalRefreshHz * 0.015);
}

bool DiagVrrCadenceIsNearNominal(double measuredHz, double nominalRefreshHz) noexcept
{
    const auto tolerance = DiagVrrNearNominalToleranceHz(nominalRefreshHz);
    return std::isfinite(measuredHz) && std::isfinite(tolerance) &&
        std::abs(measuredHz - nominalRefreshHz) <= tolerance;
}

DiagD3dkmtCadenceAnalysis AnalyzeDiagD3dkmtCadence(
    const DiagD3dkmtCadenceCapture& capture, double nominalRefreshHz)
{
    DiagD3dkmtCadenceAnalysis result;
    if (!capture.available)
    {
        result.dataStatus = DiagD3dkmtCadenceDataStatus::Unavailable;
        result.classification = DiagD3dkmtCadenceClass::Unavailable;
        return result;
    }
    result.dataStatus = DiagD3dkmtCadenceDataStatus::Available;
    result.classification = DiagD3dkmtCadenceClass::Indeterminate;

    if (capture.qpcFrequency <= 0)
    {
        result.dataStatus = DiagD3dkmtCadenceDataStatus::InvalidFrequency;
        return result;
    }
    if (!std::isfinite(nominalRefreshHz) || nominalRefreshHz <= 0.0)
    {
        result.dataStatus = DiagD3dkmtCadenceDataStatus::InvalidNominalRefresh;
        return result;
    }

    const auto& timestamps = capture.timestamps;
    for (std::size_t i = 1; i < timestamps.size(); ++i)
    {
        if (timestamps[i] <= timestamps[i - 1])
        {
            result.dataStatus = DiagD3dkmtCadenceDataStatus::InvalidTimestamps;
            return result;
        }
    }
    if (timestamps.size() < 2) return result;

    const auto frequency = static_cast<std::uint64_t>(capture.qpcFrequency);
    const auto durationTicks = timestamps.back() - timestamps.front();
    const auto completeWindowCount = durationTicks / frequency;
    if (completeWindowCount > kMaximumWindowCount)
    {
        result.dataStatus = DiagD3dkmtCadenceDataStatus::TooManyWindows;
        return result;
    }

    result.windows.reserve(static_cast<std::size_t>(completeWindowCount));
    std::size_t sampleIndex{};
    for (std::uint64_t windowIndex = 0; windowIndex < completeWindowCount; ++windowIndex)
    {
        const auto startTicks = windowIndex * frequency;
        const auto endTicks = startTicks + frequency;
        std::size_t eventCount{};
        while (sampleIndex < timestamps.size())
        {
            const auto relativeTicks = timestamps[sampleIndex] - timestamps.front();
            if (relativeTicks >= endTicks) break;
            if (relativeTicks >= startTicks) ++eventCount;
            ++sampleIndex;
        }
        const auto startSeconds = static_cast<double>(windowIndex) * kCadenceWindowSeconds;
        result.windows.push_back({ startSeconds, startSeconds + kCadenceWindowSeconds,
            eventCount, static_cast<double>(eventCount) / kCadenceWindowSeconds });
    }

    if (result.windows.empty()) return result;

    std::vector<double> rates;
    rates.reserve(result.windows.size());
    std::size_t nearNominalCount{};
    for (const auto& window : result.windows)
    {
        rates.push_back(window.measuredHz);
        if (DiagVrrCadenceIsNearNominal(window.measuredHz, nominalRefreshHz))
            ++nearNominalCount;
    }
    result.minimumHz = *std::min_element(rates.begin(), rates.end());
    result.maximumHz = *std::max_element(rates.begin(), rates.end());
    result.averageHz = std::accumulate(rates.begin(), rates.end(), 0.0) /
        static_cast<double>(rates.size());
    result.medianHz = Median(rates);
    result.nearNominalRatio = static_cast<double>(nearNominalCount) /
        static_cast<double>(rates.size());
    result.rangeWidthHz = *result.maximumHz - *result.minimumHz;

    double squaredDeviation{};
    for (const auto rate : rates)
    {
        const auto deviation = rate - *result.averageHz;
        squaredDeviation += deviation * deviation;
    }
    result.standardDeviationHz = std::sqrt(squaredDeviation / static_cast<double>(rates.size()));

    if (result.windows.size() < 10) return result;
    const auto tolerance = DiagVrrNearNominalToleranceHz(nominalRefreshHz);
    if (*result.nearNominalRatio >= 0.80 &&
        std::abs(*result.medianHz - nominalRefreshHz) <= tolerance &&
        *result.rangeWidthHz <= std::max(5.0, nominalRefreshHz * 0.05))
    {
        result.classification = DiagD3dkmtCadenceClass::FixedLike;
    }
    else if (*result.nearNominalRatio <= 0.50 &&
        *result.rangeWidthHz >= std::max(6.0, nominalRefreshHz * 0.05))
    {
        result.classification = DiagD3dkmtCadenceClass::VariableLike;
    }
    return result;
}
