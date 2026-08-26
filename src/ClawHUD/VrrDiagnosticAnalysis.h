#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace clawhud
{
struct VrrCsvSummary
{
    bool valid{};
    std::string reason;
    std::size_t rows{};
    std::string dominantSwapChain;
    std::size_t dominantRows{};
    std::size_t presentModeSamples{};
    std::map<std::string, std::size_t> modes;
    bool preferredSwapChainUsed{};
    std::size_t displayed{};
    std::size_t notDisplayed{};
    double presentAverage{};
    double displayAverage{};
    double displayMin{};
    double displayMax{};
};

enum class VrrDiagnosticVerdict
{
    Pass,
    Fail,
    Inconclusive,
};

struct VrrDiagnosticEvaluation
{
    VrrDiagnosticVerdict verdict{ VrrDiagnosticVerdict::Inconclusive };
    std::string reason;
};

inline constexpr std::size_t kMinimumVrrComparisonSamples = 20;
inline constexpr double kBaselineIndependentFlipMinimumPercent = 80.0;
inline constexpr double kFailureIndependentFlipMaximumPercent = 50.0;
inline constexpr double kFailureIndependentFlipDropPoints = 30.0;

VrrCsvSummary ParseVrrCsvText(
    std::string_view text,
    std::string_view preferredSwapChain = {});
VrrCsvSummary ParseVrrCsvFile(
    const std::filesystem::path& path,
    std::string_view preferredSwapChain = {});

bool IsIndependentFlipPresentMode(std::string_view mode) noexcept;
double IndependentFlipPercentage(const VrrCsvSummary& summary) noexcept;
std::string DominantPresentMode(const VrrCsvSummary& summary);
VrrDiagnosticEvaluation EvaluateVrrComparison(
    const VrrCsvSummary& off,
    const VrrCsvSummary& staticHud,
    const VrrCsvSummary& dynamicHud);
const char* VrrDiagnosticVerdictName(VrrDiagnosticVerdict verdict) noexcept;
}
