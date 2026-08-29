#include "D3dkmtVblankProbe.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool ok = true;
    const auto atHz = [](double hz)
    {
        return static_cast<std::uint64_t>(std::llround(10'000'000.0 / hz));
    };
    const auto stats120 = CalculateD3dkmtVblankStatistics(
        {0, atHz(120), atHz(120) * 2, atHz(120) * 3}, 0, 10'000'000);
    ok &= Check(stats120.measuredHzMedian &&
        std::abs(*stats120.measuredHzMedian - 120.0) < 0.01, "120 Hz cadence");
    const auto stats80 = CalculateD3dkmtVblankStatistics(
        {0, atHz(80), atHz(80) * 2, atHz(80) * 3}, 0, 10'000'000);
    ok &= Check(stats80.measuredHzMedian &&
        std::abs(*stats80.measuredHzMedian - 80.0) < 0.01, "80 Hz cadence");
    const auto stats60 = CalculateD3dkmtVblankStatistics(
        {0, atHz(60), atHz(60) * 2, atHz(60) * 3}, 0, 10'000'000);
    ok &= Check(stats60.measuredHzMedian &&
        std::abs(*stats60.measuredHzMedian - 60.0) < 0.01, "60 Hz cadence");
    const auto insufficient = CalculateD3dkmtVblankStatistics({123}, 2, 10'000'000);
    ok &= Check(!insufficient.measuredHzMedian && insufficient.failedWaits == 2,
        "insufficient samples are unavailable");
    const auto outlier = CalculateD3dkmtVblankStatistics(
        {0, 125'000, 250'000, 1'250'000, 1'375'000}, 0, 10'000'000);
    ok &= Check(outlier.minimumDeltaMs > 12.4 && outlier.maximumDeltaMs > 99.0 &&
        outlier.medianDeltaMs > 12.4 && outlier.medianDeltaMs < 12.6,
        "outlier preserves raw range and median cadence");

    const auto makeWindows = [](const std::vector<std::size_t>& counts)
    {
        std::vector<std::uint64_t> timestamps;
        for (std::size_t window = 0; window < counts.size(); ++window)
            for (std::size_t event = 0; event < counts[window]; ++event)
                timestamps.push_back(static_cast<std::uint64_t>(
                    (window + static_cast<double>(event) / counts[window]) *
                    10'000'000.0));
        return timestamps;
    };
    const auto windows120 = CalculateD3dkmtVblankWindows(
        makeWindows({120, 120, 120}), 10'000'000);
    ok &= Check(windows120.size() == 2 && windows120[0].eventCount == 120 &&
        windows120[0].measuredHz == 120.0 && windows120[1].measuredHz == 120.0,
        "constant 120 Hz windows");
    const auto windows80 = CalculateD3dkmtVblankWindows(
        makeWindows({80, 80, 80}), 10'000'000);
    ok &= Check(windows80.size() == 2 && windows80[0].measuredHz == 80.0 &&
        windows80[1].measuredHz == 80.0, "constant 80 Hz windows");
    const auto variableWindows = CalculateD3dkmtVblankWindows(
        makeWindows({90, 110, 100}), 10'000'000);
    ok &= Check(variableWindows.size() == 2, "variable cadence has two full windows");
    if (variableWindows.size() == 2)
    {
        ok &= Check(variableWindows[0].eventCount == 90 &&
            variableWindows[0].measuredHz == 90.0, "variable cadence first window");
        ok &= Check(variableWindows[1].eventCount == 110 &&
            variableWindows[1].measuredHz == 110.0, "variable cadence second window");
    }
    ok &= Check(CalculateD3dkmtVblankWindows({}, 10'000'000).empty() &&
        CalculateD3dkmtVblankWindows({123}, 10'000'000).empty(),
        "empty and short data have no complete windows");
    ok &= Check(CalculateD3dkmtVblankStatistics(
        {0, 10}, 0, 0).measuredHzMedian == std::nullopt,
        "invalid QPC frequency is unavailable");
    const auto nonMonotonic = CalculateD3dkmtVblankStatistics(
        {0, 10, 5, 20}, 0, 10);
    ok &= Check(nonMonotonic.minimumDeltaMs == 1000.0 &&
        nonMonotonic.maximumDeltaMs == 1500.0,
        "non-monotonic samples ignore invalid deltas");
    const auto boundaryWindows = CalculateD3dkmtVblankWindows(
        {0, 10'000'000, 20'000'000}, 10'000'000);
    ok &= Check(boundaryWindows.size() == 2 && boundaryWindows[0].eventCount == 1 &&
        boundaryWindows[1].eventCount == 1,
        "window boundaries use a half-open interval");
    ok &= Check(CalculateD3dkmtVblankWindows(
        {0, 10'000'000}, 10'000'000, 0.0).empty(),
        "invalid window size is unavailable");
    return ok ? 0 : 1;
}
