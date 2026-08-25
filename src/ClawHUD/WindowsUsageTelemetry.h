#pragma once

#include <windows.h>
#include <pdh.h>

#include <optional>
#include <string>
#include <vector>

namespace clawhud
{
struct WindowsUsageTelemetry
{
    std::optional<double> cpuUsagePercent;
    std::optional<double> gpuUsagePercent;
};

std::optional<double> NormalizeUsagePercent(double value) noexcept;
std::optional<double> MaxGpuUsagePercent(const std::vector<double>& values) noexcept;

class WindowsUsageSampler
{
public:
    ~WindowsUsageSampler();
    bool Initialize();
    void Reset() noexcept;
    std::optional<WindowsUsageTelemetry> Sample();
    bool Initialized() const noexcept { return query_ != nullptr; }

private:
    static bool IsValidCounter(const PDH_FMT_COUNTERVALUE& value) noexcept;
    bool AddGpuCounters();
    std::optional<double> ReadCounter(PDH_HCOUNTER counter,
        bool capAbove100) const;

    PDH_HQUERY query_{};
    PDH_HCOUNTER cpuCounter_{};
    std::vector<PDH_HCOUNTER> gpuCounters_;
    bool primed_{};
};
}
