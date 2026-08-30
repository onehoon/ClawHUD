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
// Query semantics matching the official Intel PresentMon v2.5.1 UI. The 1000 ms
// window is the API2 statistical averaging window; it is not the HUD update
// cadence (ClawHUD keeps publishing at 500 ms).
inline constexpr double kPresentMonFpsWindowMs = 1000.0;
inline constexpr double kPresentMonFpsOffsetMs = 80.0;
inline constexpr std::uint32_t kPresentMonEtwFlushPeriodMs = 8;

struct PresentMonProcessQueryPlan
{
    std::vector<PM_QUERY_ELEMENT> elements;
    std::size_t displayedIndex{};
    std::optional<std::size_t> presentedIndex;
    std::optional<std::size_t> swapChainAddressIndex;
};

// Requires DISPLAYED_FPS + AVG (the HUD authority). PRESENTED_FPS + AVG and
// SWAP_CHAIN_ADDRESS are added to the same query only when introspection
// confirms support; their absence never blocks readiness.
std::optional<PresentMonProcessQueryPlan> BuildPresentMonProcessQueryPlan(
    const PresentMonTelemetryCapabilities& capabilities);

// Non-finite or <= 0 frame rates are treated as unavailable.
std::optional<double> DecodePresentMonFps(
    std::span<const std::uint8_t> blob, const PM_QUERY_ELEMENT& element);

std::optional<std::uint64_t> DecodePresentMonSwapChainAddress(
    std::span<const std::uint8_t> blob, const PM_QUERY_ELEMENT& element);

// Owns exactly one target-bound frame-metric dynamic query. The query and the
// process tracking are destroyed and rebuilt on every PID transition, so no
// frame-metric state ever crosses a PID boundary.
class PresentMonProcessTelemetry
{
public:
    // Validates capabilities only. The dynamic query is created lazily per
    // target inside Read().
    bool Initialize(
        PresentMonApi2Client& client,
        const PresentMonTelemetryCapabilities& capabilities);

    void Shutdown(PresentMonApi2Client& client) noexcept;

    // processId 0 releases the current target (query + tracking + state) and
    // returns nullopt without shutting down the shared session.
    std::optional<PresentMonProcessSnapshot> Read(
        PresentMonApi2Client& client, std::uint32_t processId);

    bool Ready() const noexcept { return ready_; }
    std::uint32_t TrackedProcessId() const noexcept { return trackedProcessId_; }

private:
    bool RetargetProcess(PresentMonApi2Client& client, std::uint32_t processId);
    void ReleaseTarget(PresentMonApi2Client& client) noexcept;

    PresentMonProcessQueryPlan plan_;
    PM_DYNAMIC_QUERY_HANDLE query_{};
    std::vector<PM_QUERY_ELEMENT> queryElements_;
    std::uint64_t blobSize_{};
    std::uint32_t trackedProcessId_{};
    bool ownsTracking_{};
    bool ready_{};
};
}
