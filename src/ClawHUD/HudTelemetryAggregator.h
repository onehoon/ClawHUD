#pragma once

#include <cstdint>
#include <optional>

#include "HudModel.h"
#include "MsiEcHudTelemetry.h"

namespace clawhud
{
inline constexpr unsigned kEcTelemetryMissingThreshold = 3;
inline constexpr unsigned kSystemTelemetryMissingThreshold = 3;

// One poll's worth of PresentMon system telemetry plus the separately-read
// system memory figure. Any field may be nullopt when that metric was missing
// for this poll.
struct HudSystemTelemetryInput
{
    std::optional<double> cpuUsagePercent;
    std::optional<double> gpuUsagePercent;
    std::optional<double> gpuClockMHz;
    std::optional<std::uint64_t> gpuMemoryUsedBytes;
    std::optional<std::uint64_t> systemMemoryUsedBytes;
};

// Retains the most recent usable EC and PresentMon system telemetry for the HUD.
// A retained field is cleared only after the corresponding missing threshold of
// consecutive empty polls (see TelemetryRetention.h for the per-field rule).
class HudTelemetryAggregator
{
public:
    // Folds one fresh EC read into the retained cpuTemp / fan1 / fan2 / tdp fields.
    void IngestEc(const MsiEcHudTelemetry& fresh) noexcept;

    // Folds one poll of system telemetry into the retained system fields.
    void IngestSystem(const HudSystemTelemetryInput& input) noexcept;

    void ResetEc() noexcept;
    void ResetSystem() noexcept;
    void Reset() noexcept
    {
        ResetEc();
        ResetSystem();
    }

    // Copies the nine fields this owns (EC cpu temp / tdp / fans and the four
    // system metrics plus system memory) into `snapshot`.
    void FillSnapshot(HudTelemetrySnapshot& snapshot) const;

    const MsiEcHudTelemetry& Ec() const noexcept { return ec_; }

private:
    MsiEcHudTelemetry ec_{};
    unsigned ecCpuTempMissing_{};
    unsigned ecFan1Missing_{};
    unsigned ecFan2Missing_{};
    unsigned ecTdpMissing_{};

    std::optional<double> cpuUsagePercent_;
    std::optional<double> gpuUsagePercent_;
    std::optional<double> gpuClockMHz_;
    std::optional<std::uint64_t> gpuMemoryUsedBytes_;
    std::optional<std::uint64_t> systemMemoryUsedBytes_;
    unsigned cpuUsageMissing_{};
    unsigned gpuUsageMissing_{};
    unsigned gpuClockMissing_{};
    unsigned gpuMemoryMissing_{};
    unsigned systemMemoryMissing_{};
};
}
