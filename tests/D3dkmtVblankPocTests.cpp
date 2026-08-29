#include "D3dkmtVblankPoc.h"

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
    return ok ? 0 : 1;
}
