#pragma once

#include <windows.h>

#include "PresentMonApi2Api.h"

#include <cstddef>
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

// Per-row blob stride PresentMon 2.5.1 uses: PadToAlignment(cursor, 16). The
// unaligned max element end (24 for three 8-byte fields) misdecodes row 1+ and
// under-allocates the caller buffer for multi-swap-chain polls.
std::size_t Api2AlignedRowBytes(const std::vector<PM_QUERY_ELEMENT>& elements) noexcept;

// Typed decode of one blob row. Nullopt = field absent / not finite / zero addr.
std::optional<std::uint64_t> Api2DecodeAddress(const std::uint8_t* row, const PM_QUERY_ELEMENT& element);
std::optional<double> Api2DecodeFps(const std::uint8_t* row, const PM_QUERY_ELEMENT& element);
std::optional<DWORD> Api2DecodeProcessId(const std::uint8_t* row, const PM_QUERY_ELEMENT& element) noexcept;
// String form (decimal or "null"), used for the raw JSON and decode tests.
std::string Api2DecodeValue(const std::uint8_t* blob, const PM_QUERY_ELEMENT& element);

// One decoded swap-chain row.
struct Api2SwapChainRow
{
    std::optional<DWORD> processId;   // PM_METRIC_PROCESS_ID, when the query carries it
    bool pidMismatch{};               // returned PID != the PID we polled for
    std::optional<std::uint64_t> address;
    std::optional<double> displayedFps;
    std::optional<double> presentedFps;
};

// Derived milestone signals for a poll's rows. Rows whose returned PID does not
// match the polled PID are excluded. swapChainCount only counts rows carrying a
// non-null SWAP_CHAIN_ADDRESS: PresentMon 2.5.1 returns one null-address probe
// row before it has observed a real swap chain, and that row is not evidence.
struct Api2RowAggregate
{
    std::uint32_t swapChainCount{};
    bool anyDisplayedFpsPositive{};
    bool anyPresentedFpsPositive{};
};
Api2RowAggregate Api2AggregateRows(const std::vector<Api2SwapChainRow>& rows, DWORD requestedPid) noexcept;

// Compose the api2 record body from a completed poll. Split out so the
// in/out capacity + status contract can be unit-tested without a live loader.
// pollRowCount = raw *numSwapChains from API2; swapChainCount = matched rows
// that carry a non-null SWAP_CHAIN_ADDRESS.
std::string Api2ComposeSample(PM_STATUS pollStatus, std::uint32_t pollRowCount,
    std::uint32_t swapChainCount, const std::vector<Api2SwapChainRow>& rows,
    bool pidValidationAvailable, std::string trackStatusJson,
    std::string trackStatusCodeJson);

// Structured sample: JSONL body plus the derived milestone signals, aggregated
// across every matched swap-chain row so the per-PID summary stays correct for
// a multi-swap-chain process (the caller never reparses the serialized JSON).
struct Api2SampleResult
{
    std::string json;
    bool pollSucceeded{};
    std::uint32_t pollRowCount{};    // raw *numSwapChains returned by API2
    std::uint32_t swapChainCount{};  // matched rows with a non-null SWAP_CHAIN_ADDRESS
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
