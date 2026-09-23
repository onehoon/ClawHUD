#ifdef NDEBUG
#undef NDEBUG
#endif

#include "DisplayPathProbe.h"

#include <cassert>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
VrrActiveDisplayPath Path(std::wstring device, UINT32 targetId,
    UINT32 numerator = 60000, UINT32 denominator = 1001)
{
    return { std::move(device), LUID{ 7, 2 }, 3, LUID{ 11, 4 }, targetId,
        numerator, denominator };
}
}

int main()
{
    const RECT monitor{ 10, 20, 3010, 1700 };
    const std::vector<VrrActiveDisplayPath> active{
        Path(L"\\.\\DISPLAY1", 9), Path(L"\\.\\DISPLAY2", 10) };
    const auto exact = ResolveVrrDisplayPath(L"\\.\\display1", monitor, active);
    assert(exact.status == DisplayPathProbeStatus::Matched);
    assert(exact.path);
    assert(exact.path->sourceAdapterLuid.LowPart == 7);
    assert(exact.path->sourceId == 3);
    assert(exact.path->targetAdapterLuid.LowPart == 11);
    assert(exact.path->targetId == 9);
    assert(exact.path->refreshRateNumerator == 60000);
    assert(exact.path->refreshRateDenominator == 1001);
    assert(std::abs(exact.path->nominalRefreshHz - (60000.0 / 1001.0)) < 1e-9);
    assert(!exact.path->usedModeRefreshFallback);
    assert(exact.path->monitorRect.right == monitor.right);

    const auto noMatch = ResolveVrrDisplayPath(L"\\.\\DISPLAY3", monitor, active);
    assert(noMatch.status == DisplayPathProbeStatus::NotFound);
    assert(!noMatch.path);

    const std::vector<VrrActiveDisplayPath> cloned{
        Path(L"\\.\\DISPLAY1", 9), Path(L"\\.\\DISPLAY1", 12) };
    const auto ambiguous = ResolveVrrDisplayPath(L"\\.\\DISPLAY1", monitor, cloned);
    assert(ambiguous.status == DisplayPathProbeStatus::Ambiguous);
    assert(!ambiguous.path);

    const auto incomplete = ResolveVrrDisplayPath(L"\\.\\DISPLAY1", monitor,
        active, false);
    assert(incomplete.status == DisplayPathProbeStatus::Unavailable);
    const auto incompleteNoMatch = ResolveVrrDisplayPath(L"\\.\\DISPLAY3", monitor,
        active, false);
    assert(incompleteNoMatch.status == DisplayPathProbeStatus::Unavailable);

    const std::vector<VrrActiveDisplayPath> noRational{
        Path(L"\\.\\DISPLAY1", 9, 0, 0) };
    const auto fallback = ResolveVrrDisplayPath(L"\\.\\DISPLAY1", monitor,
        noRational, true, 75.0);
    assert(fallback.status == DisplayPathProbeStatus::Matched);
    assert(fallback.path);
    assert(fallback.path->nominalRefreshHz == 75.0);
    assert(fallback.path->usedModeRefreshFallback);
    assert(fallback.path->refreshRateNumerator == 0);
    assert(fallback.path->refreshRateDenominator == 0);
}
