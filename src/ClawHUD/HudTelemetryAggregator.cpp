#include "HudTelemetryAggregator.h"

#include "TelemetryRetention.h"

namespace clawhud
{
void HudTelemetryAggregator::IngestEc(const MsiEcHudTelemetry& fresh) noexcept
{
    UpdateRetainedTelemetryField(
        ec_.cpuTempC, fresh.cpuTempC, ecCpuTempMissing_, kEcTelemetryMissingThreshold);
    UpdateRetainedTelemetryField(
        ec_.fan1Rpm, fresh.fan1Rpm, ecFan1Missing_, kEcTelemetryMissingThreshold);
    UpdateRetainedTelemetryField(
        ec_.fan2Rpm, fresh.fan2Rpm, ecFan2Missing_, kEcTelemetryMissingThreshold);
    UpdateRetainedTelemetryField(
        ec_.cpuPackagePowerW, fresh.cpuPackagePowerW, ecTdpMissing_,
        kEcTelemetryMissingThreshold);
}

void HudTelemetryAggregator::IngestSystem(const HudSystemTelemetryInput& input) noexcept
{
    UpdateRetainedTelemetryField(
        cpuUsagePercent_, input.cpuUsagePercent, cpuUsageMissing_,
        kSystemTelemetryMissingThreshold);
    UpdateRetainedTelemetryField(
        gpuUsagePercent_, input.gpuUsagePercent, gpuUsageMissing_,
        kSystemTelemetryMissingThreshold);
    UpdateRetainedTelemetryField(
        gpuClockMHz_, input.gpuClockMHz, gpuClockMissing_,
        kSystemTelemetryMissingThreshold);
    UpdateRetainedTelemetryField(
        gpuMemoryUsedBytes_, input.gpuMemoryUsedBytes, gpuMemoryMissing_,
        kSystemTelemetryMissingThreshold);
    UpdateRetainedTelemetryField(
        systemMemoryUsedBytes_, input.systemMemoryUsedBytes, systemMemoryMissing_,
        kSystemTelemetryMissingThreshold);
}

void HudTelemetryAggregator::ResetEc() noexcept
{
    ec_ = {};
    ecCpuTempMissing_ = 0;
    ecFan1Missing_ = 0;
    ecFan2Missing_ = 0;
    ecTdpMissing_ = 0;
}

void HudTelemetryAggregator::ResetSystem() noexcept
{
    cpuUsagePercent_.reset();
    gpuUsagePercent_.reset();
    gpuClockMHz_.reset();
    gpuMemoryUsedBytes_.reset();
    systemMemoryUsedBytes_.reset();
    cpuUsageMissing_ = 0;
    gpuUsageMissing_ = 0;
    gpuClockMissing_ = 0;
    gpuMemoryMissing_ = 0;
    systemMemoryMissing_ = 0;
}

void HudTelemetryAggregator::FillSnapshot(HudTelemetrySnapshot& snapshot) const
{
    snapshot.cpuTemperatureC = ec_.cpuTempC;
    snapshot.cpuPackagePowerW = ec_.cpuPackagePowerW
        ? std::optional<double>(*ec_.cpuPackagePowerW) : std::nullopt;
    snapshot.fan1Rpm = ec_.fan1Rpm;
    snapshot.fan2Rpm = ec_.fan2Rpm;
    snapshot.cpuUsagePercent = cpuUsagePercent_;
    snapshot.systemMemoryUsedBytes = systemMemoryUsedBytes_;
    snapshot.gpuMemoryUsedBytes = gpuMemoryUsedBytes_;
    snapshot.gpuUsagePercent = gpuUsagePercent_;
    snapshot.gpuClockMHz = gpuClockMHz_;
}
}
