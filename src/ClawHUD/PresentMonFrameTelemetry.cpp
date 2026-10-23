#include "PresentMonFrameTelemetry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

namespace clawhud
{
namespace
{
constexpr std::uint32_t kIndependentDeviceId = 0;
constexpr std::uint32_t kFrameConsumeBatch = 64;

const PresentMonMetricCapability* FindMetric(
    const PresentMonTelemetryCapabilities& capabilities, PM_METRIC metric)
{
    for (const auto& candidate : capabilities.metrics)
        if (candidate.id == metric)
            return &candidate;
    return nullptr;
}

bool SupportsFrameMetric(const PresentMonMetricCapability& metric)
{
    if (metric.type != PM_METRIC_TYPE_FRAME_EVENT &&
        metric.type != PM_METRIC_TYPE_DYNAMIC_FRAME)
        return false;
    for (const auto& device : metric.devices)
        if (device.deviceId == kIndependentDeviceId &&
            device.availability == PM_METRIC_AVAILABILITY_AVAILABLE)
            return true;
    return false;
}

bool HasFrameMetric(
    const PresentMonTelemetryCapabilities& capabilities, PM_METRIC id)
{
    const auto* metric = FindMetric(capabilities, id);
    return metric && SupportsFrameMetric(*metric);
}

PM_QUERY_ELEMENT MakeElement(PM_METRIC metric)
{
    return PM_QUERY_ELEMENT{metric, PM_STAT_NONE, kIndependentDeviceId, 0, 0, 0};
}

bool HasValidDoubleLayout(const PM_QUERY_ELEMENT& element,
    std::span<const std::uint8_t> record)
{
    return element.dataSize == sizeof(double) &&
        element.dataOffset <= record.size() &&
        element.dataSize <= record.size() - element.dataOffset;
}

constexpr auto kDiagnosticInterval = std::chrono::seconds(5);
}

std::optional<PresentMonFrameQueryPlan> BuildPresentMonFrameQueryPlan(
    const PresentMonTelemetryCapabilities& capabilities)
{
    if (!HasFrameMetric(capabilities, PM_METRIC_BETWEEN_DISPLAY_CHANGE))
        return std::nullopt;

    PresentMonFrameQueryPlan plan;
    plan.betweenDisplayChangeIndex = plan.elements.size();
    plan.elements.push_back(MakeElement(PM_METRIC_BETWEEN_DISPLAY_CHANGE));

    const auto addOptional = [&](PM_METRIC id, std::optional<std::size_t>& index)
    {
        if (!HasFrameMetric(capabilities, id))
            return;
        index = plan.elements.size();
        plan.elements.push_back(MakeElement(id));
    };
    addOptional(PM_METRIC_PROCESS_ID, plan.processIdIndex);
    addOptional(PM_METRIC_SWAP_CHAIN_ADDRESS, plan.swapChainAddressIndex);
    addOptional(PM_METRIC_FRAME_TYPE, plan.frameTypeIndex);
    addOptional(PM_METRIC_PRESENT_MODE, plan.presentModeIndex);

    return plan;
}

std::optional<double> DecodePresentMonFrameDouble(
    std::span<const std::uint8_t> record, const PM_QUERY_ELEMENT& element)
{
    if (element.dataSize != sizeof(double) ||
        element.dataOffset > record.size() ||
        element.dataSize > record.size() - element.dataOffset)
        return std::nullopt;

    double value{};
    std::memcpy(&value, record.data() + element.dataOffset, sizeof(value));
    if (!std::isfinite(value))
        return std::nullopt;
    return value;
}

bool PresentMonDisplayedFrameDetector::Observe(double betweenDisplayChangeMs) noexcept
{
    if (seen_)
        return false;
    if (!std::isfinite(betweenDisplayChangeMs) || betweenDisplayChangeMs <= 0.0)
        return false;
    seen_ = true;
    return true;
}

bool PresentMonFrameTelemetry::Initialize(
    PresentMonApi2Client& client,
    const PresentMonTelemetryCapabilities& capabilities)
{
    Shutdown(client);
    auto plan = BuildPresentMonFrameQueryPlan(capabilities);
    if (!plan)
        return false;

    plan_ = std::move(*plan);
    queryElements_ = plan_.elements;
    std::uint32_t blobSize = 0;
    const PM_STATUS registrationStatus = client.RegisterFrameQuery(&query_,
        queryElements_.data(), queryElements_.size(), &blobSize);
    if (registrationStatus != PM_STATUS_SUCCESS ||
        query_ == nullptr || blobSize == 0)
    {
        pendingDiagnostic_ = L"[PresentMonFrameVerify] init register status=" +
            std::to_wstring(static_cast<int>(registrationStatus)) +
            L" blobSize=" + std::to_wstring(blobSize);
        query_ = nullptr;
        plan_ = {};
        queryElements_.clear();
        return false;
    }
    blobSize_ = blobSize;
    ready_ = true;
    return true;
}

void PresentMonFrameTelemetry::Shutdown(PresentMonApi2Client& client) noexcept
{
    if (query_)
        client.FreeFrameQuery(query_);
    query_ = nullptr;
    plan_ = {};
    queryElements_.clear();
    blobSize_ = 0;
    armedProcessId_ = 0;
    detector_.Reset();
    ResetAttemptDiagnostics();
    ready_ = false;
}

void PresentMonFrameTelemetry::ResetAttemptDiagnostics()
{
    diagnostics_ = {};
    diagnostics_.startedAt = diagnostics_.lastDiagnosticAt =
        std::chrono::steady_clock::now();
    pendingDiagnostic_.reset();
}

void PresentMonFrameTelemetry::QueuePollSummary(const wchar_t* result, bool force,
    std::optional<double> firstPositive)
{
    const auto now = std::chrono::steady_clock::now();
    if (!force && diagnostics_.firstOutcomeLogged &&
        now - diagnostics_.lastDiagnosticAt < kDiagnosticInterval)
        return;

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - diagnostics_.startedAt).count();
    std::wstring line = firstPositive
        ? L"[PresentMonFrameVerify] first-displayed pid=" +
            std::to_wstring(armedProcessId_) + L" betweenDisplayChangeMs=" +
            std::to_wstring(*firstPositive)
        : L"[PresentMonFrameVerify] poll pid=" +
            std::to_wstring(armedProcessId_);
    line += L" elapsedMs=" + std::to_wstring(elapsedMs) + L" polls=" +
        std::to_wstring(diagnostics_.polls) + L" records=" +
        std::to_wstring(diagnostics_.records) +
        L" emptyPolls=" +
        std::to_wstring(diagnostics_.emptyPolls) + L" statusFailures=" +
        std::to_wstring(diagnostics_.statusFailures) + L" invalidSamples=" +
        std::to_wstring(diagnostics_.invalidSamples) + L" nonpositiveSamples=" +
        std::to_wstring(diagnostics_.nonpositiveSamples) + L" layoutMismatches=" +
        std::to_wstring(diagnostics_.layoutMismatches) + L" lastStatus=" +
        std::to_wstring(static_cast<int>(diagnostics_.lastStatus)) + L" result=" + result;
    pendingDiagnostic_ = std::move(line);
    diagnostics_.firstOutcomeLogged = true;
    diagnostics_.lastDiagnosticAt = now;
}

std::optional<std::wstring> PresentMonFrameTelemetry::TakePendingDiagnostic()
{
    auto diagnostic = std::move(pendingDiagnostic_);
    pendingDiagnostic_.reset();
    return diagnostic;
}

bool PresentMonFrameTelemetry::BeginVerification(
    PresentMonApi2Client& client, std::uint32_t processId)
{
    if (!ready_ || query_ == nullptr || processId == 0)
        return false;
    detector_.Reset();
    armedProcessId_ = 0;
    ResetAttemptDiagnostics();
    // Dropping frames queued before this attempt is mandatory: another
    // shared-session consumer (FPS telemetry, Debug Logging) may already have
    // this PID tracked, so historical frames can be queued. If the flush fails
    // we must not arm the target and let stale evidence satisfy it.
    const PM_STATUS flushStatus = client.FlushFrames(processId);
    pendingDiagnostic_ = L"[PresentMonFrameVerify] begin pid=" +
        std::to_wstring(processId) + L" stage=flush status=" +
        std::to_wstring(static_cast<int>(flushStatus));
    if (flushStatus != PM_STATUS_SUCCESS)
        return false;
    armedProcessId_ = processId;
    return true;
}

std::optional<bool> PresentMonFrameTelemetry::PollDisplayedFrame(
    PresentMonApi2Client& client, std::uint32_t processId)
{
    if (!ready_ || query_ == nullptr || processId == 0 ||
        processId != armedProcessId_)
    {
        if (processId != 0 && diagnostics_.lastUnarmedPid != processId)
        {
            pendingDiagnostic_ = L"[PresentMonFrameVerify] poll pid=" +
                std::to_wstring(processId) + L" armedPid=" +
                std::to_wstring(armedProcessId_) + L" result=not-armed";
            diagnostics_.lastUnarmedPid = processId;
        }
        return std::nullopt;
    }
    if (detector_.DisplayedFrameSeen())
        return true;

    std::vector<std::uint8_t> blob(
        static_cast<std::size_t>(blobSize_) * kFrameConsumeBatch);
    std::uint32_t count = kFrameConsumeBatch;
    const auto status = client.ConsumeFrames(
        query_, processId, blob.data(), &count);
    ++diagnostics_.polls;
    diagnostics_.lastStatus = status;
    if (status != PM_STATUS_SUCCESS)
    {
        ++diagnostics_.statusFailures;
        QueuePollSummary(L"status-error");
        return std::nullopt;
    }

    diagnostics_.records += count;
    if (count == 0)
    {
        ++diagnostics_.emptyPolls;
        QueuePollSummary(L"no-records");
        return false;
    }

    const std::span<const std::uint8_t> view(blob);
    std::uint32_t finiteSamplesInPoll{};
    std::uint32_t nonpositiveInPoll{};
    if (count > kFrameConsumeBatch)
    {
        ++diagnostics_.layoutMismatches;
    }
    const std::uint32_t safeCount = std::min(count, kFrameConsumeBatch);
    for (std::uint32_t i = 0; i < safeCount; ++i)
    {
        const auto offset = static_cast<std::size_t>(i) * blobSize_;
        if (offset > view.size() || blobSize_ > view.size() - offset)
        {
            ++diagnostics_.layoutMismatches;
            continue;
        }
        const auto record = view.subspan(offset, blobSize_);
        if (plan_.betweenDisplayChangeIndex >= queryElements_.size())
        {
            ++diagnostics_.layoutMismatches;
            continue;
        }
        const auto& element = queryElements_[plan_.betweenDisplayChangeIndex];
        if (!HasValidDoubleLayout(element, record))
        {
            ++diagnostics_.layoutMismatches;
            continue;
        }
        const auto interval = DecodePresentMonFrameDouble(record, element);
        if (!interval)
        {
            ++diagnostics_.invalidSamples;
            continue;
        }
        ++finiteSamplesInPoll;
        if (*interval <= 0.0)
        {
            ++diagnostics_.nonpositiveSamples;
            ++nonpositiveInPoll;
            continue;
        }
        if (detector_.Observe(*interval))
        {
            QueuePollSummary(L"first-displayed", true, *interval);
            return true;
        }
    }
    QueuePollSummary(finiteSamplesInPoll == 0 ? L"invalid-interval" :
        (nonpositiveInPoll != 0 ? L"nonpositive" : L"waiting"));
    return false;
}
}
