#include "A8TelemetryEvidence.h"

#include "RuntimeLogger.h"

#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace clawhud
{
namespace
{
constexpr auto kMissingSampleWarningAfter = std::chrono::seconds(30);

std::wstring Decimal(double value)
{
    std::wostringstream text;
    text << std::fixed << std::setprecision(2) << value;
    return text.str();
}

bool ValidRepresentableNonnegative(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0 &&
        value < static_cast<double>(std::numeric_limits<long>::max());
}

std::wstring GpuTargetName(PresentMonGpuTargetStatus status)
{
    switch (status)
    {
    case PresentMonGpuTargetStatus::Selected: return L"selected";
    case PresentMonGpuTargetStatus::UnknownVendor: return L"unknown-vendor";
    case PresentMonGpuTargetStatus::NoMatch: return L"gpu-no-match";
    case PresentMonGpuTargetStatus::Ambiguous: return L"gpu-ambiguous";
    }
    return L"unknown";
}

std::wstring BindingName(const std::optional<bool>& bound)
{
    if (!bound)
        return L"unknown";
    return *bound ? L"bound" : L"not-bound";
}

void Append(std::vector<A8TelemetryEvidenceEvent>& destination,
    std::vector<A8TelemetryEvidenceEvent> source)
{
    destination.insert(destination.end(),
        std::make_move_iterator(source.begin()),
        std::make_move_iterator(source.end()));
}
}

bool A8TelemetryEvidence::IsEnabledForModel(std::string_view modelId) noexcept
{
    return modelId == "msi.claw.bz2em";
}

bool A8TelemetryEvidence::Configure(bool enabled) noexcept
{
    if (started_)
        return false;
    enabled_ = enabled;
    return true;
}

std::vector<A8TelemetryEvidenceEvent> A8TelemetryEvidence::Start(
    const A8TelemetryStartupState& startup)
{
    if (!enabled_ || started_)
        return {};
    started_ = true;

    std::vector<A8TelemetryEvidenceEvent> events;
    events.push_back({ A8TelemetryEvidenceLevel::Info,
        L"[A8Telemetry] model=msi.claw.bz2em gpuVendor=AMD ec=disabled "
        L"ecMetrics=unsupported(cpuTemp,fanRpm,cpuPackagePower,batteryEstimate) "
        L"intelVrrFix=unsupported" });

    const auto gpuTarget = startup.providerReady && startup.gpuTargetStatus
        ? GpuTargetName(*startup.gpuTargetStatus) :
            (startup.providerReady ? std::wstring(L"unknown")
                : std::wstring(L"provider-unavailable"));
    const auto gpuMatches = startup.providerReady
        ? std::to_wstring(startup.gpuTargetMatchCount) : std::wstring(L"unknown");
    const auto gpuDeviceId = startup.providerReady
        ? (startup.gpuDeviceId ? std::to_wstring(*startup.gpuDeviceId)
            : std::wstring(L"n/a"))
        : std::wstring(L"unknown");
    const auto boundAt = [&](std::size_t index)
    {
        return startup.providerReady ? BindingName(startup.systemMetricBound[index])
            : std::wstring(L"unknown");
    };
    events.push_back({ A8TelemetryEvidenceLevel::Info,
        L"[A8Telemetry] source=PresentMon providerReady=" +
        std::to_wstring(startup.providerReady) + L" systemReady=" +
        std::to_wstring(startup.systemReady) + L" processReady=" +
        std::to_wstring(startup.processReady) + L" gpuTarget=" + gpuTarget +
        L" gpuMatches=" + gpuMatches + L" gpuDeviceId=" + gpuDeviceId +
        L" cpuUsage=" + boundAt(0) + L" cpuClock=" + boundAt(1) +
        L" gpuUsage=" + boundAt(2) + L" gpuClock=" + boundAt(3) +
        L" gpuPower=" + boundAt(4) + L" vramUsed=" + boundAt(5) });

    const auto warn = [&](Metric metric, std::wstring_view reason)
    {
        auto& state = metrics_[static_cast<std::size_t>(metric)];
        if (state.warningLogged)
            return;
        state.warningLogged = true;
        events.push_back({ A8TelemetryEvidenceLevel::Warn,
            std::wstring(L"[A8Telemetry] metric=") + MetricName(metric) +
            L" source=" + SourceName(metric) + L" result=unavailable reason=" +
            std::wstring(reason) });
    };

    if (!startup.providerReady)
    {
        events.push_back({ A8TelemetryEvidenceLevel::Warn,
            L"[A8Telemetry] source=PresentMon result=unavailable "
            L"reason=provider-unavailable" });
        for (const auto metric : { Metric::CpuUsage, Metric::CpuClock,
                 Metric::GpuUsage, Metric::GpuClock, Metric::GpuPower,
                 Metric::VramUsed })
            warn(metric, L"provider-unavailable");
        warn(Metric::Fps, L"provider-unavailable");
        return events;
    }

    if (!startup.systemReady)
        events.push_back({ A8TelemetryEvidenceLevel::Warn,
            L"[A8Telemetry] source=PresentMon result=unavailable "
            L"reason=system-query-not-ready" });
    if (!startup.processReady)
        events.push_back({ A8TelemetryEvidenceLevel::Warn,
            L"[A8Telemetry] source=PresentMon result=unavailable "
            L"reason=process-query-not-ready" });

    constexpr std::array<Metric, 6> systemMetrics{
        Metric::CpuUsage, Metric::CpuClock, Metric::GpuUsage,
        Metric::GpuClock, Metric::GpuPower, Metric::VramUsed };
    for (std::size_t index = 0; index < systemMetrics.size(); ++index)
    {
        const auto metric = systemMetrics[index];
        if (!startup.systemMetricBound[index])
        {
            warn(metric, L"capability-metadata-unavailable");
            continue;
        }
        if (!*startup.systemMetricBound[index])
        {
            const bool gpuMetric = index >= 2;
            const auto reason = gpuMetric && startup.gpuTargetStatus &&
                    *startup.gpuTargetStatus != PresentMonGpuTargetStatus::Selected
                ? GpuTargetName(*startup.gpuTargetStatus) : std::wstring(L"not-bound");
            warn(metric, reason);
        }
        else if (!startup.systemReady)
            warn(metric, L"system-query-not-ready");
    }
    if (!startup.processReady)
        warn(Metric::Fps, L"process-query-not-ready");
    return events;
}

std::vector<A8TelemetryEvidenceEvent> A8TelemetryEvidence::ObserveSystem(
    const std::optional<PresentMonSystemSnapshot>& snapshot,
    bool systemPollAttempted,
    std::chrono::milliseconds systemAttemptPeriod)
{
    if (!enabled_ || !started_)
        return {};
    std::vector<A8TelemetryEvidenceEvent> events;
    const auto observeDouble = [&](Metric metric, const std::optional<double>& value,
        bool percentage, const wchar_t* suffix)
    {
        const bool valid = value && ValidRepresentableNonnegative(*value) &&
            (!percentage || *value <= 100.0);
        Append(events, ObserveMetric(metric, valid,
            valid ? Decimal(*value) + suffix : std::wstring{},
            systemPollAttempted, systemAttemptPeriod));
    };
    observeDouble(Metric::CpuUsage,
        snapshot ? snapshot->cpuUsagePercent : std::nullopt, true, L"%");
    observeDouble(Metric::CpuClock,
        snapshot ? snapshot->cpuClockMHz : std::nullopt, false, L"MHz");
    observeDouble(Metric::GpuUsage,
        snapshot ? snapshot->gpuUsagePercent : std::nullopt, true, L"%");
    observeDouble(Metric::GpuClock,
        snapshot ? snapshot->gpuClockMHz : std::nullopt, false, L"MHz");
    observeDouble(Metric::GpuPower,
        snapshot ? snapshot->gpuPowerW : std::nullopt, false, L"W");
    const auto vram = snapshot ? snapshot->gpuMemoryUsedBytes : std::nullopt;
    Append(events, ObserveMetric(Metric::VramUsed, vram.has_value(),
        vram ? std::to_wstring(*vram) + L"B" : std::wstring{},
        systemPollAttempted, systemAttemptPeriod));
    return events;
}

std::vector<A8TelemetryEvidenceEvent> A8TelemetryEvidence::ObserveSystemMemory(
    const std::optional<std::uint64_t>& usedBytes,
    std::chrono::milliseconds attemptPeriod)
{
    if (!enabled_ || !started_)
        return {};
    return ObserveMetric(Metric::RamUsed, usedBytes.has_value(),
        usedBytes ? std::to_wstring(*usedBytes) + L"B" : std::wstring{},
        true, attemptPeriod);
}

std::vector<A8TelemetryEvidenceEvent> A8TelemetryEvidence::ObserveBattery(
    const std::optional<WindowsPowerTelemetry>& power,
    std::chrono::milliseconds attemptPeriod)
{
    if (!enabled_ || !started_)
        return {};
    std::vector<A8TelemetryEvidenceEvent> events;
    const auto percent = power ? power->batteryPercent : std::nullopt;
    const bool validPercent = percent && *percent >= 0 && *percent <= 100;
    Append(events, ObserveMetric(Metric::BatteryPercent, validPercent,
        validPercent ? std::to_wstring(*percent) + L"%" : std::wstring{},
        true, attemptPeriod));
    const auto onBattery = power ? power->onBattery : std::nullopt;
    Append(events, ObserveMetric(Metric::AcDc, onBattery.has_value(),
        onBattery ? (*onBattery ? L"DC" : L"AC") : std::wstring{},
        true, attemptPeriod));
    return events;
}

std::vector<A8TelemetryEvidenceEvent> A8TelemetryEvidence::ObserveFps(
    std::uint32_t processId,
    const std::optional<double>& rawDisplayedFps,
    bool processPollAttempted,
    std::chrono::milliseconds attemptPeriod)
{
    if (!enabled_ || !started_ || processId == 0)
        return {};
    const bool valid = rawDisplayedFps &&
        ValidRepresentableNonnegative(*rawDisplayedFps) &&
        std::round(*rawDisplayedFps) > 0.0;
    return ObserveMetric(Metric::Fps, valid,
        valid ? Decimal(*rawDisplayedFps) + L"FPS" : std::wstring{},
        processPollAttempted, attemptPeriod, processId);
}

std::vector<A8TelemetryEvidenceEvent> A8TelemetryEvidence::ObserveMetric(
    Metric metric, bool valid, std::wstring value, bool attempted,
    std::chrono::milliseconds attemptPeriod, std::uint32_t processId)
{
    std::vector<A8TelemetryEvidenceEvent> events;
    auto& state = metrics_[static_cast<std::size_t>(metric)];
    if (valid && !state.successLogged)
    {
        state.successLogged = true;
        std::wstring message = std::wstring(L"[A8Telemetry] metric=") +
            MetricName(metric) + L" source=" + SourceName(metric) +
            L" result=first-success value=" + value;
        if (metric == Metric::Fps)
            message += L" pid=" + std::to_wstring(processId);
        events.push_back({ A8TelemetryEvidenceLevel::Info, std::move(message) });
        return events;
    }
    if (state.successLogged || state.warningLogged || !attempted ||
        attemptPeriod <= std::chrono::milliseconds::zero())
        return events;

    ++state.attempts;
    state.activeAttemptTime += attemptPeriod;
    if (state.activeAttemptTime >= kMissingSampleWarningAfter)
    {
        state.warningLogged = true;
        std::wstring message = std::wstring(L"[A8Telemetry] metric=") +
            MetricName(metric) + L" source=" + SourceName(metric) +
            L" result=unavailable reason=no-valid-sample attempts=" +
            std::to_wstring(state.attempts);
        if (metric == Metric::Fps)
            message += L" pid=" + std::to_wstring(processId);
        events.push_back({ A8TelemetryEvidenceLevel::Warn, std::move(message) });
    }
    return events;
}

const wchar_t* A8TelemetryEvidence::MetricName(Metric metric) noexcept
{
    switch (metric)
    {
    case Metric::CpuUsage: return L"cpuUsage";
    case Metric::CpuClock: return L"cpuClock";
    case Metric::GpuUsage: return L"gpuUsage";
    case Metric::GpuClock: return L"gpuClock";
    case Metric::GpuPower: return L"gpuPower";
    case Metric::VramUsed: return L"vramUsed";
    case Metric::RamUsed: return L"ramUsed";
    case Metric::BatteryPercent: return L"batteryPercent";
    case Metric::AcDc: return L"acDc";
    case Metric::Fps: return L"fps";
    case Metric::Count: break;
    }
    return L"unknown";
}

const wchar_t* A8TelemetryEvidence::SourceName(Metric metric) noexcept
{
    switch (metric)
    {
    case Metric::RamUsed:
    case Metric::BatteryPercent:
    case Metric::AcDc: return L"Windows";
    default: return L"PresentMon";
    }
}

void LogA8TelemetryEvidence(
    const std::vector<A8TelemetryEvidenceEvent>& events) noexcept
{
    for (const auto& event : events)
        RuntimeLogger::Log(event.level == A8TelemetryEvidenceLevel::Warn
            ? RuntimeLogLevel::Warn : RuntimeLogLevel::Info, event.message);
}
}
