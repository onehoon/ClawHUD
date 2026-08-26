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
    return ok ? 0 : 1;
}
