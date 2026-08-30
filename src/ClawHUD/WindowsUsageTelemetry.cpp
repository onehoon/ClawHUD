#include "WindowsUsageTelemetry.h"

#include "RuntimeLogger.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cwchar>
#include <dxgi.h>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

#include <wrl/client.h>

namespace clawhud
{
namespace
{
constexpr DWORD kPdhMoreData = 0x800007D2u;
constexpr unsigned int kMaxIntelMemoryRebindAttempts = 3;
constexpr unsigned int kIntelMemoryFailureThreshold = 3;
constexpr unsigned int kIntelMemoryRebindCooldownSamples = 30;

void LogMemoryDebug(const std::wstring& message)
{
    RuntimeLogger::Log(RuntimeLogLevel::Debug, message);
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

bool LocalizeEnglishWildcardPath(PDH_HQUERY query,
    const wchar_t* englishPath, std::wstring& localizedPath)
{
    PDH_HCOUNTER temporaryCounter{};
    if (PdhAddEnglishCounterW(query, englishPath, 0, &temporaryCounter) !=
        ERROR_SUCCESS)
        return false;

    DWORD size{};
    auto status = PdhGetCounterInfoW(temporaryCounter, FALSE, &size, nullptr);
    if (static_cast<DWORD>(status) != kPdhMoreData ||
        size < sizeof(PDH_COUNTER_INFO))
    {
        PdhRemoveCounter(temporaryCounter);
        return false;
    }
    std::vector<std::byte> buffer(size);
    auto* info = reinterpret_cast<PDH_COUNTER_INFO*>(buffer.data());
    status = PdhGetCounterInfoW(temporaryCounter, FALSE, &size, info);
    if (status == ERROR_SUCCESS && info->szFullPath[0])
        localizedPath = info->szFullPath;
    PdhRemoveCounter(temporaryCounter);
    return status == ERROR_SUCCESS && !localizedPath.empty();
}

bool ExpandCounterPaths(PDH_HQUERY query, const wchar_t* englishPath,
    std::vector<std::wstring>& paths)
{
    std::wstring localizedPath;
    if (!LocalizeEnglishWildcardPath(query, englishPath, localizedPath))
        return false;
    DWORD size{};
    auto status = PdhExpandWildCardPathW(nullptr, localizedPath.c_str(),
        nullptr, &size, 0);
    if (static_cast<DWORD>(status) != kPdhMoreData || !size)
        return false;
    std::vector<wchar_t> buffer(size);
    status = PdhExpandWildCardPathW(nullptr, localizedPath.c_str(),
        buffer.data(), &size, 0);
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

int HexDigit(wchar_t value) noexcept
{
    if (value >= L'0' && value <= L'9') return value - L'0';
    if (value >= L'a' && value <= L'f') return value - L'a' + 10;
    if (value >= L'A' && value <= L'F') return value - L'A' + 10;
    return -1;
}

std::optional<std::uint32_t> ParseHex32(std::wstring_view text,
    std::size_t& position) noexcept
{
    if (position + 2 > text.size() || text[position] != L'0' ||
        (text[position + 1] != L'x' && text[position + 1] != L'X'))
        return std::nullopt;
    position += 2;
    std::uint32_t result{};
    std::size_t digits{};
    while (position < text.size())
    {
        const int digit = HexDigit(text[position]);
        if (digit < 0)
            break;
        if (digits == 8 || result > (std::numeric_limits<std::uint32_t>::max() -
            static_cast<std::uint32_t>(digit)) / 16u)
            return std::nullopt;
        result = result * 16u + static_cast<std::uint32_t>(digit);
        ++position;
        ++digits;
    }
    return digits ? std::optional<std::uint32_t>(result) : std::nullopt;
}

std::optional<LUID> ParseGpuMemoryInstanceLuid(std::wstring_view instance)
{
    constexpr std::wstring_view prefix = L"luid_";
    constexpr std::wstring_view physicalSuffix = L"_phys_";
    std::size_t position = instance.find(prefix);
    if (position == std::wstring_view::npos)
        return std::nullopt;
    position += prefix.size();
    const auto high = ParseHex32(instance, position);
    if (!high || position >= instance.size() || instance[position++] != L'_')
        return std::nullopt;
    const auto low = ParseHex32(instance, position);
    if (!low || instance.substr(position).find(physicalSuffix) != 0)
        return std::nullopt;
    position += physicalSuffix.size();
    if (position == instance.size())
        return std::nullopt;
    LUID result{};
    result.LowPart = *low;
    result.HighPart = static_cast<LONG>(*high);
    return result;
}

bool IsIntelGpuMemoryCounterInstance(std::wstring_view instance,
    const LUID& adapterLuid)
{
    const auto parsed = ParseGpuMemoryInstanceLuid(instance);
    return parsed && parsed->LowPart == adapterLuid.LowPart &&
        parsed->HighPart == adapterLuid.HighPart;
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

std::optional<WindowsUsageTelemetry> MergeWindowsUsageTelemetry(
    const std::optional<WindowsUsageTelemetry>& previous,
    const std::optional<WindowsUsageTelemetry>& sample) noexcept
{
    if (!sample)
        return previous;
    WindowsUsageTelemetry merged = previous.value_or(WindowsUsageTelemetry{});
    if (sample->cpuUsagePercent)
        merged.cpuUsagePercent = sample->cpuUsagePercent;
    if (sample->systemMemoryUsedBytes)
        merged.systemMemoryUsedBytes = sample->systemMemoryUsedBytes;
    if (sample->intelGpuMemoryUsedBytes)
        merged.intelGpuMemoryUsedBytes = sample->intelGpuMemoryUsedBytes;
    return merged;
}

bool ShouldInvalidateWindowsUsageTelemetry(
    unsigned consecutiveFailures, unsigned failureThreshold) noexcept
{
    return failureThreshold != 0 && consecutiveFailures >= failureThreshold;
}

bool ShouldRetryIntelGpuMemoryCounters(bool dedicatedEmpty,
    bool sharedEmpty, unsigned int attempts) noexcept
{
    return NeedsIntelGpuMemoryBinding(dedicatedEmpty, sharedEmpty) &&
        attempts < kMaxIntelMemoryRebindAttempts;
}

bool NeedsIntelGpuMemoryBinding(bool dedicatedEmpty,
    bool sharedEmpty) noexcept
{
    return dedicatedEmpty || sharedEmpty;
}

bool ShouldReleaseIntelGpuMemoryCounters(
    unsigned consecutiveFailures, unsigned failureThreshold) noexcept
{
    return failureThreshold != 0 &&
        consecutiveFailures >= failureThreshold;
}

bool ShouldRearmIntelGpuMemoryCounters(unsigned attempts,
    unsigned cooldownSamples, unsigned maxAttempts,
    unsigned cooldownThreshold) noexcept
{
    return maxAttempts != 0 && cooldownThreshold != 0 &&
        attempts >= maxAttempts && cooldownSamples >= cooldownThreshold;
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
    intelDedicatedMemoryCounters_.clear();
    intelSharedMemoryCounters_.clear();
    primed_ = false;
    memoryDiagnosticsLogged_ = false;
    intelMemoryRebindAttempts_ = 0;
    intelMemoryFailureCount_ = 0;
    intelMemoryRebindCooldownSamples_ = 0;
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
    if (!TryBindIntelGpuMemoryCounters())
        LogMemoryDebug(L"Initial Intel GPU memory counter binding unavailable; bounded retry enabled");
    if (PdhCollectQueryData(query_) != ERROR_SUCCESS)
    {
        Reset();
        return false;
    }
    primed_ = false;
    return true;
}

bool WindowsUsageSampler::AddIntelGpuMemoryCounters()
{
    const auto adapterLuid = FindIntelAdapterLuid();
    if (!adapterLuid)
        return false;

    LogMemoryDebug(L"Intel GPU adapter LUID=" + LuidToken(*adapterLuid));

    std::vector<std::wstring> dedicatedPaths;
    std::vector<std::wstring> sharedPaths;
    const bool dedicatedExpanded = ExpandCounterPaths(
        query_, L"\\GPU Adapter Memory(*)\\Dedicated Usage", dedicatedPaths);
    const bool sharedExpanded = ExpandCounterPaths(
        query_, L"\\GPU Adapter Memory(*)\\Shared Usage", sharedPaths);

    LogMemoryDebug(L"GPU memory dedicated paths=" +
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
        const PDH_STATUS status = PdhAddCounterW(
            query_, path.c_str(), 0, &counter);
        LogMemoryDebug(L"GPU memory counter add kind=Dedicated path=" +
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
        const PDH_STATUS status = PdhAddCounterW(
            query_, path.c_str(), 0, &counter);
        LogMemoryDebug(L"GPU memory counter add kind=Shared path=" +
            path + L" status=" + HexPdhStatus(status));
        if (status == ERROR_SUCCESS)
            intelSharedMemoryCounters_.push_back(counter);
    }
    LogMemoryDebug(L"GPU memory dedicated matches=" +
        std::to_wstring(dedicatedMatches) + L" shared matches=" +
        std::to_wstring(sharedMatches));
    return !intelDedicatedMemoryCounters_.empty() &&
        !intelSharedMemoryCounters_.empty();
}

bool WindowsUsageSampler::TryBindIntelGpuMemoryCounters()
{
    const bool dedicatedEmpty = intelDedicatedMemoryCounters_.empty();
    const bool sharedEmpty = intelSharedMemoryCounters_.empty();
    if (!dedicatedEmpty && !sharedEmpty)
        return true;
    if (intelMemoryRebindAttempts_ >= kMaxIntelMemoryRebindAttempts)
    {
        ++intelMemoryRebindCooldownSamples_;
        if (!ShouldRearmIntelGpuMemoryCounters(
                intelMemoryRebindAttempts_, intelMemoryRebindCooldownSamples_,
                kMaxIntelMemoryRebindAttempts,
                kIntelMemoryRebindCooldownSamples))
            return false;
        intelMemoryRebindAttempts_ = 0;
        intelMemoryRebindCooldownSamples_ = 0;
    }
    if (!ShouldRetryIntelGpuMemoryCounters(
            dedicatedEmpty, sharedEmpty, intelMemoryRebindAttempts_))
        return false;

    ++intelMemoryRebindAttempts_;
    ReleaseIntelGpuMemoryCounters();
    const bool bound = AddIntelGpuMemoryCounters();
    if (bound)
    {
        intelMemoryRebindCooldownSamples_ = 0;
        LogMemoryDebug(L"Intel VRAM counters rebound successfully");
    }
    return bound;
}

void WindowsUsageSampler::ReleaseIntelGpuMemoryCounters() noexcept
{
    for (const auto counter : intelDedicatedMemoryCounters_)
        PdhRemoveCounter(counter);
    for (const auto counter : intelSharedMemoryCounters_)
        PdhRemoveCounter(counter);
    intelDedicatedMemoryCounters_.clear();
    intelSharedMemoryCounters_.clear();
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
    if (NeedsIntelGpuMemoryBinding(
            intelDedicatedMemoryCounters_.empty(),
            intelSharedMemoryCounters_.empty()))
        TryBindIntelGpuMemoryCounters();
    WindowsUsageTelemetry result{};
    if (primed_)
        result.cpuUsagePercent = ReadCounter(cpuCounter_, true);
    else
        primed_ = true;
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
        result.systemMemoryUsedBytes = UsedPhysicalMemory(
            memory.ullTotalPhys, memory.ullAvailPhys);
    const auto dedicated = ReadByteCounters(intelDedicatedMemoryCounters_);
    const auto shared = ReadByteCounters(intelSharedMemoryCounters_);
    result.intelGpuMemoryUsedBytes = CombineGpuMemoryBytes(dedicated, shared);
    if (result.intelGpuMemoryUsedBytes)
        intelMemoryFailureCount_ = 0;
    else if (!intelDedicatedMemoryCounters_.empty() &&
        !intelSharedMemoryCounters_.empty() &&
        ShouldReleaseIntelGpuMemoryCounters(
            ++intelMemoryFailureCount_, kIntelMemoryFailureThreshold))
    {
        ReleaseIntelGpuMemoryCounters();
        intelMemoryFailureCount_ = 0;
        intelMemoryRebindAttempts_ = 0;
        intelMemoryRebindCooldownSamples_ = 0;
        LogMemoryDebug(L"Intel VRAM counters became invalid and were released for rebind");
    }
    if (!memoryDiagnosticsLogged_)
    {
        const auto format = [](const auto& value)
        {
            return value ? std::to_wstring(*value) : std::wstring(L"unavailable");
        };
        if (result.intelGpuMemoryUsedBytes)
        {
            LogMemoryDebug(L"GPU memory dedicated first=" + format(dedicated) +
                L" shared first=" + format(shared) + L" combined=" +
                format(result.intelGpuMemoryUsedBytes));
            memoryDiagnosticsLogged_ = true;
        }
        else if (intelMemoryRebindAttempts_ >= kMaxIntelMemoryRebindAttempts ||
            (!intelDedicatedMemoryCounters_.empty() &&
                !intelSharedMemoryCounters_.empty()))
        {
            RuntimeLogger::Log(RuntimeLogLevel::Warn,
                L"Intel GPU memory telemetry unavailable after bounded counter rebind attempts");
            memoryDiagnosticsLogged_ = true;
        }
    }
    return result;
}

std::optional<std::uint64_t> UsedPhysicalMemory(
    std::uint64_t totalBytes, std::uint64_t availableBytes) noexcept
{
    if (availableBytes > totalBytes)
        return std::nullopt;
    return totalBytes - availableBytes;
}

std::optional<std::uint64_t> ReadSystemMemoryUsedBytes() noexcept
{
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory))
        return std::nullopt;
    return UsedPhysicalMemory(memory.ullTotalPhys, memory.ullAvailPhys);
}
}
