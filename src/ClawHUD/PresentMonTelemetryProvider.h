#pragma once
#include "PresentMonApi2Client.h"
#include "PresentMonTelemetryTypes.h"
#include "PresentMonProcessTelemetry.h"
#include <mutex>
#include <optional>
namespace clawhud
{
PresentMonTelemetryCapabilities BuildPresentMonTelemetryCapabilities(const PM_INTROSPECTION_ROOT* root);
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
    const PresentMonTelemetryCapabilities& Capabilities() const noexcept { return capabilities_; }
    const PresentMonMetricCapability* FindMetric(PM_METRIC metric) const noexcept;
    const PresentMonDeviceCapability* FindDevice(std::uint32_t id) const noexcept;
    const PresentMonDeviceCapability* FindFirstDevice(PM_DEVICE_TYPE type, std::optional<PM_DEVICE_VENDOR> vendor = std::nullopt) const noexcept;
    bool MetricAvailable(PM_METRIC metric, std::uint32_t deviceId) const noexcept;
    bool SupportsStatistic(PM_METRIC metric, PM_STAT statistic) const noexcept;
private:
    void ShutdownUnlocked() noexcept;

    PresentMonApi2Client client_;
    PresentMonProcessTelemetry processTelemetry_;
    PresentMonTelemetryCapabilities capabilities_;
    mutable std::mutex apiMutex_;
    bool ready_{};
};
}
