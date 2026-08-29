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

bool ExpectVerdict(clawhud::VrrDiagnosticVerdict expected, double off, double dyn,
    const char* name, std::size_t rows = 100)
{
    const auto actual = clawhud::EvaluateVrrComparison(
        Summary(off, rows), Summary(dyn, rows)).verdict;
    return Expect(actual == expected, name);
}
}

int main()
{
    bool ok = true;
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Pass, 95, 92, "normal");
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Fail, 95, 25, "dynamic regression");
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Inconclusive, 10, 10, "invalid baseline");
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Fail, 95, 5, "dynamic regression with same modes");
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Pass, 95, 89, "minor variation");
    ok &= ExpectVerdict(clawhud::VrrDiagnosticVerdict::Inconclusive, 95, 92, "insufficient samples", 5);

    VrrCsvSummary malformed;
    malformed.reason = "PresentMode data is missing";
    ok &= Expect(clawhud::EvaluateVrrComparison(malformed, Summary(95)).verdict ==
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
    ok &= Expect(clawhud::EvaluateVrrComparison(sparse, Summary(95)).verdict ==
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
    ok &= Expect(clawhud::EvaluateVrrComparison(Summary(95), recreated).verdict ==
        clawhud::VrrDiagnosticVerdict::Fail, "recreated swapchain regression fails");
    return ok ? 0 : 1;
}
