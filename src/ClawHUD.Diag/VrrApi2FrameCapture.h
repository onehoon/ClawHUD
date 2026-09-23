#pragma once

#include "DiagPresentMonApi2Client.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

struct VrrFrameSample
{
    std::uint32_t processId{};
    std::uint64_t swapChainAddress{};
    std::optional<double> betweenDisplayChangeMs;
    std::int32_t presentMode{};
    std::optional<std::uint64_t> presentStartQpc;
    std::optional<std::int32_t> presentRuntime;
    std::optional<std::int32_t> frameType;
    std::optional<bool> allowsTearing;
    std::optional<bool> dropped;
    std::optional<std::int32_t> syncInterval;
    std::optional<std::uint32_t> presentFlags;
    std::optional<double> betweenPresentsMs;
    std::optional<double> displayedTimeMs;
    std::optional<double> untilDisplayedMs;
    std::optional<double> displayLatencyMs;
    std::optional<double> renderPresentLatencyMs;
    std::optional<double> flipDelayMs;
};

enum class VrrFrameField
{
    ProcessId,
    SwapChainAddress,
    PresentStartQpc,
    PresentMode,
    PresentRuntime,
    FrameType,
    AllowsTearing,
    Dropped,
    SyncInterval,
    PresentFlags,
    BetweenPresents,
    BetweenDisplayChange,
    DisplayedTime,
    UntilDisplayed,
    DisplayLatency,
    RenderPresentLatency,
    FlipDelay,
};

struct VrrFrameMetricBinding
{
    VrrFrameField field{};
    PM_DATA_TYPE dataType{PM_DATA_TYPE_VOID};
    PM_ENUM enumId{PM_ENUM_NULL_ENUM};
    bool required{};
    PM_QUERY_ELEMENT element{};
};

struct VrrFrameQueryPlan
{
    std::vector<VrrFrameMetricBinding> bindings;
};

bool VrrPresentMonApiVersionMatches(const PM_VERSION& version) noexcept;
std::optional<VrrFrameQueryPlan> BuildVrrFrameQueryPlan(
    const PM_INTROSPECTION_ROOT* root);
std::optional<VrrFrameSample> DecodeVrrFrameSample(
    std::span<const std::uint8_t> record, std::uint32_t targetProcessId,
    const VrrFrameQueryPlan& plan);

class VrrApi2FrameCapture
{
public:
    ~VrrApi2FrameCapture();

    bool Initialize();
    bool StartTracking(std::uint32_t processId);
    bool DrainFrames();
    void StopTracking() noexcept;
    void Shutdown() noexcept;

    bool Ready() const noexcept { return ready_; }
    std::uint32_t BlobSize() const noexcept { return blobSize_; }
    const std::vector<VrrFrameSample>& Samples() const noexcept { return samples_; }

private:
    DiagPresentMonApi2Client client_;
    VrrFrameQueryPlan plan_;
    PM_FRAME_QUERY_HANDLE query_{};
    std::uint32_t blobSize_{};
    std::uint32_t trackedProcessId_{};
    std::vector<VrrFrameSample> samples_;
    bool clientInitialized_{};
    bool ready_{};
};
