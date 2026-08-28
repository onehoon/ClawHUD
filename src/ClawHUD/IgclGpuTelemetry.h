#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>

#include "IgclApiCompat.h"

namespace clawhud
{
struct IgclGpuTelemetry
{
    std::optional<double> gpuUsagePercent;
    std::optional<double> gpuClockMHz;
};

std::optional<double> CalculateIgclGpuUsage(
    double previousTimestamp, double previousActivity,
    double currentTimestamp, double currentActivity) noexcept;

class IgclGpuTelemetrySampler
{
public:
    ~IgclGpuTelemetrySampler();

    bool Initialize();
    void Reset() noexcept;
    std::optional<IgclGpuTelemetry> Sample();
    bool Initialized() const noexcept { return library_ != nullptr; }
    bool InitializationAttempted() const noexcept { return initializationAttempted_; }

private:
    HMODULE library_{};
    igcl::Api api_{};
    igcl::Device device_{};
    igcl::CloseFn close_{};
    igcl::TelemetryFn telemetry_{};
    std::optional<double> previousTimestamp_;
    std::optional<double> previousActivity_;
    bool initializationAttempted_{};
};
}
