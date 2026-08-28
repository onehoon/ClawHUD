#include "IntelVrrDiagnosticProbe.h"

#include <cmath>
#include <iostream>

using namespace clawhud;

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
    VblankSeries series{};
    RecordVblankTimestamp(series, 1000000);
    RecordVblankTimestamp(series, 1000000);
    RecordVblankTimestamp(series, 1008333);
    RecordVblankTimestamp(series, 1016666);
    auto summary = SummarizeVblank(series);
    ok &= Check(summary.uniqueSamples == 3 && summary.validDeltas == 2, "duplicates are ignored");
    ok &= Check(summary.measuredHz && std::abs(*summary.measuredHz - 120.0048) < 0.1, "120 Hz cadence");

    VblankSeries slow{};
    RecordVblankTimestamp(slow, 2000000);
    RecordVblankTimestamp(slow, 2013699);
    RecordVblankTimestamp(slow, 2027398);
    summary = SummarizeVblank(slow);
    ok &= Check(summary.measuredHz && std::abs(*summary.measuredHz - 72.998) < 0.1, "73 Hz cadence");

    VblankSeries reset{};
    RecordVblankTimestamp(reset, 3000000);
    RecordVblankTimestamp(reset, 3008333);
    RecordVblankTimestamp(reset, 100);
    RecordVblankTimestamp(reset, 8433);
    summary = SummarizeVblank(reset);
    ok &= Check(summary.validDeltas == 1 && reset.resetCount == 1, "non-monotonic reset does not create a cross-reset delta");

    VblankSeries empty{}; ok &= Check(!SummarizeVblank(empty).measuredHz, "empty is unavailable");
    VblankSeries one{}; RecordVblankTimestamp(one, 1); const auto oneSummary = SummarizeVblank(one);
    ok &= Check(!oneSummary.measuredHz && oneSummary.validDeltas == 0 && oneSummary.averageDeltaUs == 0 && oneSummary.medianDeltaUs == 0,
        "one timestamp has no cadence statistics");
    ok &= Check(!UsableVblankMedian({oneSummary}), "insufficient VBlank evidence stays unavailable in comparison");
    ok &= Check(kVblankPollInterval <= std::chrono::milliseconds(1), "VBlank poll interval remains cadence-safe");

    VblankSeries output0{}; output0.output = 0; output0.target = 0;
    VblankSeries output1{}; output1.output = 1; output1.target = 0;
    RecordVblankTimestamp(output0, 1); RecordVblankTimestamp(output0, 10001);
    RecordVblankTimestamp(output1, 2); RecordVblankTimestamp(output1, 20002);
    ok &= Check(output0.output != output1.output && SummarizeVblank(output0).validDeltas == 1 && SummarizeVblank(output1).validDeltas == 1,
        "output target series remain separate");
    ok &= Check(IntelCtlResultName(0xDEADBEEF) == "UNKNOWN", "unknown result name remains raw-compatible");
    ok &= Check(IntelCtlResultName(0x00000001) == "CTL_RESULT_SUCCESS_STILL_OPEN_BY_ANOTHER_CALLER",
        "still-open success result name");
    ok &= Check(IntelCtlResultName(0x40000017) == "CTL_RESULT_ERROR_KMD_CALL", "KMD call failure result name");
    ok &= Check(IntelCtlResultName(0x40000010) == "CTL_RESULT_ERROR_UNSUPPORTED_SIZE", "unsupported size result name");
    ok &= Check(IntelCtlResultName(0x40000008) == "CTL_RESULT_ERROR_UNINITIALIZED", "uninitialized result name");
    return ok ? 0 : 1;
}
