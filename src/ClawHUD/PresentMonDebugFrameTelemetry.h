#pragma once

#include "PresentMonApi2Client.h"
#include "PresentMonTelemetryTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace clawhud
{
// Debug-only per-frame evidence for a single process, taken from the shared
// production PresentMon API2 session. This replaces the PresentMon.exe stdout
// stream that Debug Logging used to consume. It is observation only and never
// feeds production game detection.

struct PresentMonDebugFrame
{
    std::optional<std::uint64_t> swapChainAddress;
    std::optional<std::int32_t> presentMode; // PM_PRESENT_MODE
    std::optional<std::int32_t> frameType;   // PM_FRAME_TYPE
    std::optional<double> betweenDisplayChangeMs;
};

struct PresentMonDebugFrameQueryPlan
{
    std::vector<PM_QUERY_ELEMENT> elements;
    std::optional<std::size_t> swapChainAddressIndex;
    std::optional<std::size_t> presentModeIndex;
    std::optional<std::size_t> frameTypeIndex;
    std::optional<std::size_t> betweenDisplayChangeIndex;

    bool Empty() const noexcept { return elements.empty(); }
};

// Adds every supported field; a plan with no fields is unusable.
PresentMonDebugFrameQueryPlan BuildPresentMonDebugFrameQueryPlan(
    const PresentMonTelemetryCapabilities& capabilities);

const char* PresentMonPresentModeName(std::int32_t presentMode) noexcept;
const char* PresentMonFrameTypeName(std::int32_t frameType) noexcept;

// Folds one ConsumeFrames batch into "the newest frame we have seen".
PresentMonDebugFrame FoldPresentMonDebugFrames(
    const PresentMonDebugFrameQueryPlan& plan,
    std::span<const std::uint8_t> blob, std::uint32_t frameCount,
    std::uint32_t recordSize);

class PresentMonDebugFrameTelemetry
{
public:
    bool Initialize(
        PresentMonApi2Client& client,
        const PresentMonTelemetryCapabilities& capabilities);
    void Shutdown(PresentMonApi2Client& client) noexcept;
    bool Ready() const noexcept { return ready_; }

    // Consumes queued frames for `processId` and returns the newest decoded
    // frame, or nullopt when nothing new arrived / on failure. A PID change
    // flushes the previous target.
    std::optional<PresentMonDebugFrame> ReadLatest(
        PresentMonApi2Client& client, std::uint32_t processId);

private:
    PresentMonDebugFrameQueryPlan plan_;
    PM_FRAME_QUERY_HANDLE query_{};
    std::vector<PM_QUERY_ELEMENT> queryElements_;
    std::uint32_t blobSize_{};
    std::uint32_t targetProcessId_{};
    bool ready_{};
};
}
