#include "VrrDiagnosticAnalysis.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{
using clawhud::VrrCsvSummary;

VrrCsvSummary Summary(double independentFlip, std::size_t rows = 100)
{
    const auto independentRows = static_cast<std::size_t>(std::lround(independentFlip * rows / 100.0));
    VrrCsvSummary result;
    result.valid = true;
    result.rows = rows;
    result.dominantSwapChain = "game-swapchain";
    result.dominantRows = rows;
    result.presentModeSamples = rows;
    result.modes["Hardware Composed: Independent Flip"] = independentRows;
    result.modes["Composed: Flip"] = rows - independentRows;
    return result;
}

bool Expect(bool condition, const char* name)
{
    if (!condition) std::cerr << "FAILED: " << name << '\n';
    return condition;
}

bool ExpectVerdict(clawhud::VrrDiagnosticVerdict expected, double off, double stat, double dyn,
    const char* name, std::size_t rows = 100)
{
    const auto actual = clawhud::EvaluateVrrComparison(
        Summary(off, rows), Summary(stat, rows), Summary(dyn, rows)).verdict;
    return Expect(actual == expected, name);
}
}

int main()
{
    bool ok = true;
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Pass, 95, 94, 92, "normal");
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Fail, 95, 20, 18, "static regression");
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Fail, 95, 94, 25, "dynamic regression");
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Inconclusive, 10, 10, 10, "invalid baseline");
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Fail, 95, 5, 5, "same modes reversed");
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Pass, 95, 91, 89, "minor variation");
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Inconclusive, 95, 94, 92, "insufficient samples", 5);

    VrrCsvSummary malformed;
    malformed.reason = "PresentMode data is missing";
    ok &= Expect(clawhud::EvaluateVrrComparison(malformed, Summary(95), Summary(95)).verdict ==
        clawhud::VrrDiagnosticVerdict::Inconclusive, "malformed data");

    const std::string csv =
        "ProcessID,SwapChainAddress,PresentMode,MsUntilDisplayed,MsBetweenPresents,MsBetweenDisplayChange\n"
        "1,game,Hardware Composed: Independent Flip,1,16.6,16.6\n"
        "1,game,Composed: Flip,1,16.7,16.7\n";
    const auto parsed = clawhud::ParseVrrCsvText(csv);
    ok &= Expect(parsed.valid && parsed.rows == 2 && parsed.modes.size() == 2,
        "CSV distribution parsing");
    ok &= Expect(std::abs(clawhud::IndependentFlipPercentage(parsed) - 50.0) < 0.01,
        "Independent Flip percentage");
    ok &= Expect(clawhud::IndependentFlipPercentageIfAvailable(parsed).has_value(),
        "available summary exposes Independent Flip percentage");

    const std::string crlfCsv =
        "ProcessID,SwapChainAddress,PresentMode,MsUntilDisplayed,MsBetweenPresents,MsBetweenDisplayChange\r\n"
        "1,game,Hardware Composed: Independent Flip,1,16.6,16.6\r\n"
        "1,game,Composed: Flip,1,16.7,16.7\r\n";
    const auto parsedCrlf = clawhud::ParseVrrCsvText(crlfCsv);
    ok &= Expect(parsedCrlf.valid && parsedCrlf.modes.size() == 2,
        "Windows CRLF CSV parsing");

    const auto missing = clawhud::ParseVrrCsvText(
        "ProcessID,SwapChainAddress,MsUntilDisplayed,MsBetweenPresents,MsBetweenDisplayChange\n"
        "1,game,1,16.6,16.6\n");
    ok &= Expect(!missing.valid, "missing PresentMode column");

    std::string sparseCsv =
        "ProcessID,SwapChainAddress,PresentMode,MsUntilDisplayed,MsBetweenPresents,MsBetweenDisplayChange\n";
    sparseCsv += "1,game,Hardware Composed: Independent Flip,1,16.6,16.6\n";
    for (int i = 1; i < 20; ++i)
        sparseCsv += "1,game,NA,1,16.6,16.6\n";
    const auto sparse = clawhud::ParseVrrCsvText(sparseCsv);
    ok &= Expect(sparse.valid && sparse.rows == 20 && sparse.presentModeSamples == 1,
        "usable PresentMode sample count");
    ok &= Expect(clawhud::EvaluateVrrComparison(sparse, Summary(95), Summary(95)).verdict ==
        clawhud::VrrDiagnosticVerdict::Inconclusive, "insufficient usable PresentMode samples");

    std::string recreatedCsv =
        "ProcessID,SwapChainAddress,PresentMode,MsUntilDisplayed,MsBetweenPresents,MsBetweenDisplayChange\n";
    for (int i = 0; i < 20; ++i)
        recreatedCsv += "1,baseline,Hardware Composed: Independent Flip,1,16.6,16.6\n";
    for (int i = 0; i < 100; ++i)
        recreatedCsv += "1,replacement,Composed: Flip,1,16.6,16.6\n";
    const auto recreated = clawhud::ParseVrrCsvText(recreatedCsv, "baseline");
    ok &= Expect(recreated.valid && recreated.dominantSwapChain == "replacement" &&
        !recreated.preferredSwapChainUsed, "stale preferred swapchain is not selected");
    ok &= Expect(clawhud::EvaluateVrrComparison(Summary(95), Summary(95), recreated).verdict ==
        clawhud::VrrDiagnosticVerdict::Fail, "recreated swapchain regression fails");

    const std::string rangedCsv =
        "ProcessID,SwapChainAddress,PresentMode,MsUntilDisplayed,MsBetweenPresents,MsBetweenDisplayChange,CPUStartQPCTimeInMs\n"
        "1,game,Hardware Composed: Independent Flip,1,16.6,16.6,0\n"
        "1,game,Hardware Composed: Independent Flip,1,16.6,16.6,14000\n"
        "1,game,Hardware Composed: Independent Flip,1,16.6,16.6,28000\n"
        "1,game,Composed: Flip,1,16.6,16.6,29000\n"
        "1,game,Composed: Flip,1,16.6,16.6,41000\n"
        "1,game,Composed: Flip,1,16.6,16.6,57000\n"
        "1,game,Hardware Composed: Independent Flip,1,16.6,16.6,58000\n"
        "1,game,Hardware Composed: Independent Flip,1,16.6,16.6,72000\n"
        "1,game,Hardware Composed: Independent Flip,1,16.6,16.6,86000\n";
    const auto offRange = clawhud::ParseVrrCsvText(rangedCsv, {}, 0.0, 28000.0);
    const auto staticRange = clawhud::ParseVrrCsvText(rangedCsv, "game", 29000.0, 57000.0);
    const auto dynamicRange = clawhud::ParseVrrCsvText(rangedCsv, "game", 58000.0, 86000.0);
    ok &= Expect(offRange.valid && offRange.rows == 3 && offRange.sufficientCoverage,
        "OFF QPC range and full coverage");
    ok &= Expect(staticRange.valid && staticRange.rows == 3 && staticRange.sufficientCoverage,
        "STATIC QPC range excludes transition");
    ok &= Expect(dynamicRange.valid && dynamicRange.rows == 3 && dynamicRange.sufficientCoverage,
        "DYNAMIC QPC range excludes transition");
    const auto boundaryRange = clawhud::ParseVrrCsvText(rangedCsv, {}, 0.0, 20000.0);
    ok &= Expect(boundaryRange.sufficientCoverage, "coverage at the 70 percent boundary remains usable");
    const std::string shortCsv =
        "ProcessID,SwapChainAddress,PresentMode,MsUntilDisplayed,MsBetweenPresents,MsBetweenDisplayChange,CPUStartQPCTimeInMs\n"
        "1,game,Hardware Composed: Independent Flip,1,16.6,16.6,0\n"
        "1,game,Hardware Composed: Independent Flip,1,16.6,16.6,9000\n";
    const auto insufficient = clawhud::ParseVrrCsvText(shortCsv, {}, 0.0, 28000.0);
    ok &= Expect(!insufficient.sufficientCoverage, "insufficient QPC coverage is rejected");
    ok &= Expect(!clawhud::IndependentFlipPercentageIfAvailable(insufficient).has_value(),
        "insufficient-coverage Independent Flip is unavailable");
    const std::string twentyFourSecondCsv = shortCsv +
        "1,game,Hardware Composed: Independent Flip,1,16.6,16.6,24000\n";
    const auto twentyFourSeconds = clawhud::ParseVrrCsvText(twentyFourSecondCsv, {}, 0.0, 28000.0);
    ok &= Expect(twentyFourSeconds.sufficientCoverage, "24 seconds of a 28 second phase is usable");
    VrrCsvSummary unavailable;
    ok &= Expect(!clawhud::IndependentFlipPercentageIfAvailable(unavailable).has_value(),
        "missing phase Independent Flip is unavailable");
    ok &= Expect(clawhud::EvaluateVrrComparison(offRange, staticRange, unavailable).verdict ==
        clawhud::VrrDiagnosticVerdict::Inconclusive, "missing phase is diagnostic inconclusive");
    ok &= Expect(clawhud::VrrDiagnosticRuntimeStatusForResult(
        true, clawhud::VrrDiagnosticVerdict::Inconclusive) ==
        clawhud::VrrDiagnosticRuntimeStatus::Inconclusive,
        "complete inconclusive diagnostic keeps inconclusive runtime status");
    ok &= Expect(clawhud::VrrDiagnosticRuntimeStatusForResult(
        false, clawhud::VrrDiagnosticVerdict::Inconclusive) ==
        clawhud::VrrDiagnosticRuntimeStatus::Failed,
        "incomplete diagnostic reports failed runtime status");
    return ok ? 0 : 1;
}
