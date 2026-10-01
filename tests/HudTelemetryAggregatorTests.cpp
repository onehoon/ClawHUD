#include "HudTelemetryAggregator.h"

#include <iostream>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

MsiEcHudTelemetry Ec(std::optional<int> cpuTemp, std::optional<int> fan1,
    std::optional<int> fan2, std::optional<int> tdp)
{
    MsiEcHudTelemetry ec;
    ec.cpuTempC = cpuTemp;
    ec.fan1Rpm = fan1;
    ec.fan2Rpm = fan2;
    ec.cpuPackagePowerW = tdp;
    return ec;
}

HudSystemTelemetryInput Sys(std::optional<double> cpu, std::optional<double> gpu,
    std::optional<double> gpuClock, std::optional<std::uint64_t> gpuMem,
    std::optional<std::uint64_t> sysMem,
    std::optional<double> cpuClock = std::nullopt)
{
    return HudSystemTelemetryInput{cpu, cpuClock, gpu, gpuClock, gpuMem, sysMem};
}

HudTelemetrySnapshot SnapshotOf(const HudTelemetryAggregator& agg)
{
    HudTelemetrySnapshot s{};
    agg.FillSnapshot(s);
    return s;
}

void FreshEcValuesAppearInSnapshot(bool& ok)
{
    HudTelemetryAggregator agg;
    agg.IngestEc(Ec(64, 2400, 2600, 35));
    const auto s = SnapshotOf(agg);
    ok &= Check(s.cpuTemperatureC == 64, "cpu temp mapped");
    ok &= Check(s.fan1Rpm == 2400 && s.fan2Rpm == 2600, "fans mapped");
    ok &= Check(s.cpuPackagePowerW.has_value() && *s.cpuPackagePowerW == 35.0,
        "tdp mapped to double");
}

void EcFieldRetainedForTwoMissesThenCleared(bool& ok)
{
    HudTelemetryAggregator agg;
    agg.IngestEc(Ec(64, 2400, 2600, 35));
    agg.IngestEc(Ec(std::nullopt, std::nullopt, std::nullopt, std::nullopt)); // miss 1
    ok &= Check(SnapshotOf(agg).cpuTemperatureC == 64, "retained after 1 miss");
    agg.IngestEc(Ec(std::nullopt, std::nullopt, std::nullopt, std::nullopt)); // miss 2
    ok &= Check(SnapshotOf(agg).cpuTemperatureC == 64, "retained after 2 misses");
    agg.IngestEc(Ec(std::nullopt, std::nullopt, std::nullopt, std::nullopt)); // miss 3
    ok &= Check(!SnapshotOf(agg).cpuTemperatureC, "cleared on the 3rd consecutive miss");
    ok &= Check(!SnapshotOf(agg).fan1Rpm && !SnapshotOf(agg).cpuPackagePowerW,
        "all EC fields cleared together");
}

void FreshEcValueResetsTheMissStreak(bool& ok)
{
    HudTelemetryAggregator agg;
    agg.IngestEc(Ec(64, 2400, 2600, 35));
    agg.IngestEc(Ec(std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    agg.IngestEc(Ec(std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    agg.IngestEc(Ec(70, 2500, 2700, 40)); // fresh: streak resets
    agg.IngestEc(Ec(std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    agg.IngestEc(Ec(std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    ok &= Check(SnapshotOf(agg).cpuTemperatureC == 70,
        "a fresh value between misses keeps the field alive");
}

void SystemFieldRetentionMatchesThreshold(bool& ok)
{
    HudTelemetryAggregator agg;
    agg.IngestSystem(Sys(40.0, 55.0, 1800.0, 4000u, 9000u, 4200.0));
    agg.IngestSystem(Sys(std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    agg.IngestSystem(Sys(std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    auto s = SnapshotOf(agg);
    ok &= Check(s.cpuUsagePercent == 40.0 && s.gpuUsagePercent == 55.0 &&
        s.cpuClockMHz == 4200.0 && s.gpuClockMHz == 1800.0 &&
        s.gpuMemoryUsedBytes == 4000u &&
        s.systemMemoryUsedBytes == 9000u, "system fields retained through 2 misses");
    agg.IngestSystem(Sys(std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    s = SnapshotOf(agg);
    ok &= Check(!s.cpuUsagePercent && !s.cpuClockMHz && !s.gpuUsagePercent && !s.gpuClockMHz &&
        !s.gpuMemoryUsedBytes && !s.systemMemoryUsedBytes,
        "system fields cleared on the 3rd consecutive miss");
}

void PresentMonMappingAndCpuClockRetentionAreIndependent(bool& ok)
{
    PresentMonSystemSnapshot presentMon{};
    presentMon.cpuUsagePercent = 42.0;
    presentMon.cpuClockMHz = 4200.0;
    presentMon.gpuUsagePercent = 80.0;
    presentMon.gpuClockMHz = 1800.0;
    presentMon.gpuMemoryUsedBytes = 4000u;
    auto input = MakeHudSystemTelemetryInput(presentMon, 9000u);
    ok &= Check(input.cpuUsagePercent == 42.0 && input.cpuClockMHz == 4200.0 &&
        input.gpuUsagePercent == 80.0 && input.gpuClockMHz == 1800.0 &&
        input.gpuMemoryUsedBytes == 4000u && input.systemMemoryUsedBytes == 9000u,
        "production system snapshot mapping preserves CPU clock and existing fields");

    HudTelemetryAggregator agg;
    agg.IngestEc(Ec(67, std::nullopt, std::nullopt, std::nullopt));
    agg.IngestSystem(input);
    input.cpuClockMHz.reset();
    input.cpuUsagePercent = 43.0;
    agg.IngestSystem(input);
    agg.IngestSystem(input);
    auto s = SnapshotOf(agg);
    ok &= Check(s.cpuClockMHz == 4200.0 && s.cpuUsagePercent == 43.0 &&
        s.cpuTemperatureC == 67, "CPU clock retained for two misses without affecting usage or temperature");
    agg.IngestSystem(input);
    s = SnapshotOf(agg);
    ok &= Check(!s.cpuClockMHz && s.cpuUsagePercent == 43.0 &&
        s.cpuTemperatureC == 67, "third CPU clock miss clears only the clock");

    input = MakeHudSystemTelemetryInput(std::nullopt, 9000u);
    ok &= Check(!input.cpuClockMHz && !input.cpuUsagePercent &&
        input.systemMemoryUsedBytes == 9000u,
        "missing PresentMon snapshot leaves independent system memory usable");
}

void PartialSystemPollUpdatesOnlyPresentFields(bool& ok)
{
    HudTelemetryAggregator agg;
    agg.IngestSystem(Sys(40.0, 55.0, 1800.0, 4000u, 9000u, 4200.0));
    // Two polls where only cpu is present: gpu clock etc. accrue misses.
    agg.IngestSystem(Sys(41.0, std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    agg.IngestSystem(Sys(42.0, std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    agg.IngestSystem(Sys(43.0, std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    const auto s = SnapshotOf(agg);
    ok &= Check(s.cpuUsagePercent == 43.0, "cpu keeps updating from fresh values");
    ok &= Check(!s.gpuUsagePercent, "gpu usage cleared after 3 misses while cpu stayed fresh");
}

void FillSnapshotLeavesUnownedFieldsAlone(bool& ok)
{
    HudTelemetryAggregator agg;
    agg.IngestEc(Ec(64, 2400, 2600, 35));
    HudTelemetrySnapshot s{};
    s.presentMonDisplayedFps = 120.0;
    s.batteryPercent = 88;
    s.onBattery = true;
    agg.FillSnapshot(s);
    ok &= Check(s.presentMonDisplayedFps == 120.0, "fps untouched");
    ok &= Check(s.batteryPercent == 88 && s.onBattery, "battery fields untouched");
    ok &= Check(s.cpuTemperatureC == 64, "owned field still filled");
}

void ResetScopes(bool& ok)
{
    HudTelemetryAggregator agg;
    agg.IngestEc(Ec(64, 2400, 2600, 35));
    agg.IngestSystem(Sys(40.0, 55.0, 1800.0, 4000u, 9000u));

    agg.ResetSystem();
    auto s = SnapshotOf(agg);
    ok &= Check(s.cpuTemperatureC == 64, "ResetSystem keeps EC");
    ok &= Check(!s.cpuUsagePercent && !s.cpuClockMHz, "ResetSystem clears system");

    agg.IngestSystem(Sys(40.0, 55.0, 1800.0, 4000u, 9000u, 4200.0));
    agg.ResetEc();
    s = SnapshotOf(agg);
    ok &= Check(!s.cpuTemperatureC, "ResetEc clears EC");
    ok &= Check(s.cpuUsagePercent == 40.0 && s.cpuClockMHz == 4200.0,
        "ResetEc keeps system");

    agg.IngestEc(Ec(64, 2400, 2600, 35));
    agg.Reset();
    s = SnapshotOf(agg);
    ok &= Check(!s.cpuTemperatureC && !s.cpuUsagePercent, "Reset clears both");
}

void ResetClearsTheMissStreak(bool& ok)
{
    HudTelemetryAggregator agg;
    agg.IngestEc(Ec(64, 2400, 2600, 35));
    agg.IngestSystem(Sys(30.0, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt, 4200.0));
    agg.IngestEc(Ec(std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    agg.IngestEc(Ec(std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    agg.IngestSystem(Sys(31.0, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt));
    agg.IngestSystem(Sys(32.0, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt));
    agg.Reset();
    agg.IngestEc(Ec(70, 2500, 2700, 40));
    agg.IngestSystem(Sys(33.0, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt, 4300.0));
    agg.IngestEc(Ec(std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    agg.IngestEc(Ec(std::nullopt, std::nullopt, std::nullopt, std::nullopt));
    agg.IngestSystem(Sys(34.0, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt));
    agg.IngestSystem(Sys(35.0, std::nullopt, std::nullopt,
        std::nullopt, std::nullopt));
    const auto s = SnapshotOf(agg);
    ok &= Check(s.cpuTemperatureC == 70 && s.cpuClockMHz == 4300.0,
        "EC and CPU clock miss counters restart from zero after Reset");
}
}

int main()
{
    bool ok = true;
    FreshEcValuesAppearInSnapshot(ok);
    EcFieldRetainedForTwoMissesThenCleared(ok);
    FreshEcValueResetsTheMissStreak(ok);
    SystemFieldRetentionMatchesThreshold(ok);
    PresentMonMappingAndCpuClockRetentionAreIndependent(ok);
    PartialSystemPollUpdatesOnlyPresentFields(ok);
    FillSnapshotLeavesUnownedFieldsAlone(ok);
    ResetScopes(ok);
    ResetClearsTheMissStreak(ok);
    return ok ? 0 : 1;
}
