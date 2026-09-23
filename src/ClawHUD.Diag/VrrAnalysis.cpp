#include "VrrAnalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr std::int32_t kIgclArcSyncProfileOff = 5;
constexpr std::size_t kMinimumPresentModeSamples = 60;
constexpr double kDominantSwapchainShare = 0.70;
constexpr std::size_t kMinimumCadenceWindows = 10;

bool IsIndependentFlip(std::int32_t mode) noexcept
{
    return mode == PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP ||
        mode == PM_PRESENT_MODE_HARDWARE_COMPOSED_INDEPENDENT_FLIP;
}

std::optional<double> NearestRank(std::vector<double>& values, double quantile)
{
    if (values.empty()) return std::nullopt;
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(
        std::ceil(quantile * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(1, rank) - 1];
}

VrrMetricPercentiles SummarizeMetric(
    VrrPacingMetric metric, std::vector<double> values)
{
    VrrMetricPercentiles result;
    result.metric = metric;
    result.finiteSampleCount = values.size();
    if (values.empty()) return result;
    auto p50Values = values;
    auto p95Values = values;
    auto p99Values = values;
    result.p50 = NearestRank(p50Values, 0.50);
    result.p95 = NearestRank(p95Values, 0.95);
    result.p99 = NearestRank(p99Values, 0.99);
    result.maximum = *std::max_element(values.begin(), values.end());
    return result;
}

void AddFinite(std::optional<double> value, std::vector<double>& values)
{
    if (value && std::isfinite(*value)) values.push_back(*value);
}

void AddPositiveFinite(std::optional<double> value, std::vector<double>& values)
{
    if (value && std::isfinite(*value) && *value > 0.0) values.push_back(*value);
}

bool IsDisplayed(const VrrFrameSample& frame) noexcept
{
    return frame.swapChainAddress != 0 && frame.dropped != true &&
        frame.betweenDisplayChangeMs &&
        std::isfinite(*frame.betweenDisplayChangeMs) &&
        *frame.betweenDisplayChangeMs > 0.0;
}

bool HasHighQualityEvidence(const VrrAnalysisResult& result) noexcept
{
    return result.presentation.dominantSwapchain.has_value() &&
        result.presentation.dominantShare >= kDominantSwapchainShare &&
        result.presentation.classification != VrrPresentationClass::Unknown &&
        result.presentation.usablePresentModeSamples >= kMinimumPresentModeSamples &&
        result.cadence.windows.size() >= kMinimumCadenceWindows;
}

void AddReason(VrrAnalysisResult& result, VrrAnalysisReason reason)
{
    if (std::find(result.reasons.begin(), result.reasons.end(), reason) == result.reasons.end())
        result.reasons.push_back(reason);
}
}

VrrIgclClass ClassifyDiagIgclState(const DiagIntelVrrState& state) noexcept
{
    if (state.mappingStatus == DiagIgclTargetMappingStatus::Ambiguous)
        return VrrIgclClass::Ambiguous;
    if (state.mappingStatus != DiagIgclTargetMappingStatus::Exact || !state.profile)
        return VrrIgclClass::Unknown;

    if (state.profile->profile == kIgclArcSyncProfileOff)
        return VrrIgclClass::Off;
    const double minimum = state.profile->minimumHz;
    const double maximum = state.profile->maximumHz;
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum < minimum)
        return VrrIgclClass::Unknown;
    if (std::abs(maximum - minimum) <= 0.5)
        return VrrIgclClass::Off;
    if (!state.capability || !state.capability->supported)
        return VrrIgclClass::Unknown;
    return VrrIgclClass::Enabled;
}

VrrPresentationAnalysis AnalyzeVrrPresentation(
    std::span<const VrrFrameSample> frames, std::uint32_t targetProcessId)
{
    VrrPresentationAnalysis result;
    std::map<std::uint64_t, std::size_t> displayedBySwapchain;
    for (const auto& frame : frames)
    {
        if (frame.processId != targetProcessId || !IsDisplayed(frame)) continue;
        ++displayedBySwapchain[frame.swapChainAddress];
        ++result.usableDisplayedSamples;
    }
    for (const auto& [swapchain, displayedCount] : displayedBySwapchain)
    {
        if (!result.dominantSwapchain ||
            displayedCount > result.dominantDisplayedSamples)
        {
            result.dominantSwapchain = swapchain;
            result.dominantDisplayedSamples = displayedCount;
        }
    }
    if (!result.usableDisplayedSamples || !result.dominantSwapchain) return result;
    result.dominantShare = static_cast<double>(result.dominantDisplayedSamples) /
        static_cast<double>(result.usableDisplayedSamples);
    if (result.dominantShare < kDominantSwapchainShare)
    {
        result.dominantSwapchain.reset();
        result.dominantDisplayedSamples = 0;
        return result;
    }

    std::vector<double> betweenPresents;
    std::vector<double> betweenDisplayChange;
    std::vector<double> displayedTime;
    std::vector<double> untilDisplayed;
    std::vector<double> displayLatency;
    std::vector<double> renderPresentLatency;
    std::vector<double> flipDelay;
    for (const auto& frame : frames)
    {
        if (frame.processId != targetProcessId ||
            frame.swapChainAddress != *result.dominantSwapchain)
            continue;

        ++result.frameCount;
        if (frame.dropped)
        {
            ++result.droppedFlagSamples;
            if (*frame.dropped) ++result.droppedCount;
        }
        if (frame.presentMode != PM_PRESENT_MODE_UNKNOWN)
        {
            ++result.usablePresentModeSamples;
            ++result.presentModeCounts[frame.presentMode];
            if (IsIndependentFlip(frame.presentMode)) ++result.independentFlipSamples;
        }
        if (frame.presentRuntime) ++result.presentRuntimeCounts[*frame.presentRuntime];
        if (frame.frameType)
        {
            ++result.frameTypeCounts[*frame.frameType];
            if (*frame.frameType == PM_FRAME_TYPE_INTEL_XEFG ||
                *frame.frameType == PM_FRAME_TYPE_AMD_AFMF)
                result.frameGenerationPresent = true;
        }
        if (frame.syncInterval) ++result.syncIntervalCounts[*frame.syncInterval];
        if (frame.allowsTearing)
        {
            if (*frame.allowsTearing) ++result.allowsTearingTrue;
            else ++result.allowsTearingFalse;
        }
        else ++result.allowsTearingUnknown;

        AddFinite(frame.betweenPresentsMs, betweenPresents);
        AddPositiveFinite(frame.betweenDisplayChangeMs, betweenDisplayChange);
        AddFinite(frame.displayedTimeMs, displayedTime);
        AddFinite(frame.untilDisplayedMs, untilDisplayed);
        AddFinite(frame.displayLatencyMs, displayLatency);
        AddFinite(frame.renderPresentLatencyMs, renderPresentLatency);
        AddFinite(frame.flipDelayMs, flipDelay);
    }

    if (result.droppedFlagSamples)
        result.droppedPercentage = 100.0 * static_cast<double>(result.droppedCount) /
            static_cast<double>(result.droppedFlagSamples);
    if (result.usablePresentModeSamples)
        result.independentFlipPercentage = 100.0 *
            static_cast<double>(result.independentFlipSamples) /
            static_cast<double>(result.usablePresentModeSamples);
    if (const auto p50 = NearestRank(betweenDisplayChange, 0.50); p50 && *p50 > 0.0)
        result.displayChangeRateHz = 1000.0 / *p50;

    result.pacingMetrics = {
        SummarizeMetric(VrrPacingMetric::BetweenPresents, std::move(betweenPresents)),
        SummarizeMetric(VrrPacingMetric::BetweenDisplayChange, std::move(betweenDisplayChange)),
        SummarizeMetric(VrrPacingMetric::DisplayedTime, std::move(displayedTime)),
        SummarizeMetric(VrrPacingMetric::UntilDisplayed, std::move(untilDisplayed)),
        SummarizeMetric(VrrPacingMetric::DisplayLatency, std::move(displayLatency)),
        SummarizeMetric(VrrPacingMetric::RenderPresentLatency, std::move(renderPresentLatency)),
        SummarizeMetric(VrrPacingMetric::FlipDelay, std::move(flipDelay)),
    };

    if (result.usablePresentModeSamples < kMinimumPresentModeSamples ||
        !result.independentFlipPercentage)
        result.classification = VrrPresentationClass::Unknown;
    else if (*result.independentFlipPercentage >= 90.0)
        result.classification = VrrPresentationClass::VrrSafe;
    else if (*result.independentFlipPercentage >= 50.0)
        result.classification = VrrPresentationClass::Marginal;
    else
        result.classification = VrrPresentationClass::NotVrrSafe;
    return result;
}

VrrAnalysisResult AnalyzeVrrSession(
    std::span<const VrrFrameSample> frames,
    std::uint32_t targetProcessId,
    const DiagD3dkmtCadenceCapture& d3dkmt,
    double nominalRefreshHz,
    const DiagIntelVrrState& igclState,
    bool processIdentityStable,
    bool monitorStable,
    bool displayPathExact)
{
    VrrAnalysisResult result;
    result.cadence = AnalyzeDiagD3dkmtCadence(d3dkmt, nominalRefreshHz);
    result.presentation = AnalyzeVrrPresentation(frames, targetProcessId);
    result.igcl = ClassifyDiagIgclState(igclState);

    if (!processIdentityStable || !monitorStable)
        AddReason(result, VrrAnalysisReason::IdentityUnstable);
    if (!displayPathExact)
        AddReason(result, VrrAnalysisReason::DisplayPathUnavailable);
    if (result.igcl == VrrIgclClass::Unknown)
        AddReason(result, VrrAnalysisReason::IgclUnknown);
    if (result.igcl == VrrIgclClass::Ambiguous)
        AddReason(result, VrrAnalysisReason::IgclAmbiguous);
    if (!result.presentation.dominantSwapchain)
        AddReason(result, VrrAnalysisReason::NoDominantSwapchain);
    if (result.presentation.usablePresentModeSamples < kMinimumPresentModeSamples)
        AddReason(result, VrrAnalysisReason::InsufficientPresentModeSamples);
    if (result.cadence.classification == DiagD3dkmtCadenceClass::Unavailable)
        AddReason(result, VrrAnalysisReason::CadenceUnavailable);
    else if (result.cadence.windows.size() < kMinimumCadenceWindows)
        AddReason(result, VrrAnalysisReason::InsufficientCadenceWindows);

    if (!processIdentityStable || !monitorStable || !displayPathExact)
    {
        result.status = VrrOverallStatus::Inconclusive;
        result.confidence = VrrConfidence::Low;
        return result;
    }

    if (result.igcl == VrrIgclClass::Off)
    {
        result.status = VrrOverallStatus::Off;
        result.confidence = VrrConfidence::Medium;
        if (result.presentation.classification == VrrPresentationClass::VrrSafe &&
            result.cadence.classification == DiagD3dkmtCadenceClass::FixedLike &&
            HasHighQualityEvidence(result))
            result.confidence = VrrConfidence::High;
        return result;
    }

    if (result.igcl != VrrIgclClass::Enabled)
    {
        result.status = VrrOverallStatus::Inconclusive;
        result.confidence = VrrConfidence::Low;
        return result;
    }

    if (!HasHighQualityEvidence(result))
    {
        result.status = VrrOverallStatus::Inconclusive;
        result.confidence = VrrConfidence::Low;
        AddReason(result, VrrAnalysisReason::InsufficientEvidence);
        return result;
    }

    if (result.presentation.classification == VrrPresentationClass::VrrSafe &&
        result.cadence.classification == DiagD3dkmtCadenceClass::VariableLike)
    {
        result.status = VrrOverallStatus::LikelyActive;
        result.confidence = result.presentation.frameGenerationPresent
            ? VrrConfidence::Medium : VrrConfidence::High;
        if (result.presentation.frameGenerationPresent)
            AddReason(result, VrrAnalysisReason::FrameGenerationConfidenceReduction);
        return result;
    }

    if (result.presentation.classification == VrrPresentationClass::VrrSafe &&
        result.cadence.classification == DiagD3dkmtCadenceClass::FixedLike)
    {
        if (!result.presentation.displayChangeRateHz || !std::isfinite(nominalRefreshHz))
        {
            AddReason(result, VrrAnalysisReason::InsufficientEvidence);
        }
        else if (*result.presentation.displayChangeRateHz >= nominalRefreshHz * 0.90)
        {
            AddReason(result, VrrAnalysisReason::GameRateNearNominal);
        }
        else if (!result.presentation.frameGenerationPresent)
        {
            result.status = VrrOverallStatus::LikelyFixed;
            result.confidence = VrrConfidence::Medium;
            return result;
        }
        else
        {
            AddReason(result, VrrAnalysisReason::FrameGenerationConfidenceReduction);
        }
    }
    else
    {
        AddReason(result, VrrAnalysisReason::EvidenceDisagreement);
    }

    result.status = VrrOverallStatus::Inconclusive;
    result.confidence = VrrConfidence::Low;
    return result;
}
