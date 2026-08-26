#include "WindowsUsageTelemetry.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <dxgi.h>
#include <iomanip>
#include <limits>
#include <sstream>

#include <wrl/client.h>

namespace clawhud
{
namespace
{
constexpr DWORD kPdhMoreData = 0x800007D2u;

std::wstring LuidToken(const LUID& luid, bool highFirst)
{
    std::wostringstream output;
    output << L"luid_0x" << std::hex << std::setfill(L'0') << std::setw(8)
        << (highFirst ? static_cast<std::uint32_t>(luid.HighPart)
                      : luid.LowPart)
        << L"_0x" << std::setw(8)
        << (highFirst ? luid.LowPart
                      : static_cast<std::uint32_t>(luid.HighPart));
    return output.str();
}

std::wstring ShortLuidToken(const LUID& luid, bool lowFirst, bool highWord)
{
    std::wostringstream output;
    output << L"luid_0x" << std::hex << std::setfill(L'0') << std::setw(8)
        << (lowFirst ? luid.LowPart : static_cast<std::uint32_t>(luid.HighPart))
        << L"_0x" << std::setw(4)
        << (lowFirst
            ? static_cast<std::uint16_t>(highWord ? luid.HighPart >> 16 : luid.HighPart)
            : static_cast<std::uint16_t>(highWord ? luid.LowPart >> 16 : luid.LowPart));
    return output.str();
}

std::optional<LUID> FindIntelAdapterLuid()
{
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
        reinterpret_cast<void**>(factory.GetAddressOf()))))
        return std::nullopt;

    for (UINT index = 0;; ++index)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;
        if (!adapter)
            continue;
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) ||
            description.VendorId != 0x8086 ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            continue;
        return description.AdapterLuid;
    }
    return std::nullopt;
}

bool ExpandCounterPaths(const wchar_t* path, std::vector<std::wstring>& paths)
{
    DWORD size{};
    auto status = PdhExpandWildCardPathW(nullptr, path, nullptr, &size, 0);
    if (static_cast<DWORD>(status) != kPdhMoreData || !size)
        return false;
    std::vector<wchar_t> buffer(size);
    status = PdhExpandWildCardPathW(nullptr, path, buffer.data(), &size, 0);
    if (status != ERROR_SUCCESS)
        return false;
    for (const wchar_t* current = buffer.data(); *current;
        current += std::wcslen(current) + 1)
        paths.emplace_back(current);
    return true;
}
}

std::optional<double> NormalizeUsagePercent(double value) noexcept
{
    if (!std::isfinite(value) || value < 0.0)
        return std::nullopt;
    return std::min(value, 100.0);
}

std::optional<double> MaxGpuUsagePercent(const std::vector<double>& values) noexcept
{
    std::optional<double> maximum;
    for (const double value : values)
    {
        if (const auto valid = std::isfinite(value) && value >= 0.0 && value <= 100.0
                ? std::optional<double>(value) : std::nullopt;
            valid && (!maximum || *valid > *maximum))
            maximum = valid;
    }
    return maximum;
}

bool IsIntelGpuMemoryCounterInstance(std::wstring_view instance,
    const LUID& adapterLuid)
{
    const auto highFirst = LuidToken(adapterLuid, true);
    const auto lowFirst = LuidToken(adapterLuid, false);
    const auto lowFirstShort = ShortLuidToken(adapterLuid, true, false);
    const auto lowFirstShortHighWord = ShortLuidToken(adapterLuid, true, true);
    const auto highFirstShort = ShortLuidToken(adapterLuid, false, false);
    const auto highFirstShortHighWord = ShortLuidToken(adapterLuid, false, true);
    return instance.find(highFirst) != std::wstring_view::npos ||
        instance.find(lowFirst) != std::wstring_view::npos ||
        instance.find(lowFirstShort) != std::wstring_view::npos ||
        instance.find(lowFirstShortHighWord) != std::wstring_view::npos ||
        instance.find(highFirstShort) != std::wstring_view::npos ||
        instance.find(highFirstShortHighWord) != std::wstring_view::npos;
}

std::optional<std::uint64_t> CombineGpuMemoryBytes(
    std::optional<std::uint64_t> dedicated,
    std::optional<std::uint64_t> shared) noexcept
{
    if (!dedicated && !shared)
        return std::nullopt;
    const auto dedicatedValue = dedicated.value_or(0);
    const auto sharedValue = shared.value_or(0);
    if (dedicatedValue > std::numeric_limits<std::uint64_t>::max() - sharedValue)
        return std::nullopt;
    return dedicatedValue + sharedValue;
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
    intelDedicatedMemoryCounters_.clear();
    intelSharedMemoryCounters_.clear();
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
    AddIntelGpuMemoryCounters();
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

bool WindowsUsageSampler::AddIntelGpuMemoryCounters()
{
    const auto adapterLuid = FindIntelAdapterLuid();
    if (!adapterLuid)
        return false;

    std::vector<std::wstring> dedicatedPaths;
    std::vector<std::wstring> sharedPaths;
    if (!ExpandCounterPaths(L"\\GPU Adapter Memory(*)\\Dedicated Usage",
        dedicatedPaths) || !ExpandCounterPaths(
            L"\\GPU Adapter Memory(*)\\Shared Usage", sharedPaths))
        return false;

    for (const auto& path : dedicatedPaths)
    {
        if (!IsIntelGpuMemoryCounterInstance(path, *adapterLuid))
            continue;
        PDH_HCOUNTER counter{};
        if (PdhAddEnglishCounterW(query_, path.c_str(), 0, &counter) == ERROR_SUCCESS)
            intelDedicatedMemoryCounters_.push_back(counter);
    }
    for (const auto& path : sharedPaths)
    {
        if (!IsIntelGpuMemoryCounterInstance(path, *adapterLuid))
            continue;
        PDH_HCOUNTER counter{};
        if (PdhAddEnglishCounterW(query_, path.c_str(), 0, &counter) == ERROR_SUCCESS)
            intelSharedMemoryCounters_.push_back(counter);
    }
    return !intelDedicatedMemoryCounters_.empty() ||
        !intelSharedMemoryCounters_.empty();
}

bool WindowsUsageSampler::IsValidCounter(
    const PDH_FMT_COUNTERVALUE& value) noexcept
{
    return value.CStatus == ERROR_SUCCESS || value.CStatus == 0x00000001L;
}

std::optional<double> WindowsUsageSampler::ReadCounter(
    PDH_HCOUNTER counter, bool capAbove100) const
{
    if (!counter)
        return std::nullopt;
    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) !=
        ERROR_SUCCESS || !IsValidCounter(value))
        return std::nullopt;
    if (capAbove100)
        return NormalizeUsagePercent(value.doubleValue);
    if (!std::isfinite(value.doubleValue) || value.doubleValue < 0.0 ||
        value.doubleValue > 100.0)
        return std::nullopt;
    return value.doubleValue;
}

std::optional<std::uint64_t> WindowsUsageSampler::ReadByteCounter(
    PDH_HCOUNTER counter) const
{
    if (!counter)
        return std::nullopt;
    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_LARGE, nullptr, &value) !=
        ERROR_SUCCESS || !IsValidCounter(value) || value.largeValue < 0)
        return std::nullopt;
    return static_cast<std::uint64_t>(value.largeValue);
}

std::optional<std::uint64_t> WindowsUsageSampler::ReadByteCounters(
    const std::vector<PDH_HCOUNTER>& counters) const
{
    std::optional<std::uint64_t> total;
    for (const auto counter : counters)
    {
        const auto value = ReadByteCounter(counter);
        if (!value)
            continue;
        total = CombineGpuMemoryBytes(total, value);
        if (!total)
            return std::nullopt;
    }
    return total;
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
    result.cpuUsagePercent = ReadCounter(cpuCounter_, true);
    std::vector<double> gpuValues;
    for (const auto counter : gpuCounters_)
    {
        if (const auto value = ReadCounter(counter, false))
            gpuValues.push_back(*value);
    }
    result.gpuUsagePercent = MaxGpuUsagePercent(gpuValues);
    result.intelGpuMemoryUsedBytes = CombineGpuMemoryBytes(
        ReadByteCounters(intelDedicatedMemoryCounters_),
        ReadByteCounters(intelSharedMemoryCounters_));
    return result;
}
}
