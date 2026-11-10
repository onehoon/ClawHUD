#include "A8TelemetryEvidence.h"
#include "RuntimeLogger.h"

#include <windows.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using clawhud::A8TelemetryEvidence;
using clawhud::A8TelemetryEvidenceEvent;
using clawhud::A8TelemetryEvidenceLevel;

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool Contains(const std::vector<A8TelemetryEvidenceEvent>& events,
    const std::wstring& text,
    std::optional<A8TelemetryEvidenceLevel> level = std::nullopt)
{
    return std::any_of(events.begin(), events.end(), [&](const auto& event)
    {
        return event.message.find(text) != std::wstring::npos &&
            (!level || event.level == *level);
    });
}

std::size_t Count(const std::vector<A8TelemetryEvidenceEvent>& events,
    const std::wstring& text)
{
    return static_cast<std::size_t>(std::count_if(events.begin(), events.end(),
        [&](const auto& event)
        {
            return event.message.find(text) != std::wstring::npos;
        }));
}

clawhud::A8TelemetryStartupState ReadyStartup()
{
    clawhud::A8TelemetryStartupState startup;
    startup.providerReady = true;
    startup.systemReady = true;
    startup.processReady = true;
    startup.gpuTargetStatus = clawhud::PresentMonGpuTargetStatus::Selected;
    startup.gpuTargetMatchCount = 1;
    startup.gpuDeviceId = 7;
    startup.systemMetricBound.fill(true);
    return startup;
}

A8TelemetryEvidence StartedEvidence(
    const clawhud::A8TelemetryStartupState& startup = ReadyStartup())
{
    A8TelemetryEvidence evidence;
    Require(evidence.Configure(true), "A8 evidence configuration failed");
    (void)evidence.Start(startup);
    return evidence;
}

void TestEcMetricConfigurationAndIndependentFirstResults()
{
    auto startup = ReadyStartup();
    startup.ecSensors = { true, true, false, false };
    clawhud::A8TelemetryEvidence configuredEvidence;
    Require(configuredEvidence.Configure(true), "A8 evidence configuration failed");
    const auto startupEvents = configuredEvidence.Start(startup);
    Require(Contains(startupEvents,
            L"ec=enabled ecMetrics=cpuTemp:enabled,fanRpm:enabled,cpuPackagePower:disabled,batteryDischarge:disabled") &&
        !Contains(startupEvents, L"ecMetrics=unsupported"),
        "startup EC summary must reflect configured A8 capabilities without claiming unsupported sensors");

    clawhud::MsiEcHudTelemetry sample;
    sample.cpuTemperatureAttempted = true;
    sample.cpuTempC = 58;
    const auto temperature = configuredEvidence.ObserveEc(sample,
        std::chrono::seconds(1));
    Require(Contains(temperature,
            L"metric=cpuTemp source=MSI_EC result=first-success value=58C",
            A8TelemetryEvidenceLevel::Info) && !Contains(temperature, L"fanRpm"),
        "CPU temperature first-success must be independent of the fan getter");

    sample.cpuTemperatureAttempted = false;
    sample.cpuTempC.reset();
    sample.fanRpmAttempted = true;
    sample.fan1Rpm = 0;
    const auto fan = configuredEvidence.ObserveEc(sample,
        std::chrono::seconds(1));
    Require(Contains(fan,
            L"metric=fanRpm source=MSI_EC result=first-success value=0RPM",
            A8TelemetryEvidenceLevel::Info),
        "zero RPM must be a valid independent first-success");
    Require(configuredEvidence.ObserveEc(sample,
            std::chrono::seconds(1)).empty(),
        "resolved EC metrics must not repeat their first-success logs");
}

void TestApuPowerEvidenceAndTimeouts()
{
    auto startup = ReadyStartup();
    startup.apuPowerConfigured = true;
    A8TelemetryEvidence startupEvidence;
    Require(startupEvidence.Configure(true), "A8 evidence configuration failed");
    Require(Contains(startupEvidence.Start(startup), L"apuPowerConfigured=enabled"),
        "A8 startup metadata must report configured ADLX without claiming a sample");
    auto evidence = StartedEvidence(startup);
    clawhud::PresentMonSystemSnapshot presentMon;
    presentMon.gpuPowerW = 12.0;
    const auto gpu = evidence.ObserveSystem(presentMon, true, std::chrono::seconds(1));
    const auto apu = evidence.ObserveApuPower(
        { 24, clawhud::AdlxPowerStatus::Ok }, true, std::chrono::seconds(1));
    Require(Contains(gpu, L"metric=gpuPower source=PresentMon") &&
        Contains(apu, L"metric=apuPower source=ADLX result=first-success value=24W") &&
        evidence.ObserveApuPower({ 25, clawhud::AdlxPowerStatus::Ok }, true,
            std::chrono::seconds(1)).empty(),
        "ADLX and PresentMon power evidence must be independent and first-success only");

    auto zero = StartedEvidence(startup);
    Require(Contains(zero.ObserveApuPower(
            { 0, clawhud::AdlxPowerStatus::Ok }, true, std::chrono::seconds(1)),
            L"metric=apuPower source=ADLX result=first-success value=0W"),
        "zero ADLX watts must count as a valid first result");

    const std::pair<clawhud::AdlxPowerStatus, const wchar_t*> initFailures[]{
        { clawhud::AdlxPowerStatus::RuntimeUnavailable, L"runtime-unavailable" },
        { clawhud::AdlxPowerStatus::ExportUnavailable, L"export-unavailable" },
        { clawhud::AdlxPowerStatus::InitializationFailed, L"initialization-failed" },
        { clawhud::AdlxPowerStatus::NoGpu, L"no-gpu" },
        { clawhud::AdlxPowerStatus::PerformanceUnavailable, L"performance-unavailable" },
        { clawhud::AdlxPowerStatus::MetricUnsupported, L"metric-unsupported" },
    };
    for (const auto& [status, reason] : initFailures)
    {
        auto failed = StartedEvidence(startup);
        const auto warning = failed.ObserveApuPower(
            { std::nullopt, status }, false, std::chrono::seconds(1));
        Require(Contains(warning, std::wstring(L"metric=apuPower source=ADLX result=unavailable reason=") + reason,
                A8TelemetryEvidenceLevel::Warn) &&
            failed.ObserveApuPower({ std::nullopt, status }, false,
                std::chrono::seconds(1)).empty(),
            "terminal ADLX initialization reason must warn immediately exactly once");
        Require(Contains(failed.ObserveApuPower(
                { 24, clawhud::AdlxPowerStatus::Ok }, true, std::chrono::seconds(1)),
                L"metric=apuPower source=ADLX result=first-success value=24W"),
            "a late ADLX sample must still report after an initialization warning");
    }

    auto missing = StartedEvidence(startup);
    for (int i = 0; i < 50; ++i)
        Require(missing.ObserveApuPower({ std::nullopt,
                clawhud::AdlxPowerStatus::SampleFailed }, false,
                std::chrono::seconds(1)).empty(),
            "skipped ADLX reads must not count toward the timeout");
    for (int i = 0; i < 29; ++i)
        Require(missing.ObserveApuPower({ std::nullopt,
                clawhud::AdlxPowerStatus::SampleFailed }, true,
                std::chrono::seconds(1)).empty(),
            "ADLX timeout must wait for 30 attempted reads");
    Require(Contains(missing.ObserveApuPower({ std::nullopt,
            clawhud::AdlxPowerStatus::InvalidValue }, true, std::chrono::seconds(1)),
            L"metric=apuPower source=ADLX result=unavailable reason=no-valid-sample attempts=30",
            A8TelemetryEvidenceLevel::Warn),
        "30 actual unsuccessful ADLX reads must produce one bounded warning");
}

void TestEcAttemptTimeoutsIgnoreSkippedGettersAndRecover()
{
    auto evidence = StartedEvidence();
    clawhud::MsiEcHudTelemetry sample;
    sample.cpuTemperatureAttempted = true;
    sample.fanRpmAttempted = false;
    for (int attempt = 0; attempt < 29; ++attempt)
    {
        const auto events = evidence.ObserveEc(sample,
            std::chrono::seconds(1));
        Require(!Contains(events, L"result=unavailable"),
            "EC timeout warning occurred before 30 seconds of actual attempts");
    }
    const auto timeout = evidence.ObserveEc(sample, std::chrono::seconds(1));
    Require(Contains(timeout,
            L"metric=cpuTemp source=MSI_EC result=unavailable reason=no-valid-sample attempts=30",
            A8TelemetryEvidenceLevel::Warn) && !Contains(timeout, L"fanRpm"),
        "skipped fan getter must not advance its timeout while temperature warns after 30 attempts");
    Require(evidence.ObserveEc(sample, std::chrono::seconds(1)).empty(),
        "EC timeout warnings must be one-time");

    sample.cpuTemperatureAttempted = false;
    sample.fanRpmAttempted = true;
    std::vector<A8TelemetryEvidenceEvent> fanTimeout;
    for (int attempt = 0; attempt < 30; ++attempt)
    {
        const auto events = evidence.ObserveEc(sample,
            std::chrono::seconds(1));
        if (attempt == 29)
            fanTimeout = events;
        if (attempt < 29)
            Require(!Contains(events, L"metric=fanRpm"),
                "fan warning occurred before 30 actual getter attempts");
    }
    Require(Contains(fanTimeout,
            L"metric=fanRpm source=MSI_EC result=unavailable reason=no-valid-sample attempts=30",
            A8TelemetryEvidenceLevel::Warn),
        "independent fan attempts must receive their own bounded timeout");
    sample.fan1Rpm = 3200;
    Require(Contains(evidence.ObserveEc(sample, std::chrono::seconds(1)),
            L"metric=fanRpm source=MSI_EC result=first-success value=3200RPM",
            A8TelemetryEvidenceLevel::Info),
        "late EC first-success must remain observable after its timeout warning");
}

void TestExactModelGateAndStartupReasons()
{
    Require(A8TelemetryEvidence::IsEnabledForModel("msi.claw.bz2em"),
        "exact admitted A8 model did not enable evidence");
    Require(!A8TelemetryEvidence::IsEnabledForModel("msi.claw.a2vm.7") &&
        !A8TelemetryEvidence::IsEnabledForModel("msi.claw.a2vm.8") &&
        !A8TelemetryEvidence::IsEnabledForModel("msi.claw.cg3em") &&
        !A8TelemetryEvidence::IsEnabledForModel("amd.synthetic") &&
        !A8TelemetryEvidence::IsEnabledForModel("unknown"),
        "Intel, other AMD and unknown models must not enable A8 evidence");

    A8TelemetryEvidence disabled;
    Require(disabled.Start(ReadyStartup()).empty(),
        "evidence must be disabled by default");

    A8TelemetryEvidence providerUnavailable;
    Require(providerUnavailable.Configure(true), "configuration failed");
    const auto unavailable = providerUnavailable.Start({});
    Require(Contains(unavailable, L"reason=provider-unavailable",
            A8TelemetryEvidenceLevel::Warn) &&
        !Contains(unavailable, L"reason=not-bound"),
        "failed provider must not be misreported as six unbound metrics");

    auto noGpu = ReadyStartup();
    noGpu.gpuTargetStatus = clawhud::PresentMonGpuTargetStatus::NoMatch;
    noGpu.gpuTargetMatchCount = 0;
    noGpu.gpuDeviceId.reset();
    noGpu.systemMetricBound[2] = false;
    noGpu.systemMetricBound[3] = false;
    noGpu.systemMetricBound[4] = false;
    noGpu.systemMetricBound[5] = false;
    A8TelemetryEvidence noGpuEvidence;
    (void)noGpuEvidence.Configure(true);
    const auto noGpuEvents = noGpuEvidence.Start(noGpu);
    Require(Contains(noGpuEvents, L"gpuTarget=gpu-no-match gpuMatches=0") &&
        Contains(noGpuEvents, L"gpuDeviceId=n/a") &&
        Contains(noGpuEvents, L"metric=gpuUsage source=PresentMon result=unavailable reason=gpu-no-match",
            A8TelemetryEvidenceLevel::Warn),
        "missing AMD adapter reason was not preserved");
    noGpu.gpuTargetStatus = clawhud::PresentMonGpuTargetStatus::Ambiguous;
    noGpu.gpuTargetMatchCount = 2;
    A8TelemetryEvidence ambiguousEvidence;
    (void)ambiguousEvidence.Configure(true);
    const auto ambiguousEvents = ambiguousEvidence.Start(noGpu);
    Require(Contains(ambiguousEvents, L"gpuTarget=gpu-ambiguous gpuMatches=2") &&
        Contains(ambiguousEvents, L"metric=gpuPower source=PresentMon result=unavailable reason=gpu-ambiguous"),
        "ambiguous AMD adapter reason was not preserved");
    clawhud::PresentMonSystemSnapshot lateGpu;
    lateGpu.gpuUsagePercent = 50.0;
    Require(Contains(ambiguousEvidence.ObserveSystem(lateGpu, true,
            std::chrono::seconds(1)),
            L"metric=gpuUsage source=PresentMon result=first-success value=50.00%"),
        "an immediate unavailable warning suppressed a later first-success");

    auto queryUnavailable = ReadyStartup();
    queryUnavailable.systemReady = false;
    queryUnavailable.processReady = false;
    A8TelemetryEvidence queryEvidence;
    (void)queryEvidence.Configure(true);
    const auto queryEvents = queryEvidence.Start(queryUnavailable);
    Require(Contains(queryEvents, L"reason=system-query-not-ready") &&
        Contains(queryEvents, L"reason=process-query-not-ready") &&
        Contains(queryEvents, L"metric=cpuUsage source=PresentMon result=unavailable reason=system-query-not-ready"),
        "query initialization failures were not reported truthfully");
}

void TestIndependentRawSuccessAndZeroValues()
{
    auto evidence = StartedEvidence();
    clawhud::PresentMonSystemSnapshot system;
    system.cpuUsagePercent = 0.0;
    system.cpuClockMHz = 0.0;
    system.gpuUsagePercent = 0.0;
    system.gpuClockMHz = 0.0;
    system.gpuPowerW = 0.0;
    system.gpuMemoryUsedBytes = 0;
    const auto systemEvents = evidence.ObserveSystem(system, true,
        std::chrono::seconds(1));
    Require(Count(systemEvents, L"result=first-success") == 6 &&
        Contains(systemEvents, L"metric=cpuUsage source=PresentMon result=first-success value=0.00%") &&
        Contains(systemEvents, L"metric=cpuClock source=PresentMon result=first-success value=0.00MHz") &&
        Contains(systemEvents, L"metric=gpuPower source=PresentMon result=first-success value=0.00W") &&
        Contains(systemEvents, L"metric=vramUsed source=PresentMon result=first-success value=0B"),
        "legitimate zero PresentMon values were not logged independently");

    const auto ram = evidence.ObserveSystemMemory(0, std::chrono::seconds(1));
    Require(Contains(ram, L"metric=ramUsed source=Windows result=first-success value=0B"),
        "zero-byte RAM was not logged as a successful sample");

    clawhud::WindowsPowerTelemetry power;
    power.batteryPercent = 0;
    power.onBattery = false;
    const auto battery = evidence.ObserveBattery(power, std::chrono::seconds(5));
    Require(Contains(battery, L"metric=batteryPercent source=Windows result=first-success value=0%") &&
        Contains(battery, L"metric=acDc source=Windows result=first-success value=AC"),
        "zero battery or AC state was not treated as valid");

    auto dcEvidence = StartedEvidence();
    clawhud::WindowsPowerTelemetry dcWithoutPercent;
    dcWithoutPercent.onBattery = true;
    const auto dc = dcEvidence.ObserveBattery(dcWithoutPercent,
        std::chrono::seconds(5));
    Require(Contains(dc, L"metric=acDc source=Windows result=first-success value=DC") &&
        !Contains(dc, L"metric=batteryPercent"),
        "DC state and missing battery percent were not tracked independently");
    dcWithoutPercent.batteryPercent = 0;
    const auto lateBattery = dcEvidence.ObserveBattery(dcWithoutPercent,
        std::chrono::seconds(5));
    Require(Contains(lateBattery,
            L"metric=batteryPercent source=Windows result=first-success value=0%"),
        "missing battery percent suppressed its later valid zero sample");

    Require(evidence.ObserveFps(0, 90.0, false, std::chrono::milliseconds(500)).empty(),
        "no FPS target must not produce evidence");
    const auto fps = evidence.ObserveFps(1234, 85.25, true,
        std::chrono::milliseconds(500));
    Require(Contains(fps, L"metric=fps source=PresentMon result=first-success value=85.25FPS pid=1234"),
        "raw FPS first-success evidence missing");
    Require(evidence.ObserveFps(5678, 60.0, true,
            std::chrono::milliseconds(500)).empty(),
        "target changes must not duplicate first-success evidence");
}

void TestSystemMetricTimeoutAndLateSuccess()
{
    auto evidence = StartedEvidence();
    clawhud::PresentMonSystemSnapshot partial;
    partial.cpuUsagePercent = 42.5;
    partial.cpuClockMHz = 3200.0;
    partial.gpuUsagePercent = 97.0;
    partial.gpuClockMHz = 2200.0;
    partial.gpuMemoryUsedBytes = 0;
    for (int attempt = 0; attempt < 29; ++attempt)
    {
        const auto events = evidence.ObserveSystem(partial, true,
            std::chrono::seconds(1));
        Require(!Contains(events, L"metric=gpuPower"),
            "GPU power warning occurred before 30 seconds of active attempts");
    }
    const auto timeout = evidence.ObserveSystem(partial, true,
        std::chrono::seconds(1));
    Require(Contains(timeout,
            L"metric=gpuPower source=PresentMon result=unavailable reason=no-valid-sample attempts=30",
            A8TelemetryEvidenceLevel::Warn),
        "missing GPU power did not produce one bounded timeout warning");

    partial.gpuPowerW = 13.0;
    const auto lateSuccess = evidence.ObserveSystem(partial, true,
        std::chrono::seconds(1));
    Require(Contains(lateSuccess,
            L"metric=gpuPower source=PresentMon result=first-success value=13.00W",
            A8TelemetryEvidenceLevel::Info),
        "a timeout warning suppressed later first-success evidence");
    Require(evidence.ObserveSystem(partial, true,
            std::chrono::seconds(1)).empty(),
        "resolved metrics must not log recurring samples");
}

void TestAttemptBasedTimeoutsAndInvalidValues()
{
    auto evidence = StartedEvidence();
    for (int attempt = 0; attempt < 100; ++attempt)
        Require(evidence.ObserveFps(0, std::nullopt, false,
                std::chrono::milliseconds(500)).empty(),
            "idle FPS target advanced the sampling timeout");

    clawhud::WindowsPowerTelemetry invalidPower;
    invalidPower.batteryPercent = 101;
    invalidPower.onBattery.reset();
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        const auto events = evidence.ObserveBattery(invalidPower,
            std::chrono::seconds(5));
        Require(!Contains(events, L"result=first-success"),
            "invalid battery data was reported as successful");
    }
    const auto batteryTimeout = evidence.ObserveBattery(invalidPower,
        std::chrono::seconds(5));
    Require(Contains(batteryTimeout,
            L"metric=batteryPercent source=Windows result=unavailable reason=no-valid-sample attempts=6",
            A8TelemetryEvidenceLevel::Warn) &&
        Contains(batteryTimeout,
            L"metric=acDc source=Windows result=unavailable reason=no-valid-sample attempts=6",
            A8TelemetryEvidenceLevel::Warn),
        "battery warnings must follow actual 5-second source attempts");

    for (int attempt = 0; attempt < 59; ++attempt)
    {
        const auto events = evidence.ObserveFps(1234, std::nullopt, true,
            std::chrono::milliseconds(500));
        Require(!Contains(events, L"metric=fps"),
            "FPS warning occurred before 30 seconds of target sampling");
    }
    const auto fpsTimeout = evidence.ObserveFps(1234, 0.2, true,
        std::chrono::milliseconds(500));
    Require(Contains(fpsTimeout,
            L"metric=fps source=PresentMon result=unavailable reason=no-valid-sample attempts=60 pid=1234",
            A8TelemetryEvidenceLevel::Warn),
        "zero-rounded FPS did not remain unavailable through the attempt timeout");
    const auto fpsLateSuccess = evidence.ObserveFps(1234, 0.5, true,
        std::chrono::milliseconds(500));
    Require(Contains(fpsLateSuccess,
            L"metric=fps source=PresentMon result=first-success value=0.50FPS pid=1234"),
        "FPS first-success did not recover after a missing-sample warning");

    auto invalidEvidence = StartedEvidence();
    clawhud::PresentMonSystemSnapshot invalid;
    invalid.cpuUsagePercent = std::numeric_limits<double>::quiet_NaN();
    invalid.cpuClockMHz = std::numeric_limits<double>::infinity();
    invalid.gpuUsagePercent = 101.0;
    invalid.gpuClockMHz = -1.0;
    invalid.gpuPowerW = -0.1;
    const auto invalidEvents = invalidEvidence.ObserveSystem(invalid, true,
        std::chrono::seconds(1));
    Require(!Contains(invalidEvents, L"result=first-success"),
        "nonfinite, negative or out-of-range metrics were reported as successful");
    Require(invalidEvidence.ObserveFps(0, 80.0, false,
            std::chrono::milliseconds(500)).empty(),
        "a raw value without a target must remain ignored");
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), {} };
}

void TestInfoAndWarnReachNormalLogWithDebugDisabled()
{
    const auto directory = std::filesystem::temp_directory_path() /
        (L"ClawHUD.A8TelemetryEvidenceTests." +
            std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    clawhud::RuntimeLogger::SetDirectoryForTests(directory.wstring());
    clawhud::RuntimeLogger::SetDebugLogging(false);

    auto startup = ReadyStartup();
    startup.ecSensors = { true, true, false, false };
    startup.apuPowerConfigured = true;
    startup.systemMetricBound[4] = false;
    A8TelemetryEvidence evidence;
    Require(evidence.Configure(true), "A8 evidence configuration failed");
    clawhud::LogA8TelemetryEvidence(evidence.Start(startup));
    clawhud::PresentMonSystemSnapshot system;
    system.cpuUsagePercent = 42.5;
    clawhud::LogA8TelemetryEvidence(evidence.ObserveSystem(system, true,
        std::chrono::seconds(1)));
    clawhud::MsiEcHudTelemetry ecSample;
    ecSample.cpuTempC = 58;
    ecSample.cpuTemperatureAttempted = true;
    ecSample.fanRpmAttempted = true;
    clawhud::LogA8TelemetryEvidence(evidence.ObserveEc(ecSample,
        std::chrono::seconds(1)));
    clawhud::LogA8TelemetryEvidence(evidence.ObserveApuPower(
        { 0, clawhud::AdlxPowerStatus::Ok }, true, std::chrono::seconds(1)));
    for (int attempt = 0; attempt < 30; ++attempt)
        clawhud::LogA8TelemetryEvidence(evidence.ObserveEc(ecSample,
            std::chrono::seconds(1)));
    A8TelemetryEvidence failedApuEvidence;
    (void)failedApuEvidence.Configure(true);
    clawhud::LogA8TelemetryEvidence(failedApuEvidence.Start(startup));
    clawhud::LogA8TelemetryEvidence(failedApuEvidence.ObserveApuPower(
        { std::nullopt, clawhud::AdlxPowerStatus::MetricUnsupported }, false,
        std::chrono::seconds(1)));
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Debug,
        L"debug-only-entry");

    const auto log = ReadFile(directory / L"clawhud.log");
    Require(log.find("[INFO] [A8Telemetry] model=msi.claw.bz2em") != std::string::npos &&
        log.find("[INFO] [A8Telemetry] metric=cpuUsage source=PresentMon result=first-success") != std::string::npos &&
        log.find("[INFO] [A8Telemetry] metric=cpuTemp source=MSI_EC result=first-success value=58C") != std::string::npos &&
        log.find("[WARN] [A8Telemetry] metric=fanRpm source=MSI_EC result=unavailable reason=no-valid-sample attempts=30") != std::string::npos &&
        log.find("[WARN] [A8Telemetry] metric=gpuPower source=PresentMon result=unavailable reason=not-bound") != std::string::npos,
        "A8 INFO/WARN evidence did not reach the ordinary log with Debug disabled");
    Require(log.find("apuPowerConfigured=enabled") != std::string::npos &&
        log.find("[INFO] [A8Telemetry] metric=apuPower source=ADLX result=first-success value=0W") != std::string::npos &&
        log.find("[WARN] [A8Telemetry] metric=apuPower source=ADLX result=unavailable reason=metric-unsupported") != std::string::npos,
        "ADLX startup, zero success and failure warning must reach the ordinary log");
    Require(log.find("debug-only-entry") == std::string::npos,
        "A8 evidence changed the existing Debug log filter");

    clawhud::RuntimeLogger::ResetForTests();
    std::filesystem::remove_all(directory);
}
}

int main()
{
    TestEcMetricConfigurationAndIndependentFirstResults();
    TestApuPowerEvidenceAndTimeouts();
    TestEcAttemptTimeoutsIgnoreSkippedGettersAndRecover();
    TestExactModelGateAndStartupReasons();
    TestIndependentRawSuccessAndZeroValues();
    TestSystemMetricTimeoutAndLateSuccess();
    TestAttemptBasedTimeoutsAndInvalidValues();
    TestInfoAndWarnReachNormalLogWithDebugDisabled();
}
