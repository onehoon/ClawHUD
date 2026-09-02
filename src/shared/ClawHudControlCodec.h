#pragma once

// ClawHUD Control IPC — protocol v1 pure codec.
//
// Encode/decode/validate only. No transport, no runtime state, no throwing on
// ordinary malformed remote input — decode returns a structured outcome
// carrying a stable ControlStatus.

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "ClawHudControlProtocol.h"

namespace clawhud::control
{
// Raw wire identity of a frame whose fixed header has fully validated. Kept so
// a protocol error discovered after the header (unknown operation, bad payload)
// can still be returned correlated to the request.
struct FrameIdentity
{
    std::uint16_t operationId{};
    std::uint32_t requestId{};
};

template <class T>
struct DecodeOutcome
{
    bool ok{};
    ControlStatus error{ControlStatus::InvalidFrame}; // meaningful only when !ok
    std::optional<FrameIdentity> identity;            // set once the header is trustworthy
    T value{};

    static DecodeOutcome Failure(ControlStatus status)
    {
        return {false, status, std::nullopt, {}};
    }
    static DecodeOutcome Failure(ControlStatus status, FrameIdentity id)
    {
        return {false, status, id, {}};
    }
    static DecodeOutcome Success(T v, FrameIdentity id)
    {
        return {true, ControlStatus::Ok, id, std::move(v)};
    }
};

using RequestDecode = DecodeOutcome<ControlRequest>;
using ResponseDecode = DecodeOutcome<ControlResponse>;

// Decode a full v1 frame. Never reads past `bytes`. Validation order matches
// the work order: header shape -> magic -> version -> headerSize -> kind ->
// requestId -> status -> payloadSize bound -> exact frame length -> known
// operation -> operation payload length -> value ranges.
RequestDecode DecodeControlRequest(std::span<const std::uint8_t> bytes);
ResponseDecode DecodeControlResponse(std::span<const std::uint8_t> bytes);

// Encode. Returns std::nullopt (never a malformed frame) when the DTO holds an
// invalid operation, out-of-range value, or over-long string.
std::optional<std::vector<std::uint8_t>> EncodeControlRequest(const ControlRequest& request);
std::optional<std::vector<std::uint8_t>> EncodeControlResponse(const ControlResponse& response);

// ---- Shared value validation (also used by callers/tests) ---------------

bool IsKnownOperation(std::uint16_t operation) noexcept;
bool IsValidVisibilityMode(std::uint8_t value) noexcept;
bool IsValidAlignment(std::uint8_t value) noexcept;
bool IsValidFont(std::uint8_t value) noexcept;
bool IsValidBackgroundMode(std::uint8_t value) noexcept;
bool IsValidIntelVrrStatus(std::uint8_t value) noexcept;
bool IsValidLaunchMode(std::uint8_t value) noexcept;
bool IsValidRuntimeState(std::uint8_t value) noexcept;
bool IsValidHudSizeOffset(std::int32_t value) noexcept;
bool IsValidOpacityPercent(std::uint16_t value) noexcept;

// UTF-8, no embedded NUL, <= kMaxStringBytes.
bool IsValidWireString(std::string_view text) noexcept;
}
