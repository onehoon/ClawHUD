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
struct PresentMonProcessQueryPlan
{
    PM_QUERY_ELEMENT element{};
};

std::optional<PresentMonProcessQueryPlan> BuildPresentMonProcessQueryPlan(
    const PresentMonTelemetryCapabilities& capabilities);

std::optional<double> DecodePresentMonDisplayedFps(
    std::span<const std::uint8_t> blob, const PM_QUERY_ELEMENT& element);

std::optional<double> SelectPresentMonDisplayedFps(
    std::span<const std::optional<double>> values) noexcept;

class PresentMonProcessTelemetry
{
public:
    bool Initialize(
        PresentMonApi2Client& client,
        const PresentMonTelemetryCapabilities& capabilities);

    void Shutdown(PresentMonApi2Client& client) noexcept;

    std::optional<PresentMonProcessSnapshot> Read(
        PresentMonApi2Client& client, std::uint32_t processId);

    bool Ready() const noexcept { return ready_; }
    std::uint32_t TrackedProcessId() const noexcept { return trackedProcessId_; }

private:
    bool SwitchProcess(PresentMonApi2Client& client, std::uint32_t processId);
    void ClearTracking() noexcept;

    PM_DYNAMIC_QUERY_HANDLE query_{};
    PM_QUERY_ELEMENT fpsElement_{};
    std::uint64_t blobSize_{};
    std::uint32_t trackedProcessId_{};
    bool ownsTracking_{};
    bool ready_{};
};
}
