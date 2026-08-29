#include "GameDetection/PresentActivitySource.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

clawhud::PresentActivitySample Sample(DWORD processId, double qpc,
    std::optional<bool> displayed = std::nullopt)
{
    clawhud::PresentActivitySample sample;
    sample.processId = processId;
    sample.application = processId == 100 ? "game.exe" : "tool.exe";
    sample.qpcTimeMs = qpc;
    sample.displayed = displayed;
    return sample;
}
}

int main()
{
    const std::vector<std::string> headers{
        "PresentMode", "CPUStartQPCTimeInMs", "ProcessID", "Application",
        "FrameType", "SwapChainAddress", "MsBetweenDisplayChange"};
    const auto schema = clawhud::ParsePresentActivitySchema(headers);
    Check(schema.HasRequiredColumns(), "required schema columns are recognized");
    Check(schema.presentMode && schema.frameType && schema.swapChainAddress &&
        schema.msBetweenDisplayChange, "optional schema columns are recognized");

    const std::vector<std::string> row{
        "Hardware:IndependentFlip", "1000.5", "100", "game.exe", "Application",
        "0x123", "16.7"};
    const auto parsed = clawhud::ParsePresentActivityRow(schema, row);
    Check(parsed.has_value(), "valid row parses");
    Check(parsed->processId == 100 && parsed->qpcTimeMs == 1000.5,
        "required values are parsed");
    Check(parsed->displayed == true, "positive display interval is displayed");
    Check(parsed->presentMode == "Hardware:IndependentFlip" &&
        parsed->frameType == "Application" && parsed->swapChainAddress == "0x123",
        "optional values are parsed");

    const auto missing = clawhud::ParsePresentActivitySchema({"Application", "ProcessID"});
    Check(!missing.HasRequiredColumns(), "missing timestamp is rejected");
    Check(!clawhud::ParsePresentActivityRow(schema, {"mode", "bad", "100", "game.exe"}),
        "invalid timestamp is rejected");
    Check(!clawhud::ParsePresentActivityRow(schema,
        {"mode", "1000", "100", "NA"}), "missing application is rejected");

    const auto command = clawhud::BuildPresentActivityCommandLine(
        L"C:\\tools\\PresentMon.exe", L"ClawHUD-PresentActivity-1-2");
    Check(command.find(L"--session_name") != std::wstring::npos,
        "diagnostic command has a session name");
    Check(command.find(L"--process_id") == std::wstring::npos,
        "diagnostic command is global");
    Check(command.find(L"--stop_existing_session") == std::wstring::npos,
        "diagnostic command does not stop another session");
    Check(clawhud::EscapePresentActivityValue("a\\b\"c\n") == L"a\\\\b\\\"c\\n",
        "log field escaping is bounded");

    clawhud::PresentActivityAggregator aggregator;
    aggregator.Consume(Sample(100, 1000.0, true));
    aggregator.Consume(Sample(200, 1000.0));
    aggregator.Consume(Sample(100, 1100.0, false));
    aggregator.Consume(Sample(100, 1200.0, true));
    auto summaries = aggregator.Consume(Sample(100, 1500.0, false));
    Check(summaries.size() == 1, "activity window emits one summary");
    Check(summaries[0].processId == 100 && summaries[0].presentCount == 3,
        "summary aggregates one PID only");
    Check(summaries[0].displayedCount == 2,
        "displayed count includes only positive intervals");
    Check(summaries[0].displayCountAvailable,
        "displayed count availability is retained");
    summaries = aggregator.Consume(Sample(200, 1600.0));
    Check(summaries.size() == 1 && summaries[0].processId == 200,
        "PID streams remain independent");

    auto noDisplay = clawhud::PresentActivityAggregator{};
    noDisplay.Consume(Sample(300, 2000.0));
    summaries = noDisplay.Consume(Sample(300, 2500.0));
    Check(summaries.size() == 1 && summaries[0].displayedCount == 0,
        "unavailable display intervals do not fabricate a displayed count");

    clawhud::PresentActivitySource source;
    source.Stop();
    source.Stop();
    std::cout << "PASS\n";
}
