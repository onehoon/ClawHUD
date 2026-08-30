#pragma once
#include "PresentMonApi2Api.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace clawhud
{
struct PresentMonSystemSnapshot
{
    std::optional<double> cpuUsagePercent;
    std::optional<double> gpuUsagePercent;
    std::optional<double> gpuClockMHz;
    std::optional<std::uint64_t> gpuMemoryUsedBytes;
};
struct PresentMonProcessSnapshot
{
    std::uint32_t processId{};
    std::optional<double> displayedFps;
    // Diagnostic comparison data. Never consumed by HUD rendering.
    std::optional<double> presentedFps;
    // Identifies the dominant swap chain PresentMon middleware selected.
    std::optional<std::uint64_t> swapChainAddress;
};
struct PresentMonDeviceCapability { std::uint32_t id{}; PM_DEVICE_TYPE type{ PM_DEVICE_TYPE_INDEPENDENT }; PM_DEVICE_VENDOR vendor{ PM_DEVICE_VENDOR_UNKNOWN }; std::string name; };
struct PresentMonDeviceMetricCapability { std::uint32_t deviceId{}; PM_METRIC_AVAILABILITY availability{ PM_METRIC_AVAILABILITY_UNAVAILABLE }; std::uint32_t arraySize{}; };
struct PresentMonMetricCapability { PM_METRIC id{}; PM_METRIC_TYPE type{}; PM_UNIT unit{}; PM_UNIT preferredUnit{}; PM_DATA_TYPE polledType{ PM_DATA_TYPE_VOID }; PM_DATA_TYPE frameType{ PM_DATA_TYPE_VOID }; PM_ENUM enumId{ PM_ENUM_NULL_ENUM }; std::vector<PM_STAT> statistics; std::vector<PresentMonDeviceMetricCapability> devices; };
struct PresentMonTelemetryCapabilities { std::vector<PresentMonDeviceCapability> devices; std::vector<PresentMonMetricCapability> metrics; };
}
