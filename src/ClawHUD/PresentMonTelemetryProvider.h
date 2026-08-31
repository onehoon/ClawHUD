#pragma once
#include "PresentMonApi2Client.h"
#include "PresentMonTelemetryTypes.h"
#include "PresentMonFrameTelemetry.h"
#include "PresentMonProcessTelemetry.h"
#include "PresentMonSystemTelemetry.h"
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
namespace clawhud
{
PresentMonTelemetryCapabilities BuildPresentMonTelemetryCapabilities(const PM_INTROSPECTION_ROOT* root);

class PresentMonTelemetryProvider;

// RAII hold on shared API2 process tracking. While a lease is alive the target
// PID stays tracked on the shared session regardless of what the FPS path does.
class PresentMonProcessLease
{
public:
    PresentMonProcessLease() = default;
    PresentMonProcessLease(PresentMonTelemetryProvider* provider, std::uint32_t processId)
        : provider_(provider), processId_(processId) {}
    PresentMonProcessLease(const PresentMonProcessLease&) = delete;
    PresentMonProcessLease& operator=(const PresentMonProcessLease&) = delete;
    PresentMonProcessLease(PresentMonProcessLease&& other) noexcept
        : provider_(std::exchange(other.provider_, nullptr)),
          processId_(std::exchange(other.processId_, 0)) {}
    PresentMonProcessLease& operator=(PresentMonProcessLease&& other) noexcept;
    ~PresentMonProcessLease();

    void Release() noexcept;
    std::uint32_t ProcessId() const noexcept { return processId_; }
    explicit operator bool() const noexcept { return provider_ != nullptr; }

private:
    PresentMonTelemetryProvider* provider_{};
    std::uint32_t processId_{};
};

class PresentMonTelemetryProvider
{
public:
    PresentMonTelemetryProvider() = default; ~PresentMonTelemetryProvider();
    PresentMonTelemetryProvider(const PresentMonTelemetryProvider&) = delete;
    PresentMonTelemetryProvider& operator=(const PresentMonTelemetryProvider&) = delete;
    bool Initialize(); void Shutdown() noexcept;
    bool Ready() const noexcept { return ready_; }
    bool ProcessReady() const noexcept { return processTelemetry_.Ready(); }
    std::optional<PresentMonProcessSnapshot> ReadProcess(std::uint32_t processId);
    bool SystemReady() const noexcept;
    std::optional<PresentMonSystemSnapshot> ReadSystem();

    // Shared process-tracking ownership. AcquireProcess keeps `processId`
    // tracked on the shared session until the returned lease is released; an
    // empty lease means tracking could not be started.
    PresentMonProcessLease AcquireProcess(std::uint32_t processId);

    // Game-render verification: true once the target PID has produced at least
    // one displayed frame (PM_METRIC_BETWEEN_DISPLAY_CHANGE > 0). nullopt on a
    // transient consume miss. A PID change resets the first-frame latch.
    std::optional<bool> PollGameRenderDisplayedFrame(std::uint32_t processId);
    bool FrameReady() const noexcept;
    const PresentMonTelemetryCapabilities& Capabilities() const noexcept { return capabilities_; }
    const PresentMonMetricCapability* FindMetric(PM_METRIC metric) const noexcept;
    const PresentMonDeviceCapability* FindDevice(std::uint32_t id) const noexcept;
    const PresentMonDeviceCapability* FindFirstDevice(PM_DEVICE_TYPE type, std::optional<PM_DEVICE_VENDOR> vendor = std::nullopt) const noexcept;
    bool MetricAvailable(PM_METRIC metric, std::uint32_t deviceId) const noexcept;
    bool SupportsStatistic(PM_METRIC metric, PM_STAT statistic) const noexcept;
private:
    friend class PresentMonProcessLease;
    void ShutdownUnlocked() noexcept;
    void ReleaseProcess(std::uint32_t processId) noexcept;

    PresentMonApi2Client client_;
    PresentMonProcessTelemetry processTelemetry_;
    PresentMonSystemTelemetry systemTelemetry_;
    PresentMonFrameTelemetry frameTelemetry_;
    PresentMonTelemetryCapabilities capabilities_;
    mutable std::mutex apiMutex_;
    bool ready_{};
};
}
