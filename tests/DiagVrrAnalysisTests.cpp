#ifdef NDEBUG
#undef NDEBUG
#endif

#include "DiagD3dkmtCadenceAnalysis.h"
#include "VrrAnalysis.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
constexpr std::int64_t kQpcFrequency = 12000;
constexpr double kNominalHz = 120.0;

DiagD3dkmtCadenceCapture MakeCadence(const std::vector<std::uint32_t>& rates,
    std::int64_t frequency = kQpcFrequency)
{
    DiagD3dkmtCadenceCapture capture;
    capture.available = true;
    capture.qpcFrequency = frequency;
    for (std::size_t second = 0; second < rates.size(); ++second)
    {
        const auto rate = rates[second];
        for (std::uint32_t event = 0; event < rate; ++event)
        {
            const auto offset = static_cast<std::uint64_t>(event) *
                static_cast<std::uint64_t>(frequency) / rate;
            capture.timestamps.push_back(static_cast<std::uint64_t>(second) * frequency + offset);
        }
    }
    capture.timestamps.push_back(static_cast<std::uint64_t>(rates.size()) * frequency);
    return capture;
}

std::vector<VrrFrameSample> MakeFrames(std::size_t count, std::int32_t presentMode,
    double betweenDisplayChangeMs = 16.0)
{
    std::vector<VrrFrameSample> frames;
    frames.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        VrrFrameSample frame{ 42, 0x1234, betweenDisplayChangeMs };
        frame.presentMode = presentMode;
        frame.presentRuntime = 1;
        frame.frameType = PM_FRAME_TYPE_APPLICATION;
        frame.allowsTearing = true;
        frame.dropped = false;
        frame.syncInterval = 0;
        frame.betweenPresentsMs = 16.5;
        frame.displayedTimeMs = 16.0;
        frame.untilDisplayedMs = 1.0;
        frame.displayLatencyMs = 2.0;
        frame.renderPresentLatencyMs = 3.0;
        frame.flipDelayMs = 0.5;
        frames.push_back(frame);
    }
    return frames;
}

DiagIntelVrrState MakeIgcl(std::uint32_t profile = 2,
    float minimumHz = 48.0f, float maximumHz = 120.0f, bool supported = true)
{
    DiagIntelVrrState state;
    state.mappingStatus = DiagIgclTargetMappingStatus::Exact;
    state.capability = DiagArcSyncCapability{ supported, minimumHz, maximumHz };
    state.profile = DiagArcSyncProfile{ profile, maximumHz, minimumHz };
    return state;
}

bool HasReason(const VrrAnalysisResult& result, VrrAnalysisReason reason)
{
    for (const auto actual : result.reasons)
        if (actual == reason) return true;
    return false;
}

void TestCadence()
{
    const auto fixed = AnalyzeDiagD3dkmtCadence(
        MakeCadence(std::vector<std::uint32_t>(12, 120)), kNominalHz);
    assert(fixed.dataStatus == DiagD3dkmtCadenceDataStatus::Available);
    assert(fixed.classification == DiagD3dkmtCadenceClass::FixedLike);
    assert(fixed.windows.size() == 12);
    assert(fixed.minimumHz == 120.0 && fixed.maximumHz == 120.0);
    assert(fixed.averageHz == 120.0 && fixed.medianHz == 120.0);
    assert(fixed.standardDeviationHz == 0.0 && fixed.nearNominalRatio == 1.0);

    const auto lower = AnalyzeDiagD3dkmtCadence(
        MakeCadence(std::vector<std::uint32_t>(12, 60)), kNominalHz);
    assert(lower.classification == DiagD3dkmtCadenceClass::Indeterminate);
    assert(lower.minimumHz == 60.0 && lower.nearNominalRatio == 0.0);

    std::vector<std::uint32_t> variableRates;
    for (int i = 0; i < 4; ++i)
        variableRates.insert(variableRates.end(), { 90, 110, 100 });
    const auto variable = AnalyzeDiagD3dkmtCadence(MakeCadence(variableRates), kNominalHz);
    assert(variable.classification == DiagD3dkmtCadenceClass::VariableLike);
    assert(variable.minimumHz == 90.0 && variable.maximumHz == 110.0);
    assert(variable.averageHz == 100.0 && variable.medianHz == 100.0);
    assert(std::abs(*variable.standardDeviationHz - std::sqrt(200.0 / 3.0)) < 1e-9);
    assert(variable.nearNominalRatio == 0.0 && variable.rangeWidthHz == 20.0);

    const auto insufficient = AnalyzeDiagD3dkmtCadence(
        MakeCadence(std::vector<std::uint32_t>(9, 120)), kNominalHz);
    assert(insufficient.windows.size() == 9);
    assert(insufficient.classification == DiagD3dkmtCadenceClass::Indeterminate);

    std::vector<std::uint32_t> fixedBoundaryRates(8, 120);
    fixedBoundaryRates.insert(fixedBoundaryRates.end(), { 114, 114 });
    const auto fixedBoundary = AnalyzeDiagD3dkmtCadence(
        MakeCadence(fixedBoundaryRates), kNominalHz);
    assert(fixedBoundary.nearNominalRatio == 0.8);
    assert(fixedBoundary.rangeWidthHz == 6.0);
    assert(fixedBoundary.classification == DiagD3dkmtCadenceClass::FixedLike);

    const auto variableBoundary = AnalyzeDiagD3dkmtCadence(
        MakeCadence({ 117, 123, 117, 123, 117, 123, 117, 123, 117, 123 }), kNominalHz);
    assert(variableBoundary.nearNominalRatio == 0.0);
    assert(variableBoundary.rangeWidthHz == 6.0);
    assert(variableBoundary.classification == DiagD3dkmtCadenceClass::VariableLike);

    auto invalidOrder = MakeCadence({ 120, 120 });
    invalidOrder.timestamps[2] = invalidOrder.timestamps[1];
    const auto invalid = AnalyzeDiagD3dkmtCadence(invalidOrder, kNominalHz);
    assert(invalid.dataStatus == DiagD3dkmtCadenceDataStatus::InvalidTimestamps);
    assert(invalid.classification == DiagD3dkmtCadenceClass::Indeterminate);

    auto invalidFrequency = MakeCadence({ 120, 120 });
    invalidFrequency.qpcFrequency = 0;
    assert(AnalyzeDiagD3dkmtCadence(invalidFrequency, kNominalHz).dataStatus ==
        DiagD3dkmtCadenceDataStatus::InvalidFrequency);

    auto unavailable = MakeCadence({ 120, 120 });
    unavailable.available = false;
    assert(AnalyzeDiagD3dkmtCadence(unavailable, kNominalHz).classification ==
        DiagD3dkmtCadenceClass::Unavailable);

    assert(std::abs(DiagVrrNearNominalToleranceHz(120.0) - 1.8) < 1e-12);
    assert(DiagVrrCadenceIsNearNominal(118.2, 120.0));
    assert(DiagVrrCadenceIsNearNominal(121.8, 120.0));
    assert(!DiagVrrCadenceIsNearNominal(118.19, 120.0));
    assert(!DiagVrrCadenceIsNearNominal(121.81, 120.0));
    assert(DiagVrrNearNominalToleranceHz(100.0) == 1.5);

    DiagD3dkmtCadenceCapture boundaries{ true, 100 };
    boundaries.timestamps = { 0, 99, 100, 199, 200, 299, 300 };
    const auto windows = AnalyzeDiagD3dkmtCadence(boundaries, 2.0);
    assert(windows.windows.size() == 3);
    for (const auto& window : windows.windows)
        assert(window.eventCount == 2 && window.measuredHz == 2.0);
}

void TestPresentationAnalysis()
{
    auto frames = MakeFrames(54, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    for (std::size_t i = 0; i < frames.size(); ++i)
        frames[i].betweenPresentsMs = static_cast<double>(i + 1);
    auto composed = MakeFrames(6, PM_PRESENT_MODE_COMPOSED_FLIP);
    for (std::size_t i = 0; i < composed.size(); ++i)
        composed[i].betweenPresentsMs = static_cast<double>(i + 55);
    frames.insert(frames.end(), composed.begin(), composed.end());
    auto otherChain = MakeFrames(6, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    for (auto& frame : otherChain) frame.swapChainAddress = 0x5678;
    frames.insert(frames.end(), otherChain.begin(), otherChain.end());
    frames.push_back({ 43, 0x9999, 10.0 });
    frames.push_back({ 42, 0, 10.0 });
    frames.push_back({ 42, 0x1234, std::numeric_limits<double>::quiet_NaN() });
    auto dropped = VrrFrameSample{ 42, 0x1234, std::nullopt };
    dropped.dropped = true;
    frames.push_back(dropped);

    const auto result = AnalyzeVrrPresentation(frames, 42);
    assert(result.classification == VrrPresentationClass::VrrSafe);
    assert(result.dominantSwapchain == 0x1234);
    assert(result.dominantDisplayedSamples == 60);
    assert(result.usableDisplayedSamples == 66);
    assert(std::abs(result.dominantShare - (60.0 / 66.0)) < 0.0001);
    assert(result.presentModeCounts.at(PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP) == 54);
    assert(result.usablePresentModeSamples == 60);
    assert(result.independentFlipPercentage == 90.0);
    assert(result.droppedCount == 1 && std::abs(*result.droppedPercentage - 100.0 / 61.0) < 0.0001);
    assert(result.displayChangeRateHz == 62.5);
    assert(result.pacingMetrics.size() == 7);
    assert(result.pacingMetrics[0].p50 == 30.0);
    assert(result.pacingMetrics[0].p95 == 57.0);
    assert(result.pacingMetrics[0].p99 == 60.0);
    assert(result.pacingMetrics[0].maximum == 60.0);

    auto marginal = MakeFrames(60, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    for (std::size_t i = 0; i < 30; ++i)
        marginal[i].presentMode = PM_PRESENT_MODE_COMPOSED_FLIP;
    assert(AnalyzeVrrPresentation(marginal, 42).classification ==
        VrrPresentationClass::Marginal);
    marginal[30].presentMode = PM_PRESENT_MODE_COMPOSED_FLIP;
    assert(AnalyzeVrrPresentation(marginal, 42).classification ==
        VrrPresentationClass::NotVrrSafe);

    auto exactDominance = MakeFrames(70, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    auto minorityChain = MakeFrames(30, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    for (auto& frame : minorityChain) frame.swapChainAddress = 0x5678;
    exactDominance.insert(exactDominance.end(), minorityChain.begin(), minorityChain.end());
    const auto atThreshold = AnalyzeVrrPresentation(exactDominance, 42);
    assert(atThreshold.dominantSwapchain == 0x1234);
    assert(atThreshold.dominantShare == 0.70);

    auto tearingDisabled = MakeFrames(60, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    for (auto& frame : tearingDisabled) frame.allowsTearing = false;
    assert(AnalyzeVrrPresentation(tearingDisabled, 42).classification ==
        VrrPresentationClass::VrrSafe);

    auto noDominant = MakeFrames(69, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    auto other = MakeFrames(31, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    for (auto& frame : other) frame.swapChainAddress = 0x5678;
    noDominant.insert(noDominant.end(), other.begin(), other.end());
    const auto split = AnalyzeVrrPresentation(noDominant, 42);
    assert(!split.dominantSwapchain);
    assert(split.classification == VrrPresentationClass::Unknown);

    auto generated = MakeFrames(60, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    generated.front().frameType = PM_FRAME_TYPE_INTEL_XEFG;
    assert(AnalyzeVrrPresentation(generated, 42).frameGenerationPresent);
}

void TestIgclClassification()
{
    assert(ClassifyDiagIgclState(MakeIgcl()) == VrrIgclClass::Enabled);
    assert(ClassifyDiagIgclState(MakeIgcl(5)) == VrrIgclClass::Off);
    assert(ClassifyDiagIgclState(MakeIgcl(2, 119.5f, 120.0f)) == VrrIgclClass::Off);
    assert(ClassifyDiagIgclState(MakeIgcl(2, 119.499f, 120.0f)) == VrrIgclClass::Enabled);
    assert(ClassifyDiagIgclState(MakeIgcl(2, 48.0f, 120.0f, false)) == VrrIgclClass::Unknown);

    auto unknown = MakeIgcl();
    unknown.mappingStatus = DiagIgclTargetMappingStatus::Unknown;
    assert(ClassifyDiagIgclState(unknown) == VrrIgclClass::Unknown);
    unknown.mappingStatus = DiagIgclTargetMappingStatus::Ambiguous;
    assert(ClassifyDiagIgclState(unknown) == VrrIgclClass::Ambiguous);
}

void TestCombinedAnalysis()
{
    auto frames = MakeFrames(60, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    std::vector<std::uint32_t> variableRates;
    for (int i = 0; i < 4; ++i)
        variableRates.insert(variableRates.end(), { 90, 110, 100 });
    const auto variableCadence = MakeCadence(variableRates);
    const auto active = AnalyzeVrrSession(frames, 42, variableCadence, kNominalHz,
        MakeIgcl(), true, true, true);
    assert(active.status == VrrOverallStatus::LikelyActive);
    assert(active.confidence == VrrConfidence::High);

    const auto fixedCadence = MakeCadence(std::vector<std::uint32_t>(12, 120));
    const auto fixed = AnalyzeVrrSession(frames, 42, fixedCadence, kNominalHz,
        MakeIgcl(), true, true, true);
    assert(fixed.status == VrrOverallStatus::LikelyFixed);
    assert(fixed.confidence == VrrConfidence::Medium);

    auto nearNominalFrames = MakeFrames(60, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP, 8.0);
    const auto nearNominal = AnalyzeVrrSession(nearNominalFrames, 42, fixedCadence,
        kNominalHz, MakeIgcl(), true, true, true);
    assert(nearNominal.status == VrrOverallStatus::Inconclusive);
    assert(HasReason(nearNominal, VrrAnalysisReason::GameRateNearNominal));

    auto exactlyNearNominalFrames = MakeFrames(60,
        PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP, 1000.0 / 108.0);
    const auto exactlyNearNominal = AnalyzeVrrSession(exactlyNearNominalFrames, 42,
        fixedCadence, kNominalHz, MakeIgcl(), true, true, true);
    assert(exactlyNearNominal.status == VrrOverallStatus::Inconclusive);
    assert(HasReason(exactlyNearNominal, VrrAnalysisReason::GameRateNearNominal));

    const auto disabled = AnalyzeVrrSession(frames, 42, fixedCadence, kNominalHz,
        MakeIgcl(5), true, true, true);
    assert(disabled.status == VrrOverallStatus::Off);
    assert(disabled.confidence == VrrConfidence::High);

    auto ambiguousIgcl = MakeIgcl();
    ambiguousIgcl.mappingStatus = DiagIgclTargetMappingStatus::Ambiguous;
    const auto ambiguous = AnalyzeVrrSession(frames, 42, variableCadence, kNominalHz,
        ambiguousIgcl, true, true, true);
    assert(ambiguous.status == VrrOverallStatus::Inconclusive);
    assert(HasReason(ambiguous, VrrAnalysisReason::IgclAmbiguous));

    const auto unstable = AnalyzeVrrSession(frames, 42, variableCadence, kNominalHz,
        MakeIgcl(), false, true, true);
    assert(unstable.status == VrrOverallStatus::Inconclusive);
    assert(HasReason(unstable, VrrAnalysisReason::IdentityUnstable));

    auto notSafeFrames = MakeFrames(60, PM_PRESENT_MODE_COMPOSED_FLIP);
    const auto notSafe = AnalyzeVrrSession(notSafeFrames, 42, variableCadence, kNominalHz,
        MakeIgcl(), true, true, true);
    assert(notSafe.status == VrrOverallStatus::Inconclusive);
    assert(notSafe.presentation.classification == VrrPresentationClass::NotVrrSafe);
    assert(HasReason(notSafe, VrrAnalysisReason::EvidenceDisagreement));

    auto missingRateFrames = MakeFrames(60, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP,
        std::numeric_limits<double>::quiet_NaN());
    const auto missingRate = AnalyzeVrrSession(missingRateFrames, 42, fixedCadence,
        kNominalHz, MakeIgcl(), true, true, true);
    assert(missingRate.status == VrrOverallStatus::Inconclusive);
    assert(HasReason(missingRate, VrrAnalysisReason::InsufficientEvidence));

    const auto missingPath = AnalyzeVrrSession(frames, 42, variableCadence, kNominalHz,
        MakeIgcl(), true, true, false);
    assert(missingPath.status == VrrOverallStatus::Inconclusive);
    assert(HasReason(missingPath, VrrAnalysisReason::DisplayPathUnavailable));

    auto generatedFrames = MakeFrames(60, PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP);
    generatedFrames.front().frameType = PM_FRAME_TYPE_AMD_AFMF;
    const auto generated = AnalyzeVrrSession(generatedFrames, 42, variableCadence,
        kNominalHz, MakeIgcl(), true, true, true);
    assert(generated.status == VrrOverallStatus::LikelyActive);
    assert(generated.confidence == VrrConfidence::Medium);
    assert(HasReason(generated, VrrAnalysisReason::FrameGenerationConfidenceReduction));
}
}

int main()
{
    TestCadence();
    TestPresentationAnalysis();
    TestIgclClassification();
    TestCombinedAnalysis();
}
