#pragma once

#include "PresentMonTelemetryTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace clawhud
{
class PresentMonApi2Client;
constexpr std::uint32_t kSystemTelemetryProcessId = 0;

enum class SystemMetricSlot { CpuUsage, GpuUsage, GpuFrequency, GpuMemoryUsed };

struct SystemMetricBinding
{
    SystemMetricSlot slot{};
    std::size_t elementIndex{};
    PM_DATA_TYPE type{ PM_DATA_TYPE_VOID };
    PM_UNIT unit{};
};

struct PresentMonSystemQueryPlan
{
    std::vector<PM_QUERY_ELEMENT> elements;
    std::vector<SystemMetricBinding> bindings;
};

PresentMonSystemQueryPlan BuildPresentMonSystemQueryPlan(
    const PresentMonTelemetryCapabilities& capabilities);
std::optional<double> DecodePresentMonPercentage(
    const std::uint8_t* blob, const PM_QUERY_ELEMENT& element, PM_DATA_TYPE type);
std::optional<double> DecodePresentMonFrequencyMHz(
    const std::uint8_t* blob, const PM_QUERY_ELEMENT& element,
    PM_DATA_TYPE type, PM_UNIT unit);
std::optional<std::uint64_t> DecodePresentMonMemoryBytes(
    const std::uint8_t* blob, const PM_QUERY_ELEMENT& element,
    PM_DATA_TYPE type, PM_UNIT unit);
bool HasPresentMonDynamicQueryResult(PM_STATUS status, std::uint32_t resultCount) noexcept;
std::optional<PresentMonSystemSnapshot> DecodePresentMonSystemSnapshot(
    PM_STATUS status, std::uint32_t resultCount, const std::uint8_t* blob,
    const std::vector<PM_QUERY_ELEMENT>& elements,
    const std::vector<SystemMetricBinding>& bindings);

class PresentMonSystemTelemetry
{
public:
    bool Initialize(PresentMonApi2Client& client,
        const PresentMonTelemetryCapabilities& capabilities);
    void Shutdown(PresentMonApi2Client& client) noexcept;
    std::optional<PresentMonSystemSnapshot> Read(PresentMonApi2Client& client);
    bool Ready() const noexcept { return query_ != nullptr; }

private:
    PM_DYNAMIC_QUERY_HANDLE query_{};
    std::vector<PM_QUERY_ELEMENT> elements_;
    std::vector<SystemMetricBinding> bindings_;
    std::vector<std::uint8_t> blob_;
    bool pollDiagnosticsInitialized_{};
    PM_STATUS lastPollStatus_{};
    std::uint32_t lastPollResultCount_{};
    bool firstSampleLogged_{};
};
}
