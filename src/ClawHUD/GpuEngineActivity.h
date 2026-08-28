#pragma once

#include <windows.h>
#include <pdh.h>

#include <optional>
#include <string>
#include <vector>

namespace clawhud
{
struct GpuEngineActivity
{
    DWORD processId{};
    double utilization{};
    bool intelAdapter{};
    std::wstring engine;
};

std::vector<DWORD> SelectGpuActiveProcessIds(
    const std::vector<GpuEngineActivity>& activities,
    DWORD foregroundProcessId,
    const std::vector<DWORD>& baselineProcessIds = {}) noexcept;

class GpuEngineActivitySampler
{
public:
    ~GpuEngineActivitySampler();
    bool Initialize();
    void Reset() noexcept;
    std::vector<GpuEngineActivity> Sample();
    bool Initialized() const noexcept { return query_ != nullptr; }

private:
    struct Counter
    {
        PDH_HCOUNTER handle{};
        DWORD processId{};
        bool intelAdapter{};
        std::wstring engine;
    };
    bool BindCounters();

    PDH_HQUERY query_{};
    std::vector<Counter> counters_;
    bool primed_{};
};
}
