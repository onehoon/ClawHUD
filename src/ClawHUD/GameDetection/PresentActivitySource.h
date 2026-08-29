#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace clawhud
{
struct PresentActivitySchema
{
    std::optional<std::size_t> application;
    std::optional<std::size_t> processId;
    std::optional<std::size_t> qpcTimeMs;
    std::optional<std::size_t> presentMode;
    std::optional<std::size_t> frameType;
    std::optional<std::size_t> swapChainAddress;
    std::optional<std::size_t> msBetweenDisplayChange;

    bool HasRequiredColumns() const noexcept
    {
        return application && processId && qpcTimeMs;
    }
};

struct PresentActivitySample
{
    DWORD processId{};
    std::string application;
    double qpcTimeMs{};
    std::optional<bool> displayed;
    std::string presentMode;
    std::string frameType;
    std::string swapChainAddress;
};

struct PresentActivitySummary
{
    DWORD processId{};
    std::string application;
    double firstQpcMs{};
    double lastQpcMs{};
    std::uint32_t presentCount{};
    std::uint32_t displayedCount{};
    bool displayCountAvailable{};
    std::string presentMode;
    std::string frameType;
    std::string swapChainAddress;
};

PresentActivitySchema ParsePresentActivitySchema(
    const std::vector<std::string>& headers);
std::optional<PresentActivitySample> ParsePresentActivityRow(
    const PresentActivitySchema& schema, const std::vector<std::string>& row);
std::wstring BuildPresentActivityCommandLine(const std::wstring& executable,
    const std::wstring& sessionName);
std::wstring EscapePresentActivityValue(std::string_view value);

class PresentActivityAggregator
{
public:
    std::vector<PresentActivitySummary> Consume(const PresentActivitySample& sample);
    std::size_t Size() const noexcept { return accumulators_.size(); }

private:
    struct Accumulator
    {
        PresentActivitySummary summary;
    };

    std::unordered_map<DWORD, Accumulator> accumulators_;
};

class PresentActivitySource
{
public:
    PresentActivitySource() = default;
    ~PresentActivitySource();

    PresentActivitySource(const PresentActivitySource&) = delete;
    PresentActivitySource& operator=(const PresentActivitySource&) = delete;

    bool Start(const std::wstring& executable);
    void Stop() noexcept;
    bool Running() const noexcept
    {
        return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }
    const std::wstring& SessionName() const noexcept { return sessionName_; }

private:
    void ReadLoop() noexcept;
    void LogSummary(const PresentActivitySummary& summary) noexcept;

    HANDLE process_{};
    HANDLE output_{};
    std::thread worker_;
    std::atomic_bool stop_{};
    std::atomic_uint64_t nextSequence_{1};
    std::wstring sessionName_;
};
}
