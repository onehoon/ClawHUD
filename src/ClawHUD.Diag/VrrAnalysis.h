#pragma once

#include "DiagD3dkmtCadenceAnalysis.h"
#include "DiagIntelVrrStateProbe.h"
#include "VrrApi2FrameCapture.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

enum class VrrPresentationClass
{
    Unknown,
    VrrSafe,
    Marginal,
    NotVrrSafe,
};

enum class VrrIgclClass
{
    Unknown,
    Ambiguous,
    Off,
    Enabled,
};

enum class VrrOverallStatus
{
    Inconclusive,
    Off,
    LikelyFixed,
    LikelyActive,
};

enum class VrrConfidence
{
    Low,
    Medium,
    High,
};

enum class VrrAnalysisReason
{
    InsufficientEvidence,
    IdentityUnstable,
    DisplayPathUnavailable,
    IgclUnknown,
    IgclAmbiguous,
    NoDominantSwapchain,
    InsufficientPresentModeSamples,
    CadenceUnavailable,
    InsufficientCadenceWindows,
    EvidenceDisagreement,
    GameRateNearNominal,
    FrameGenerationConfidenceReduction,
};

enum class VrrPacingMetric
{
    BetweenPresents,
    BetweenDisplayChange,
    DisplayedTime,
    UntilDisplayed,
    DisplayLatency,
    RenderPresentLatency,
    FlipDelay,
};

struct VrrMetricPercentiles
{
    VrrPacingMetric metric{};
    std::size_t finiteSampleCount{};
    std::optional<double> p50;
    std::optional<double> p95;
    std::optional<double> p99;
    std::optional<double> maximum;
};

struct VrrPresentationAnalysis
{
    VrrPresentationClass classification{ VrrPresentationClass::Unknown };
    std::optional<std::uint64_t> dominantSwapchain;
    std::size_t usableDisplayedSamples{};
    std::size_t dominantDisplayedSamples{};
    double dominantShare{};
    std::size_t frameCount{};
    std::size_t droppedCount{};
    std::size_t droppedFlagSamples{};
    std::optional<double> droppedPercentage;
    std::map<std::int32_t, std::size_t> presentModeCounts;
    std::map<std::int32_t, std::size_t> presentRuntimeCounts;
    std::map<std::int32_t, std::size_t> frameTypeCounts;
    std::map<std::int32_t, std::size_t> syncIntervalCounts;
    std::size_t allowsTearingTrue{};
    std::size_t allowsTearingFalse{};
    std::size_t allowsTearingUnknown{};
    std::size_t independentFlipSamples{};
    std::size_t usablePresentModeSamples{};
    std::optional<double> independentFlipPercentage;
    std::optional<double> displayChangeRateHz;
    std::vector<VrrMetricPercentiles> pacingMetrics;
    bool frameGenerationPresent{};
};

struct VrrAnalysisResult
{
    VrrOverallStatus status{ VrrOverallStatus::Inconclusive };
    VrrConfidence confidence{ VrrConfidence::Low };
    VrrIgclClass igcl{ VrrIgclClass::Unknown };
    DiagD3dkmtCadenceAnalysis cadence;
    VrrPresentationAnalysis presentation;
    std::vector<VrrAnalysisReason> reasons;
};

VrrIgclClass ClassifyDiagIgclState(const DiagIntelVrrState& state) noexcept;

VrrPresentationAnalysis AnalyzeVrrPresentation(
    std::span<const VrrFrameSample> frames, std::uint32_t targetProcessId);

VrrAnalysisResult AnalyzeVrrSession(
    std::span<const VrrFrameSample> frames,
    std::uint32_t targetProcessId,
    const DiagD3dkmtCadenceCapture& d3dkmt,
    double nominalRefreshHz,
    const DiagIntelVrrState& igcl,
    bool processIdentityStable,
    bool monitorStable,
    bool displayPathExact);
