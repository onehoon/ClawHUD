#include "IgclTelemetryDiagnostic.h"

#include <iostream>

using namespace clawhud;
void PlayDiagnosticCompletionSound() noexcept {}

int main()
{
    bool ok = true;
    auto check = [&](bool value, const char* name) { if (!value) { std::cerr << name << "\n"; ok = false; } };
    IgclSampleSeries changing; changing.values = { 0.0, 10.0, 20.0 }; changing.rawValues = { 0, 10, 20 };
    check(ClassifyIgclSamples(changing) == IgclDiagnosticClass::SupportedActive, "changing values");
    IgclSampleSeries zero; zero.values = { 0.0, 0.0 }; check(ClassifyIgclSamples(zero) == IgclDiagnosticClass::SupportedZero, "supported zero");
    IgclSampleSeries constant; constant.values = { 12.0, 12.0, 12.0 }; check(ClassifyIgclSamples(constant) == IgclDiagnosticClass::SupportedConstant, "constant value");
    IgclSampleSeries unsupported; unsupported.supported = false; unsupported.values = { 0.0 }; check(ClassifyIgclSamples(unsupported) == IgclDiagnosticClass::Unsupported, "unsupported");
    IgclSampleSeries missing; missing.hasDomain = false; check(ClassifyIgclSamples(missing) == IgclDiagnosticClass::NoDomain, "no domain");
    IgclSampleSeries error; error.apiSucceeded = false; check(ClassifyIgclSamples(error) == IgclDiagnosticClass::ApiError, "api error");
    IgclSampleSeries symbol; symbol.symbolPresent = false; check(ClassifyIgclSamples(symbol) == IgclDiagnosticClass::SymbolMissing, "missing symbol");
    IgclSampleSeries largeCounter; largeCounter.values = { 9007199254740992.0, 9007199254740992.0 };
    largeCounter.rawValues = { 9007199254740992ULL, 9007199254740993ULL };
    largeCounter.types = { 7, 7 };
    check(ClassifyIgclSamples(largeCounter) == IgclDiagnosticClass::SupportedActive, "changing uint64 counter");
    IgclSampleSeries twenty;
    for (int i = 0; i < 20; ++i) twenty.values.push_back(static_cast<double>(i));
    check(twenty.values.size() == 20, "twenty-sample aggregation");
    check(IgclSampleMinimum(changing) == 0.0 && IgclSampleMaximum(changing) == 20.0, "min max");
    check(std::string(IgclDiagnosticClassName(IgclDiagnosticClass::SymbolMissing)) == "SYMBOL_MISSING", "raw classification names");
    check(std::string(IgclDiagnosticClassName(IgclDiagnosticClass::SkippedMutationCapable)) == "SKIPPED_MUTATION_CAPABLE", "safety classification name");
    check(IsIgclApplicationNameLengthValid(127), "127-byte application name");
    check(!IsIgclApplicationNameLengthValid(128), "128-byte application name rejected");
    IgclSampleSeries intermittentFailure; intermittentFailure.values = { 100.0, 110.0 }; intermittentFailure.apiSucceeded = false;
    check(ClassifyIgclSamples(intermittentFailure) == IgclDiagnosticClass::ApiError, "leaf reports intermittent API failure");
    return ok ? 0 : 1;
}
