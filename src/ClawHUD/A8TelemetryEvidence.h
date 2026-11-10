#pragma once

#include "PresentMonSystemTelemetry.h"
#include "PresentMonTelemetryTypes.h"
#include "MsiEcHudTelemetry.h"
#include "SupportedHardware.h"
#include "WindowsPowerTelemetry.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clawhud
{
enum class A8TelemetryEvidenceLevel
{
    Info,
    Warn,
};

struct A8TelemetryEvidenceEvent
{
    A8TelemetryEvidenceLevel level{ A8TelemetryEvidenceLevel::Info };
    std::wstring message;
};

struct A8TelemetryStartupState
{
    bool providerReady{};
    bool systemReady{};
    bool processReady{};
    std::optional<PresentMonGpuTargetStatus> gpuTargetStatus;
    std::size_t gpuTargetMatchCount{};
    std::optional<std::uint32_t> gpuDeviceId;
    HudSensorSupport ecSensors{};
    // nullopt means capability metadata was unavailable, not that the metric
    // was unbound.
    std::array<std::optional<bool>, 6> systemMetricBound{};
};

// Bounded, process-lifetime evidence for the exact A8 model. This observes
// values supplied by the existing readers; it never performs sampling itself.
class A8TelemetryEvidence
{
public:
    static bool IsEnabledForModel(std::string_view modelId) noexcept;

    bool Configure(bool enabled) noexcept;
    bool Enabled() const noexcept { return enabled_; }

    std::vector<A8TelemetryEvidenceEvent> Start(
        const A8TelemetryStartupState& startup);
    std::vector<A8TelemetryEvidenceEvent> ObserveSystem(
        const std::optional<PresentMonSystemSnapshot>& snapshot,
        bool systemPollAttempted,
        std::chrono::milliseconds systemAttemptPeriod);
    std::vector<A8TelemetryEvidenceEvent> ObserveEc(
        const MsiEcHudTelemetry& telemetry,
        std::chrono::milliseconds attemptPeriod);
    std::vector<A8TelemetryEvidenceEvent> ObserveSystemMemory(
        const std::optional<std::uint64_t>& usedBytes,
        std::chrono::milliseconds attemptPeriod);
    std::vector<A8TelemetryEvidenceEvent> ObserveBattery(
        const std::optional<WindowsPowerTelemetry>& power,
        std::chrono::milliseconds attemptPeriod);
    std::vector<A8TelemetryEvidenceEvent> ObserveFps(
        std::uint32_t processId,
        const std::optional<double>& rawDisplayedFps,
        bool processPollAttempted,
        std::chrono::milliseconds attemptPeriod);

private:
    enum class Metric : std::size_t
    {
        CpuTemp,
        FanRpm,
        CpuUsage,
        CpuClock,
        GpuUsage,
        GpuClock,
        GpuPower,
        VramUsed,
        RamUsed,
        BatteryPercent,
        AcDc,
        Fps,
        Count,
    };

    struct MetricState
    {
        std::uint64_t attempts{};
        std::chrono::milliseconds activeAttemptTime{};
        bool warningLogged{};
        bool successLogged{};
    };

    std::vector<A8TelemetryEvidenceEvent> ObserveMetric(
        Metric metric, bool valid, std::wstring value, bool attempted,
        std::chrono::milliseconds attemptPeriod, std::uint32_t processId = 0);
    static const wchar_t* MetricName(Metric metric) noexcept;
    static const wchar_t* SourceName(Metric metric) noexcept;
    std::array<MetricState, static_cast<std::size_t>(Metric::Count)> metrics_{};
    bool enabled_{};
    bool started_{};
};

void LogA8TelemetryEvidence(
    const std::vector<A8TelemetryEvidenceEvent>& events) noexcept;
}
