#include "PresentMonHudTelemetry.h"

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
bool Near(double actual, double expected)
{
    return std::abs(actual - expected) < 0.5;
}

std::size_t CountEvents(const std::vector<PresentMonHudEvent>& events,
    PresentMonHudEventType type)
{
    std::size_t count{};
    for (const auto& event : events)
        if (event.type == type)
            ++count;
    return count;
}
}

int main()
{
    bool ok = true;
    const auto command = BuildPresentMonCommandLine(
        L"C:\\tools\\PresentMon.exe", 1234, L"ClawHUD-HUD-1234");
    ok &= Check(command.find(L"--stop_existing_session") != std::wstring::npos &&
        command.find(L"--session_name \"ClawHUD-HUD-1234\"") != std::wstring::npos &&
        command.find(L"--terminate_on_proc_exit") != std::wstring::npos,
        "PresentMon command enables stale-session recovery");
    ok &= Check(Near(CalculateDisplayedFps(60, 0.500).value(), 120.0), "120 FPS bucket");
    ok &= Check(Near(CalculateDisplayedFps(20, 0.500).value(), 40.0), "40 FPS bucket");
    ok &= Check(Near(CalculateDisplayedFps(37, 0.507).value(), 37.0 / 0.507),
        "actual elapsed bucket");
    ok &= Check(!CalculateDisplayedFps(0, 0.500), "empty window unavailable");
    ok &= Check(!CalculateDisplayedFps(20, 0.0), "invalid elapsed window unavailable");
    ok &= Check(Near(CalculateDisplayedFpsFromIntervals({250.0, 250.0}).value(), 4.0),
        "display-timeline 500ms bucket");
    ok &= Check(!CalculateDisplayedFpsFromIntervals({100.0, 200.0}),
        "incomplete display-timeline bucket unavailable");
    ok &= Check(!CalculateDisplayedFpsFromIntervals({250.0, -1.0}),
        "invalid display interval unavailable");

    const std::vector<std::string> headers{
        "Application", "ProcessID", "SwapChainAddress", "PresentRuntime",
        "SyncInterval", "PresentFlags", "AllowsTearing", "PresentMode",
        "FrameType", "TimeInQPC", "MsBetweenSimulationStart",
        "MsBetweenPresents", "MsBetweenDisplayChange", "MsInPresentAPI",
        "MsRenderPresentLatency", "MsUntilDisplayed"};
    const auto row = [](const char* interval, const char* frameType)
    {
        return std::vector<std::string>{"game.exe", "1234", "0x1", "DXGI",
            "1", "0", "False", "Hardware: Independent Flip", frameType,
            "10.0", "8.33", "8.33", interval, "1.0", "2.0", "3.0"};
    };
    const auto application = ParseDisplayedFrame(headers, row("8.33", "Application"));
    ok &= Check(application && application->frameType == "Application",
        "application frame accepted");
    const auto generated = ParseDisplayedFrame(headers, row("8.33", "Intel XeSS-FG"));
    ok &= Check(generated && generated->frameType == "Intel XeSS-FG",
        "generated frame accepted");
    auto headersWithoutFrameType = headers;
    headersWithoutFrameType.erase(headersWithoutFrameType.begin() + 8);
    auto rowWithoutFrameType = row("8.33", "Application");
    rowWithoutFrameType.erase(rowWithoutFrameType.begin() + 8);
    ok &= Check(ParseDisplayedFrame(headersWithoutFrameType, rowWithoutFrameType).has_value(),
        "frame type remains optional");
    ok &= Check(!ParseDisplayedFrame(headers, row("NA", "Application")),
        "not-displayed row ignored");
    ok &= Check(!ParseDisplayedFrame(headers, row("", "Application")),
        "empty display interval ignored");
    ok &= Check(!ParseDisplayedFrame(headers, row("0", "Application")),
        "zero display interval ignored");
    ok &= Check(!ParseDisplayedFrame(headers, row("-1", "Application")),
        "negative display interval ignored");
    ok &= Check(!ParseDisplayedFrame(headers, row("bad", "Application")),
        "malformed row ignored");

    PresentMonFrameAccumulator accumulator;
    const auto invalidEvents = accumulator.Observe({0.0, "Application"});
    ok &= Check(invalidEvents.empty(), "invalid frame produces no event");
    const auto firstEvents = accumulator.Observe({8.33, "Application"});
    ok &= Check(CountEvents(firstEvents, PresentMonHudEventType::FirstDisplayedFrame) == 1 &&
        CountEvents(firstEvents, PresentMonHudEventType::FpsUpdate) == 0,
        "first valid frame emits renderer evidence immediately");

    auto events = firstEvents;
    for (std::size_t i = 1; i <= 59; ++i)
    {
        const auto rowEvents = accumulator.Observe({8.33, "Application"});
        events.insert(events.end(), rowEvents.begin(), rowEvents.end());
    }
    ok &= Check(CountEvents(events, PresentMonHudEventType::FirstDisplayedFrame) == 1,
        "first displayed frame emits only once");
    ok &= Check(CountEvents(events, PresentMonHudEventType::FpsUpdate) == 0,
        "FPS waits for the aggregation window");

    const auto fpsEvents = accumulator.Observe({8.33, "Application"});
    ok &= Check(CountEvents(fpsEvents, PresentMonHudEventType::FpsUpdate) == 1 &&
        fpsEvents.back().displayedFps && Near(*fpsEvents.back().displayedFps, 120.0),
        "FPS update preserves calculated value");

    accumulator.Reset();
    const auto resetEvents = accumulator.Observe({8.33, "Application"});
    ok &= Check(CountEvents(resetEvents, PresentMonHudEventType::FirstDisplayedFrame) == 1,
        "new session resets first displayed frame state");
    ok &= Check(events.size() >= 1 &&
        events.front().type == PresentMonHudEventType::FirstDisplayedFrame,
        "first displayed frame precedes FPS updates");
    const PresentMonHudEvent streamEnded{
        PresentMonHudEventType::StreamEnded, std::nullopt};
    ok &= Check(streamEnded.type == PresentMonHudEventType::StreamEnded &&
        !streamEnded.displayedFps, "stream end has an explicit event type");

    std::vector<std::vector<std::string>> rows;
    rows.reserve(61);
    for (std::size_t i = 0; i < 61; ++i)
        rows.push_back(row("8.33", i % 2 == 0 ? "Application" : "Intel XeSS-FG"));
    std::vector<double> intervals;
    for (const auto& csvRow : rows)
    {
        const auto sample = ParseDisplayedFrame(headers, csvRow);
        if (sample) intervals.push_back(sample->msBetweenDisplayChange);
    }
    ok &= Check(intervals.size() == 61, "displayed rows collected from default schema");
    ok &= Check(Near(CalculateDisplayedFpsFromIntervals(intervals).value(), 120.0),
        "default schema displayed FPS regression");
    return ok ? 0 : 1;
}
