#include "WindowsUsageTelemetry.h"

#include "RuntimeLogger.h"

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

void LogMemoryDiagnostic(const std::wstring& message)
{
    RuntimeLogger::Log(RuntimeLogLevel::Info, message);
}

std::wstring HexPdhStatus(PDH_STATUS status)
{
    std::wostringstream output;
    output << L"0x" << std::hex << std::setfill(L'0') << std::setw(8)
        << static_cast<DWORD>(status);
    return output.str();
}

std::wstring LuidToken(const LUID& luid)
{
    std::wostringstream output;
    output << L"luid_0x" << std::hex << std::setfill(L'0') << std::setw(8)
        << static_cast<std::uint32_t>(luid.HighPart)
        << L"_0x" << std::setw(8)
        << luid.LowPart;
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
    const auto token = LuidToken(adapterLuid);
    const auto position = instance.find(token);
    if (position == std::wstring_view::npos)
        return false;
    const auto suffix = position + token.size();
    return suffix < instance.size() &&
        instance.substr(suffix).starts_with(L"_phys_");
}

std::optional<std::uint64_t> CombineGpuMemoryBytes(
    std::optional<std::uint64_t> dedicated,
    std::optional<std::uint64_t> shared) noexcept
{
    if (!dedicated || !shared)
        return std::nullopt;
    if (*dedicated > std::numeric_limits<std::uint64_t>::max() - *shared)
        return std::nullopt;
    return *dedicated + *shared;
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
    memoryDiagnosticsLogged_ = false;
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

    LogMemoryDiagnostic(L"Intel GPU adapter LUID=" + LuidToken(*adapterLuid));

    std::vector<std::wstring> dedicatedPaths;
    std::vector<std::wstring> sharedPaths;
    const bool dedicatedExpanded = ExpandCounterPaths(
        L"\\GPU Adapter Memory(*)\\Dedicated Usage", dedicatedPaths);
    const bool sharedExpanded = ExpandCounterPaths(
        L"\\GPU Adapter Memory(*)\\Shared Usage", sharedPaths);

    LogMemoryDiagnostic(L"GPU memory dedicated paths=" +
        std::to_wstring(dedicatedPaths.size()) + L" shared paths=" +
        std::to_wstring(sharedPaths.size()));
    if (!dedicatedExpanded || !sharedExpanded)
        return false;
    std::size_t dedicatedMatches{};
    std::size_t sharedMatches{};

    for (const auto& path : dedicatedPaths)
    {
        if (!IsIntelGpuMemoryCounterInstance(path, *adapterLuid))
            continue;
        ++dedicatedMatches;
        PDH_HCOUNTER counter{};
        const PDH_STATUS status = PdhAddEnglishCounterW(
            query_, path.c_str(), 0, &counter);
        LogMemoryDiagnostic(L"GPU memory counter add kind=Dedicated path=" +
            path + L" status=" + HexPdhStatus(status));
        if (status == ERROR_SUCCESS)
            intelDedicatedMemoryCounters_.push_back(counter);
    }
    for (const auto& path : sharedPaths)
    {
        if (!IsIntelGpuMemoryCounterInstance(path, *adapterLuid))
            continue;
        ++sharedMatches;
        PDH_HCOUNTER counter{};
        const PDH_STATUS status = PdhAddEnglishCounterW(
            query_, path.c_str(), 0, &counter);
        LogMemoryDiagnostic(L"GPU memory counter add kind=Shared path=" +
            path + L" status=" + HexPdhStatus(status));
        if (status == ERROR_SUCCESS)
            intelSharedMemoryCounters_.push_back(counter);
    }
    LogMemoryDiagnostic(L"GPU memory dedicated matches=" +
        std::to_wstring(dedicatedMatches) + L" shared matches=" +
        std::to_wstring(sharedMatches));
    return !intelDedicatedMemoryCounters_.empty() &&
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
    if (counters.empty())
        return std::nullopt;
    std::optional<std::uint64_t> total;
    for (const auto counter : counters)
    {
        const auto value = ReadByteCounter(counter);
        if (!value)
            return std::nullopt;
        if (total && *total > std::numeric_limits<std::uint64_t>::max() - *value)
            return std::nullopt;
        total = total.value_or(0) + *value;
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
    const auto dedicated = ReadByteCounters(intelDedicatedMemoryCounters_);
    const auto shared = ReadByteCounters(intelSharedMemoryCounters_);
    result.intelGpuMemoryUsedBytes = CombineGpuMemoryBytes(dedicated, shared);
    if (!memoryDiagnosticsLogged_)
    {
        const auto format = [](const auto& value)
        {
            return value ? std::to_wstring(*value) : std::wstring(L"unavailable");
        };
        LogMemoryDiagnostic(L"GPU memory dedicated first=" + format(dedicated) +
            L" shared first=" + format(shared) + L" combined=" +
            format(CombineGpuMemoryBytes(dedicated, shared)));
        memoryDiagnosticsLogged_ = true;
    }
    return result;
}
}
