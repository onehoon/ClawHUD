#include "GpuEngineActivity.h"

#include "WindowsUsageTelemetry.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <dxgi.h>
#include <pdh.h>
#include <string_view>
#include <unordered_map>

namespace clawhud
{
namespace
{
constexpr DWORD kPdhMoreData = 0x800007D2u;

std::vector<std::wstring> ExpandEnginePaths()
{
    DWORD size{};
    const auto pattern = L"\\GPU Engine(*)\\Utilization Percentage";
    auto status = PdhExpandWildCardPathW(nullptr, pattern, nullptr, &size, 0);
    if (static_cast<DWORD>(status) != kPdhMoreData || !size)
        return {};
    std::vector<wchar_t> buffer(size);
    status = PdhExpandWildCardPathW(nullptr, pattern, buffer.data(), &size, 0);
    if (status != ERROR_SUCCESS)
        return {};
    std::vector<std::wstring> paths;
    for (const wchar_t* current = buffer.data(); *current;
        current += std::wcslen(current) + 1)
        paths.emplace_back(current);
    return paths;
}

std::optional<DWORD> ParsePid(std::wstring_view path)
{
    const auto start = path.find(L"pid_");
    if (start == std::wstring_view::npos)
        return std::nullopt;
    auto position = start + 4;
    DWORD value{};
    std::size_t digits{};
    while (position < path.size() && path[position] >= L'0' &&
        path[position] <= L'9')
    {
        const DWORD digit = static_cast<DWORD>(path[position++] - L'0');
        if (value > (MAXDWORD - digit) / 10u)
            return std::nullopt;
        value = value * 10u + digit;
        ++digits;
    }
    return digits ? std::optional<DWORD>(value) : std::nullopt;
}

std::wstring ParseEngine(std::wstring_view path)
{
    const auto start = path.find(L"_eng_");
    if (start == std::wstring_view::npos)
        return {};
    const auto end = path.find(L')', start);
    return end == std::wstring_view::npos ? std::wstring(path.substr(start + 5))
        : std::wstring(path.substr(start + 5, end - start - 5));
}

bool IsPositive(double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}
}

std::vector<DWORD> SelectGpuActiveProcessIds(
    const std::vector<GpuEngineActivity>& activities,
    DWORD foregroundProcessId,
    const std::vector<DWORD>& baselineProcessIds,
    bool allowBaselineRenderer) noexcept
{
    struct Candidate { bool intel{}; bool foreground{}; bool recent{}; double activity{}; };
    std::unordered_map<DWORD, Candidate> candidates;
    for (const auto& activity : activities)
    {
        if (!activity.processId || !IsPositive(activity.utilization))
            continue;
        if (!allowBaselineRenderer && std::find(baselineProcessIds.begin(),
            baselineProcessIds.end(), activity.processId) != baselineProcessIds.end())
            continue;
        auto& candidate = candidates[activity.processId];
        candidate.intel = candidate.intel || activity.intelAdapter;
        candidate.foreground = candidate.foreground ||
            activity.processId == foregroundProcessId;
        candidate.recent = std::find(baselineProcessIds.begin(),
            baselineProcessIds.end(), activity.processId) == baselineProcessIds.end();
        candidate.activity += activity.utilization;
    }
    std::vector<DWORD> result;
    for (const auto& [processId, candidate] : candidates)
        result.push_back(processId);
    std::sort(result.begin(), result.end(), [&](DWORD left, DWORD right)
    {
        const auto& a = candidates[left];
        const auto& b = candidates[right];
        if (a.foreground != b.foreground) return a.foreground > b.foreground;
        if (a.recent != b.recent) return a.recent > b.recent;
        if (a.intel != b.intel) return a.intel > b.intel;
        return a.activity > b.activity;
    });
    return result;
}

std::vector<std::wstring> SelectUnboundGpuEnginePaths(
    const std::vector<std::wstring>& paths,
    const std::unordered_set<std::wstring>& boundPaths)
{
    std::vector<std::wstring> result;
    for (const auto& path : paths)
    {
        if (boundPaths.find(path) == boundPaths.end())
            result.push_back(path);
    }
    return result;
}

GpuEngineActivitySampler::~GpuEngineActivitySampler()
{
    Reset();
}

void GpuEngineActivitySampler::Reset() noexcept
{
    if (query_)
        PdhCloseQuery(query_);
    query_ = nullptr;
    counters_.clear();
    boundPaths_.clear();
    primed_ = false;
}

bool GpuEngineActivitySampler::Initialize()
{
    Reset();
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS ||
        !BindNewCounters())
    {
        Reset();
        return false;
    }
    if (PdhCollectQueryData(query_) != ERROR_SUCCESS)
    {
        Reset();
        return false;
    }
    primed_ = true;
    return true;
}

bool GpuEngineActivitySampler::BindNewCounters()
{
    const auto adapter = FindIntelAdapterLuid();
    bool added = false;
    for (const auto& path : SelectUnboundGpuEnginePaths(
        ExpandEnginePaths(), boundPaths_))
    {
        const auto pid = ParsePid(path);
        if (!pid)
            continue;
        PDH_HCOUNTER counter{};
        if (PdhAddEnglishCounterW(query_, path.c_str(), 0, &counter) != ERROR_SUCCESS)
            continue;
        boundPaths_.insert(path);
        counters_.push_back(Counter{counter, *pid,
            adapter && IsIntelGpuMemoryCounterInstance(path, *adapter),
            ParseEngine(path)});
        added = true;
    }
    return added || !counters_.empty();
}

std::vector<GpuEngineActivity> GpuEngineActivitySampler::Sample()
{
    if (!query_)
        return {};
    BindNewCounters();
    if (PdhCollectQueryData(query_) != ERROR_SUCCESS)
        return {};
    if (primed_)
    {
        primed_ = false;
        return {};
    }
    std::vector<GpuEngineActivity> result;
    for (const auto& counter : counters_)
    {
        PDH_FMT_COUNTERVALUE value{};
        if (PdhGetFormattedCounterValue(counter.handle, PDH_FMT_DOUBLE, nullptr,
            &value) != ERROR_SUCCESS || !IsPositive(value.doubleValue))
            continue;
        result.push_back({counter.processId, value.doubleValue,
            counter.intelAdapter, counter.engine});
    }
    return result;
}
}
