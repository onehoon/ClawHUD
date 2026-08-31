#pragma once

#include <windows.h>

#include "PresentMonApi2Api.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Human-readable name for a PresentMon API2 status, so raw evidence keeps the
// exact PM_STATUS instead of a diagnostic-owned label.
std::string_view Api2StatusName(PM_STATUS status) noexcept;

// Typed decode of one blob row. Nullopt = field absent / not finite / zero addr.
std::optional<std::uint64_t> Api2DecodeAddress(const std::uint8_t* row, const PM_QUERY_ELEMENT& element);
std::optional<double> Api2DecodeFps(const std::uint8_t* row, const PM_QUERY_ELEMENT& element);
// String form (decimal or "null"), used for the raw JSON and decode tests.
std::string Api2DecodeValue(const std::uint8_t* blob, const PM_QUERY_ELEMENT& element);

// One decoded swap-chain row.
struct Api2SwapChainRow
{
    std::optional<std::uint64_t> address;
    std::optional<double> displayedFps;
    std::optional<double> presentedFps;
};

// Compose the api2 record body from a completed poll. Split out so the
// in/out capacity + status contract can be unit-tested without a live loader.
std::string Api2ComposeSample(PM_STATUS pollStatus, std::uint32_t swapChainCount,
    const std::vector<Api2SwapChainRow>& rows, std::string trackStatusJson,
    std::string trackStatusCodeJson);

// Structured sample: JSONL body plus the derived milestone signals, aggregated
// across every swap-chain row so the per-PID summary stays correct for a
// multi-swap-chain process (the caller never reparses the serialized JSON).
struct Api2SampleResult
{
    std::string json;
    bool pollSucceeded{};
    std::uint32_t swapChainCount{};
    bool anySwapChainAddress{};
    bool anyDisplayedFpsPositive{};
    bool anyPresentedFpsPositive{};
};

class Api2Evidence
{
public:
    // PresentMon 2.5.1 treats an input *numSwapChains == 0 as PM_STATUS_BAD_ARGUMENT,
    // so every poll passes this capacity and the blob holds this many result rows.
    static constexpr std::uint32_t kSwapChainCapacity = 16;

    Api2Evidence() noexcept;
    ~Api2Evidence();  // out-of-line: State is incomplete here

    bool Start(std::string& detail) noexcept;
    void Stop() noexcept;
    void Retire(DWORD processId) noexcept;
    Api2SampleResult Sample(DWORD processId) noexcept;

private:
    struct State;
    void StopLocked() noexcept;  // mutex_ held by caller

    // Sample() runs on the API2 sampler thread; Retire()/Start()/Stop() are
    // reached from WinEvent, PDH, and main threads. Serialize all API2 state.
    std::mutex mutex_;
    std::unique_ptr<State> state_;
};
