#pragma once

#include "PresentMonApi2Client.h"
#include "PresentMonTelemetryTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace clawhud
{
// Narrowly scoped API2 frame telemetry for production game-render verification.
// It answers a single question: has the target PID produced at least one
// displayed frame (PM_METRIC_BETWEEN_DISPLAY_CHANGE > 0)?
//
// The frame query is PID-independent and registered once against the shared
// production session. ConsumeFrames() is driven per target PID. This is not a
// general diagnostic framework: the diagnostic frame survey remains in
// PresentMonApi2Diagnostic.

struct PresentMonFrameQueryPlan
{
    std::vector<PM_QUERY_ELEMENT> elements;
    std::size_t betweenDisplayChangeIndex{};
    // Recorded for identity/debug logging only; never gates readiness.
    std::optional<std::size_t> processIdIndex;
    std::optional<std::size_t> swapChainAddressIndex;
    std::optional<std::size_t> frameTypeIndex;
    std::optional<std::size_t> presentModeIndex;
};

// Requires PM_METRIC_BETWEEN_DISPLAY_CHANGE as an available frame metric on the
// independent device. The optional identity fields are added only when
// introspection confirms support; their absence never blocks the plan.
std::optional<PresentMonFrameQueryPlan> BuildPresentMonFrameQueryPlan(
    const PresentMonTelemetryCapabilities& capabilities);

// Reads a double frame field from one ConsumeFrames record. Non-finite values
// return nullopt.
std::optional<double> DecodePresentMonFrameDouble(
    std::span<const std::uint8_t> record, const PM_QUERY_ELEMENT& element);

// Emits FirstDisplayedFrame evidence exactly once across its lifetime.
class PresentMonDisplayedFrameDetector
{
public:
    // Returns true only on the transition to "a displayed frame has been seen".
    bool Observe(double betweenDisplayChangeMs) noexcept;
    bool DisplayedFrameSeen() const noexcept { return seen_; }
    void Reset() noexcept { seen_ = false; }

private:
    bool seen_{};
};

class PresentMonFrameTelemetry
{
public:
    // Validates capabilities and registers the shared frame query. A failure
    // leaves the component not ready without disturbing the session.
    bool Initialize(
        PresentMonApi2Client& client,
        const PresentMonTelemetryCapabilities& capabilities);

    void Shutdown(PresentMonApi2Client& client) noexcept;

    bool Ready() const noexcept { return ready_; }

    // Consumes queued frames for `processId` and returns true once the first
    // displayed frame has been observed for the current detection target.
    // `processId` transitions reset the first-frame latch. Returns nullopt on a
    // consume failure (the caller keeps polling / the app stays running).
    std::optional<bool> PollDisplayedFrame(
        PresentMonApi2Client& client, std::uint32_t processId);

private:
    PresentMonFrameQueryPlan plan_;
    PM_FRAME_QUERY_HANDLE query_{};
    std::vector<PM_QUERY_ELEMENT> queryElements_;
    std::uint32_t blobSize_{};
    std::uint32_t targetProcessId_{};
    PresentMonDisplayedFrameDetector detector_;
    bool ready_{};
};
}
