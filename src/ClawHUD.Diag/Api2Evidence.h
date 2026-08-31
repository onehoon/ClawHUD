#pragma once

#include <windows.h>

#include "PresentMonApi2Api.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

std::string Api2DecodeValue(const std::uint8_t* blob, const PM_QUERY_ELEMENT& element);

// Human-readable name for a PresentMon API2 status, so raw evidence keeps the
// exact PM_STATUS instead of a diagnostic-owned label.
std::string_view Api2StatusName(PM_STATUS status) noexcept;

// One decoded swap-chain row.
struct Api2SwapChainRow
{
    std::string address{"null"};
    std::string displayedFps{"null"};
    std::string presentedFps{"null"};
};

// Compose the api2 record body from a completed poll. Split out so the
// in/out capacity + status contract can be unit-tested without a live loader.
std::string Api2ComposeSample(PM_STATUS pollStatus, std::uint32_t swapChainCount,
    const std::vector<Api2SwapChainRow>& rows, std::string trackStatusJson,
    std::string trackStatusCodeJson);

class Api2Evidence
{
public:
    // PresentMon 2.5.1 treats an input *numSwapChains == 0 as PM_STATUS_BAD_ARGUMENT,
    // so every poll passes this capacity and the blob holds this many result rows.
    static constexpr std::uint32_t kSwapChainCapacity = 16;

    bool Start(std::string& detail) noexcept;
    void Stop() noexcept;
    void Retire(DWORD processId) noexcept;
    std::string Sample(DWORD processId) noexcept;

private:
    struct State;
    State* state_{};
};
