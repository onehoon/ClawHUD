#include "WindowsUsageTelemetry.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace clawhud
{
std::optional<double> ValidateUsagePercent(double value) noexcept
{
    return std::isfinite(value) && value >= 0.0 && value <= 100.0
        ? std::optional<double>(value) : std::nullopt;
}

std::optional<double> MaxGpuUsagePercent(const std::vector<double>& values) noexcept
{
    std::optional<double> maximum;
    for (const double value : values)
    {
        if (const auto valid = ValidateUsagePercent(value);
            valid && (!maximum || *valid > *maximum))
            maximum = valid;
    }
    return maximum;
}

WindowsUsageSampler::~WindowsUsageSampler()
{
    Reset();
}

void WindowsUsageSampler::Reset() noexcept
{
    if (query_)
        PdhCloseQuery(query_);
    query_ = nullptr;
    cpuCounter_ = nullptr;
    gpuCounters_.clear();
    primed_ = false;
}

bool WindowsUsageSampler::Initialize()
{
    Reset();
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS)
        return false;
    if (PdhAddEnglishCounterW(query_,
        L"\\Processor Information(_Total)\\% Processor Utility", 0,
        &cpuCounter_) != ERROR_SUCCESS)
    {
        Reset();
        return false;
    }
    AddGpuCounters();
    if (PdhCollectQueryData(query_) != ERROR_SUCCESS)
    {
        Reset();
        return false;
    }
    primed_ = false;
    return true;
}

bool WindowsUsageSampler::AddGpuCounters()
{
    constexpr wchar_t kPath[] = L"\\GPU Engine(*)\\Utilization Percentage";
    constexpr DWORD kPdhMoreData = 0x800007D2u;
    DWORD size{};
    auto status = PdhExpandWildCardPathW(nullptr, kPath, nullptr, &size, 0);
    if (static_cast<DWORD>(status) != kPdhMoreData || !size)
        return false;
    std::vector<wchar_t> paths(size);
    status = PdhExpandWildCardPathW(nullptr, kPath, paths.data(), &size, 0);
    if (status != ERROR_SUCCESS)
        return false;
    for (const wchar_t* path = paths.data(); *path;
        path += std::wcslen(path) + 1)
    {
        if (!std::wcsstr(path, L"engtype_3D"))
            continue;
        PDH_HCOUNTER counter{};
        if (PdhAddEnglishCounterW(query_, path, 0, &counter) == ERROR_SUCCESS)
            gpuCounters_.push_back(counter);
    }
    return !gpuCounters_.empty();
}

bool WindowsUsageSampler::IsValidCounter(
    const PDH_FMT_COUNTERVALUE& value) noexcept
{
    return value.CStatus == ERROR_SUCCESS || value.CStatus == 0x00000001L;
}

std::optional<double> WindowsUsageSampler::ReadCounter(PDH_HCOUNTER counter) const
{
    if (!counter)
        return std::nullopt;
    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) !=
        ERROR_SUCCESS || !IsValidCounter(value))
        return std::nullopt;
    return ValidateUsagePercent(value.doubleValue);
}

std::optional<WindowsUsageTelemetry> WindowsUsageSampler::Sample()
{
    if (!query_ || PdhCollectQueryData(query_) != ERROR_SUCCESS)
        return std::nullopt;
    if (!primed_)
    {
        primed_ = true;
        return WindowsUsageTelemetry{};
    }
    WindowsUsageTelemetry result{};
    result.cpuUsagePercent = ReadCounter(cpuCounter_);
    std::vector<double> gpuValues;
    for (const auto counter : gpuCounters_)
    {
        if (const auto value = ReadCounter(counter))
            gpuValues.push_back(*value);
    }
    result.gpuUsagePercent = MaxGpuUsagePercent(gpuValues);
    return result;
}
}
