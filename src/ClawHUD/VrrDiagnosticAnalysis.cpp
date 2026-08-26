#include "VrrDiagnosticAnalysis.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace clawhud
{
namespace
{
std::vector<std::string> CsvLine(std::string_view line)
{
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (c == '"')
        {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"')
            {
                field += '"';
                ++i;
            }
            else
                quoted = !quoted;
        }
        else if (c == ',' && !quoted)
        {
            fields.push_back(field);
            field.clear();
        }
        else
            field += c;
    }
    fields.push_back(field);
    return fields;
}

bool ParseDouble(const std::string& text, double& value)
{
    if (text.empty() || text == "NA") return false;
    try
    {
        std::size_t consumed{};
        value = std::stod(text, &consumed);
        return consumed == text.size();
    }
    catch (...)
    {
        return false;
    }
}

void StripTrailingCarriageReturn(std::string& line) noexcept
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
}
}

VrrCsvSummary ParseVrrCsvText(std::string_view text, std::string_view preferredSwapChain)
{
    VrrCsvSummary result;
    std::istringstream input{ std::string(text) };
    std::string line;
    if (!std::getline(input, line))
    {
        result.reason = "CSV is empty";
        return result;
    }
    StripTrailingCarriageReturn(line);
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF)
        line.erase(0, 3);

    const auto headers = CsvLine(line);
    std::map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < headers.size(); ++i) columns[headers[i]] = i;
    constexpr const char* required[] = {
        "ProcessID", "SwapChainAddress", "PresentMode", "MsUntilDisplayed",
        "MsBetweenPresents", "MsBetweenDisplayChange" };
    for (const auto name : required)
    {
        if (!columns.contains(name))
        {
            result.reason = std::string("Required PresentMon column missing: ") + name;
            return result;
        }
    }

    const auto field = [&](const std::vector<std::string>& row, const char* name)
        -> std::string
    {
        const auto index = columns.at(name);
        return index < row.size() ? row[index] : std::string{};
    };

    struct Row
    {
        std::vector<std::string> fields;
    };
    std::vector<Row> rows;
    std::map<std::string, std::size_t> swapCounts;
    while (std::getline(input, line))
    {
        StripTrailingCarriageReturn(line);
        if (line.empty()) continue;
        auto fields = CsvLine(line);
        ++result.rows;
        ++swapCounts[field(fields, "SwapChainAddress")];
        rows.push_back({ std::move(fields) });
    }
    if (!result.rows)
    {
        result.reason = "CSV has no data rows";
        return result;
    }

    for (const auto& [swap, count] : swapCounts)
    {
        if (count > result.dominantRows)
        {
            result.dominantSwapChain = swap;
            result.dominantRows = count;
        }
    }

    if (!preferredSwapChain.empty())
    {
        const auto preferred = swapCounts.find(std::string(preferredSwapChain));
        if (preferred != swapCounts.end() && preferred->second >= result.dominantRows)
        {
            result.dominantSwapChain = preferred->first;
            result.dominantRows = preferred->second;
            result.preferredSwapChainUsed = true;
        }
    }
    result.dominantRows = swapCounts[result.dominantSwapChain];
    result.rows = result.dominantRows;

    double presentTotal{}, displayTotal{};
    std::size_t presentCount{}, displayCount{};
    for (const auto& row : rows)
    {
        if (field(row.fields, "SwapChainAddress") != result.dominantSwapChain) continue;
        const auto mode = field(row.fields, "PresentMode");
        if (mode.empty() || mode == "NA") continue;
        ++result.presentModeSamples;
        ++result.modes[mode];

        const auto untilDisplayed = field(row.fields, "MsUntilDisplayed");
        if (!untilDisplayed.empty() && untilDisplayed != "NA") ++result.displayed;
        else ++result.notDisplayed;

        double value{};
        if (ParseDouble(field(row.fields, "MsBetweenPresents"), value))
        {
            presentTotal += value;
            ++presentCount;
        }
        if (ParseDouble(field(row.fields, "MsBetweenDisplayChange"), value))
        {
            displayTotal += value;
            ++displayCount;
            if (displayCount == 1 || value < result.displayMin) result.displayMin = value;
            if (displayCount == 1 || value > result.displayMax) result.displayMax = value;
        }
    }
    if (result.modes.empty())
    {
        result.reason = "PresentMode data is missing";
        return result;
    }
    if (!presentCount)
    {
        result.reason = "No usable MsBetweenPresents samples on dominant swapchain";
        return result;
    }
    if (!displayCount)
    {
        result.reason = "No usable MsBetweenDisplayChange samples on dominant swapchain";
        return result;
    }
    result.presentAverage = presentTotal / presentCount;
    result.displayAverage = displayTotal / displayCount;
    result.valid = true;
    return result;
}

VrrCsvSummary ParseVrrCsvFile(const std::filesystem::path& path, std::string_view preferredSwapChain)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        VrrCsvSummary result;
        result.reason = "CSV could not be opened";
        return result;
    }
    std::ostringstream text;
    text << file.rdbuf();
    return ParseVrrCsvText(text.str(), preferredSwapChain);
}

bool IsIndependentFlipPresentMode(std::string_view mode) noexcept
{
    return mode.find("Independent Flip") != std::string_view::npos;
}

double IndependentFlipPercentage(const VrrCsvSummary& summary) noexcept
{
    if (summary.modes.empty()) return 0.0;
    std::size_t independent{};
    for (const auto& [mode, count] : summary.modes)
        if (IsIndependentFlipPresentMode(mode)) independent += count;
    std::size_t total{};
    for (const auto& [mode, count] : summary.modes) total += count;
    return total == 0 ? 0.0 : 100.0 * static_cast<double>(independent) / static_cast<double>(total);
}

std::string DominantPresentMode(const VrrCsvSummary& summary)
{
    std::string dominant;
    std::size_t count{};
    for (const auto& [mode, value] : summary.modes)
    {
        if (value > count)
        {
            dominant = mode;
            count = value;
        }
    }
    return dominant;
}

VrrDiagnosticEvaluation EvaluateVrrComparison(
    const VrrCsvSummary& off,
    const VrrCsvSummary& staticHud,
    const VrrCsvSummary& dynamicHud)
{
    if (!off.valid || !staticHud.valid || !dynamicHud.valid)
        return { VrrDiagnosticVerdict::Inconclusive, "One or more phase CSV summaries are invalid." };
    if (off.presentModeSamples < kMinimumVrrComparisonSamples ||
        staticHud.presentModeSamples < kMinimumVrrComparisonSamples ||
        dynamicHud.presentModeSamples < kMinimumVrrComparisonSamples)
        return { VrrDiagnosticVerdict::Inconclusive, "Comparison phases did not provide enough PresentMode samples." };

    const double offPercentage = IndependentFlipPercentage(off);
    if (offPercentage < kBaselineIndependentFlipMinimumPercent)
        return { VrrDiagnosticVerdict::Inconclusive,
            "HUD-OFF baseline did not establish an Independent Flip presentation path." };

    const auto evaluatePhase = [&](const VrrCsvSummary& phase, const char* name)
        -> std::optional<VrrDiagnosticEvaluation>
    {
        const double percentage = IndependentFlipPercentage(phase);
        const double drop = offPercentage - percentage;
        if (percentage < kFailureIndependentFlipMaximumPercent ||
            drop >= kFailureIndependentFlipDropPoints)
        {
            return VrrDiagnosticEvaluation{
                VrrDiagnosticVerdict::Fail,
                std::string(name) + " HUD caused a major Independent Flip regression." };
        }
        return std::nullopt;
    };
    if (const auto failure = evaluatePhase(staticHud, "STATIC")) return *failure;
    if (const auto failure = evaluatePhase(dynamicHud, "DYNAMIC")) return *failure;
    return { VrrDiagnosticVerdict::Pass, "HUD phases retained the dominant Independent Flip presentation path." };
}

const char* VrrDiagnosticVerdictName(VrrDiagnosticVerdict verdict) noexcept
{
    switch (verdict)
    {
    case VrrDiagnosticVerdict::Pass: return "PASS";
    case VrrDiagnosticVerdict::Fail: return "FAIL";
    case VrrDiagnosticVerdict::Inconclusive: return "INCONCLUSIVE";
    }
    return "INCONCLUSIVE";
}
}
