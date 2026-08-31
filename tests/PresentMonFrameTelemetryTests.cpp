#include "PresentMonFrameTelemetry.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

PresentMonMetricCapability FrameMetric(PM_METRIC id,
    PM_METRIC_TYPE type = PM_METRIC_TYPE_DYNAMIC_FRAME,
    PM_METRIC_AVAILABILITY avail = PM_METRIC_AVAILABILITY_AVAILABLE)
{
    PresentMonMetricCapability metric{};
    metric.id = id;
    metric.type = type;
    metric.devices.push_back({0, avail, 1});
    return metric;
}

PresentMonTelemetryCapabilities Capabilities(bool withBetweenDisplayChange = true,
    bool withExtras = true)
{
    PresentMonTelemetryCapabilities caps;
    caps.devices.push_back({0, PM_DEVICE_TYPE_INDEPENDENT,
        PM_DEVICE_VENDOR_UNKNOWN, "Independent"});
    if (withBetweenDisplayChange)
        caps.metrics.push_back(FrameMetric(PM_METRIC_BETWEEN_DISPLAY_CHANGE));
    if (withExtras)
    {
        caps.metrics.push_back(FrameMetric(PM_METRIC_PROCESS_ID));
        caps.metrics.push_back(FrameMetric(PM_METRIC_SWAP_CHAIN_ADDRESS));
    }
    return caps;
}

// Fakes the frame-query endpoints so the armed-attempt lifecycle can be driven.
class FakeClient final : public PresentMonApi2Client
{
public:
    std::vector<PM_QUERY_ELEMENT> elements;
    std::uint32_t recordSize{sizeof(double)};
    int flushCount{};
    std::uint32_t lastFlushPid{};
    // Frames the next ConsumeFrames returns (BETWEEN_DISPLAY_CHANGE value each).
    std::vector<double> pending;

    PM_STATUS RegisterFrameQuery(PM_FRAME_QUERY_HANDLE* query,
        PM_QUERY_ELEMENT* els, std::uint64_t count, std::uint32_t* blobSize) override
    {
        elements.clear();
        for (std::uint64_t i = 0; i < count; ++i)
        {
            els[i].dataOffset = 0;
            els[i].dataSize = sizeof(double);
            elements.push_back(els[i]);
        }
        *blobSize = recordSize;
        *query = reinterpret_cast<PM_FRAME_QUERY_HANDLE>(this);
        return PM_STATUS_SUCCESS;
    }

    PM_STATUS FreeFrameQuery(PM_FRAME_QUERY_HANDLE) override { return PM_STATUS_SUCCESS; }

    PM_STATUS FlushFrames(std::uint32_t pid) override
    {
        ++flushCount;
        lastFlushPid = pid;
        pending.clear();
        return PM_STATUS_SUCCESS;
    }

    PM_STATUS ConsumeFrames(PM_FRAME_QUERY_HANDLE, std::uint32_t,
        std::uint8_t* blob, std::uint32_t* frameCount) override
    {
        const std::uint32_t n = std::min<std::uint32_t>(
            *frameCount, static_cast<std::uint32_t>(pending.size()));
        for (std::uint32_t i = 0; i < n; ++i)
            std::memcpy(blob + static_cast<std::size_t>(i) * recordSize,
                &pending[i], sizeof(double));
        pending.clear();
        *frameCount = n;
        return PM_STATUS_SUCCESS;
    }
};

void QueryPlanning(bool& ok)
{
    const auto plan = BuildPresentMonFrameQueryPlan(Capabilities());
    ok &= Check(plan && plan->elements.at(plan->betweenDisplayChangeIndex).metric ==
        PM_METRIC_BETWEEN_DISPLAY_CHANGE, "the required element is BETWEEN_DISPLAY_CHANGE");
    ok &= Check(plan && plan->processIdIndex && plan->swapChainAddressIndex,
        "optional identity fields are planned when supported");
    ok &= Check(!BuildPresentMonFrameQueryPlan(Capabilities(false, true)),
        "no BETWEEN_DISPLAY_CHANGE means no plan");
    auto nonFrame = Capabilities(false, false);
    nonFrame.metrics.push_back(FrameMetric(PM_METRIC_BETWEEN_DISPLAY_CHANGE,
        PM_METRIC_TYPE_DYNAMIC));
    ok &= Check(!BuildPresentMonFrameQueryPlan(nonFrame),
        "a non-frame BETWEEN_DISPLAY_CHANGE metric is rejected");
}

void Decoding(bool& ok)
{
    std::vector<std::uint8_t> record(16);
    PM_QUERY_ELEMENT element{PM_METRIC_BETWEEN_DISPLAY_CHANGE, PM_STAT_NONE, 0, 0,
        0, sizeof(double)};
    const double value = 8.33;
    std::memcpy(record.data(), &value, sizeof(value));
    ok &= Check(DecodePresentMonFrameDouble(record, element) == value,
        "a double frame field decodes from its offset");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::memcpy(record.data(), &nan, sizeof(nan));
    ok &= Check(!DecodePresentMonFrameDouble(record, element),
        "a non-finite frame field is unavailable");
}

void FirstFrameDetector(bool& ok)
{
    PresentMonDisplayedFrameDetector detector;
    ok &= Check(!detector.Observe(0.0) && !detector.Observe(-1.0),
        "non-positive intervals are not displayed frames");
    ok &= Check(detector.Observe(8.33) && detector.DisplayedFrameSeen(),
        "the first positive interval reports the first displayed frame");
    ok &= Check(!detector.Observe(16.6), "subsequent frames do not re-report");
    detector.Reset();
    ok &= Check(!detector.DisplayedFrameSeen() && detector.Observe(1.0),
        "Reset re-arms the detector");
}

void ArmedAttemptLifecycle(bool& ok)
{
    FakeClient client;
    PresentMonFrameTelemetry frame;
    ok &= Check(frame.Initialize(client, Capabilities()) && frame.Ready(),
        "Initialize registers the shared frame query");

    ok &= Check(!frame.PollDisplayedFrame(client, 100),
        "polling before BeginVerification yields nullopt (not armed)");

    ok &= Check(frame.BeginVerification(client, 100) && client.flushCount == 1 &&
        client.lastFlushPid == 100,
        "BeginVerification arms PID 100 and flushes stale frames");
    ok &= Check(frame.PollDisplayedFrame(client, 100) == false,
        "an armed attempt with no frames reports 'not yet'");

    client.pending = {0.0, 12.5};
    const auto seen = frame.PollDisplayedFrame(client, 100);
    ok &= Check(seen == true, "a positive BETWEEN_DISPLAY_CHANGE reports the first frame");
    ok &= Check(frame.PollDisplayedFrame(client, 100) == true,
        "the first-frame result latches for the current attempt");

    ok &= Check(!frame.PollDisplayedFrame(client, 200),
        "polling a PID that is not the armed target yields nullopt");

    // Regression: a second attempt on the SAME numeric PID must NOT inherit the
    // previous attempt's displayed-frame evidence.
    ok &= Check(frame.BeginVerification(client, 100) && client.flushCount == 2,
        "a repeat attempt on the same PID re-arms and re-flushes");
    ok &= Check(frame.PollDisplayedFrame(client, 100) == false,
        "the re-armed attempt does not report a displayed frame from history");
    client.pending = {9.9};
    ok &= Check(frame.PollDisplayedFrame(client, 100) == true,
        "the re-armed attempt reports only after a genuinely new positive sample");

    frame.Shutdown(client);
    ok &= Check(!frame.Ready() && !frame.PollDisplayedFrame(client, 100),
        "Shutdown frees the query and disarms");
}

int RunTests()
{
    bool ok = true;
    QueryPlanning(ok);
    Decoding(ok);
    FirstFrameDetector(ok);
    ArmedAttemptLifecycle(ok);
    return ok ? 0 : 1;
}
}

int main()
{
    return RunTests();
}
