#include "ClawHudControlCodec.h"

#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace clawhud::control
{
namespace
{
// ---- Little-endian byte writer ----------------------------------------

class Writer
{
public:
    explicit Writer(std::vector<std::uint8_t>& out) : out_(out) {}

    void U8(std::uint8_t v) { out_.push_back(v); }
    void U16(std::uint16_t v)
    {
        out_.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    }
    void U32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
    void I32(std::int32_t v) { U32(static_cast<std::uint32_t>(v)); }
    void Bytes(std::string_view s)
    {
        out_.insert(out_.end(), s.begin(), s.end());
    }
    // u16 length prefix + raw UTF-8 bytes.
    void String(std::string_view s)
    {
        U16(static_cast<std::uint16_t>(s.size()));
        Bytes(s);
    }

private:
    std::vector<std::uint8_t>& out_;
};

// ---- Little-endian byte reader (bounds-checked) ----------------------

class Reader
{
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool U8(std::uint8_t& v)
    {
        if (pos_ + 1 > bytes_.size()) return false;
        v = bytes_[pos_++];
        return true;
    }
    bool U16(std::uint16_t& v)
    {
        if (pos_ + 2 > bytes_.size()) return false;
        v = static_cast<std::uint16_t>(bytes_[pos_]) |
            static_cast<std::uint16_t>(bytes_[pos_ + 1] << 8);
        pos_ += 2;
        return true;
    }
    bool U32(std::uint32_t& v)
    {
        if (pos_ + 4 > bytes_.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(bytes_[pos_ + i]) << (8 * i);
        pos_ += 4;
        return true;
    }
    bool I32(std::int32_t& v)
    {
        std::uint32_t raw{};
        if (!U32(raw)) return false;
        v = static_cast<std::int32_t>(raw);
        return true;
    }
    // u16-prefixed UTF-8 string with full validation.
    bool String(std::string& out)
    {
        std::uint16_t length{};
        if (!U16(length)) return false;
        if (length > kMaxStringBytes) return false;
        if (pos_ + length > bytes_.size()) return false;
        std::string_view view(
            reinterpret_cast<const char*>(bytes_.data() + pos_), length);
        if (!IsValidWireString(view)) return false;
        out.assign(view);
        pos_ += length;
        return true;
    }

    std::size_t Remaining() const { return bytes_.size() - pos_; }
    bool AtEnd() const { return pos_ == bytes_.size(); }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t pos_{};
};

// ---- Operation payload classification --------------------------------

enum class RequestPayloadShape
{
    Empty,
    Bool,
    Enum,
    SizeOffset,
    Opacity,
};

RequestPayloadShape ShapeOf(Operation op)
{
    switch (op)
    {
    case Operation::GetRuntimeInfo:
    case Operation::GetSettingsSnapshot:
    case Operation::RequestShutdown:
        return RequestPayloadShape::Empty;
    case Operation::SetStartWithWindows:
    case Operation::SetHudEnabled:
    case Operation::SetIntelVrrRangeFixEnabled:
        return RequestPayloadShape::Bool;
    case Operation::SetHudVisibilityMode:
    case Operation::SetHudFont:
    case Operation::SetHudAlignment:
    case Operation::SetHudBackgroundMode:
        return RequestPayloadShape::Enum;
    case Operation::SetHudSizeOffset:
        return RequestPayloadShape::SizeOffset;
    case Operation::PreviewHudOpacity:
    case Operation::CommitHudOpacity:
        return RequestPayloadShape::Opacity;
    }
    return RequestPayloadShape::Empty;
}

bool ValidateRequestEnum(Operation op, std::uint8_t value)
{
    switch (op)
    {
    case Operation::SetHudVisibilityMode: return IsValidVisibilityMode(value);
    case Operation::SetHudFont: return IsValidFont(value);
    case Operation::SetHudAlignment: return IsValidAlignment(value);
    case Operation::SetHudBackgroundMode: return IsValidBackgroundMode(value);
    default: return false;
    }
}

// Ok responses that echo the authoritative settings snapshot.
bool ResponseCarriesSnapshot(Operation op)
{
    switch (op)
    {
    case Operation::GetSettingsSnapshot:
    case Operation::SetStartWithWindows:
    case Operation::SetHudEnabled:
    case Operation::SetHudVisibilityMode:
    case Operation::SetHudSizeOffset:
    case Operation::SetHudFont:
    case Operation::SetHudAlignment:
    case Operation::SetHudBackgroundMode:
    case Operation::PreviewHudOpacity:
    case Operation::CommitHudOpacity:
    case Operation::SetIntelVrrRangeFixEnabled:
        return true;
    default:
        return false;
    }
}

bool IsKnownStatus(std::uint32_t status)
{
    return status <= static_cast<std::uint32_t>(ControlStatus::ShuttingDown);
}

// ---- Snapshot / runtime-info payload codec ---------------------------

bool EncodeSnapshotPayload(const WireSettingsSnapshot& s, std::vector<std::uint8_t>& out)
{
    if (!IsValidHudSizeOffset(s.hudSizeOffset)) return false;
    if (!IsValidFont(s.hudFont)) return false;
    if (!IsValidVisibilityMode(s.visibilityMode)) return false;
    if (!IsValidAlignment(s.alignment)) return false;
    if (!IsValidBackgroundMode(s.backgroundMode)) return false;
    if (!IsValidOpacityPercent(s.backgroundOpacityPercent)) return false;
    if (s.intelVrrLastResult)
    {
        const auto& r = *s.intelVrrLastResult;
        if (!IsValidIntelVrrStatus(r.status)) return false;
        for (const auto* str : {&r.panelName, &r.rangeBefore, &r.rangeAfter,
                 &r.message, &r.timestampUtc})
            if (!IsValidWireString(*str)) return false;
    }

    Writer w(out);
    w.U8(s.startWithWindows ? 1 : 0);
    w.U8(s.hudEnabled ? 1 : 0);
    w.I32(s.hudSizeOffset);
    w.U8(s.hudFont);
    w.U8(s.visibilityMode);
    w.U8(s.alignment);
    w.U8(s.backgroundMode);
    w.U16(s.backgroundOpacityPercent);
    w.U8(s.intelVrrRangeFixEnabled ? 1 : 0);
    w.U8(s.intelVrrLastResult ? 1 : 0);
    if (s.intelVrrLastResult)
    {
        const auto& r = *s.intelVrrLastResult;
        w.U8(r.status);
        w.String(r.panelName);
        w.String(r.rangeBefore);
        w.String(r.rangeAfter);
        w.String(r.message);
        w.String(r.timestampUtc);
    }
    return true;
}

bool ReadBoolByte(Reader& r, bool& out)
{
    std::uint8_t b{};
    if (!r.U8(b) || b > 1) return false;
    out = b == 1;
    return true;
}

bool DecodeSnapshotPayload(Reader& r, WireSettingsSnapshot& s)
{
    std::uint8_t hasResult{};
    if (!ReadBoolByte(r, s.startWithWindows)) return false;
    if (!ReadBoolByte(r, s.hudEnabled)) return false;
    if (!r.I32(s.hudSizeOffset) || !IsValidHudSizeOffset(s.hudSizeOffset)) return false;
    if (!r.U8(s.hudFont) || !IsValidFont(s.hudFont)) return false;
    if (!r.U8(s.visibilityMode) || !IsValidVisibilityMode(s.visibilityMode)) return false;
    if (!r.U8(s.alignment) || !IsValidAlignment(s.alignment)) return false;
    if (!r.U8(s.backgroundMode) || !IsValidBackgroundMode(s.backgroundMode)) return false;
    if (!r.U16(s.backgroundOpacityPercent) ||
        !IsValidOpacityPercent(s.backgroundOpacityPercent))
        return false;
    if (!ReadBoolByte(r, s.intelVrrRangeFixEnabled)) return false;
    if (!r.U8(hasResult) || hasResult > 1) return false;
    if (hasResult == 1)
    {
        WireIntelVrrResult result;
        if (!r.U8(result.status) || !IsValidIntelVrrStatus(result.status)) return false;
        if (!r.String(result.panelName)) return false;
        if (!r.String(result.rangeBefore)) return false;
        if (!r.String(result.rangeAfter)) return false;
        if (!r.String(result.message)) return false;
        if (!r.String(result.timestampUtc)) return false;
        s.intelVrrLastResult = std::move(result);
    }
    return true;
}

bool EncodeRuntimeInfoPayload(const WireRuntimeInfo& info, std::vector<std::uint8_t>& out)
{
    if (!IsValidWireString(info.applicationVersion)) return false;
    if (!IsValidLaunchMode(info.launchMode)) return false;
    if (!IsValidRuntimeState(info.runtimeState)) return false;

    Writer w(out);
    w.String(info.applicationVersion);
    w.U16(info.minimumProtocolVersion);
    w.U16(info.maximumProtocolVersion);
    w.U8(info.launchMode);
    w.U8(info.runtimeState);
    return true;
}

bool DecodeRuntimeInfoPayload(Reader& r, WireRuntimeInfo& info)
{
    if (!r.String(info.applicationVersion)) return false;
    if (!r.U16(info.minimumProtocolVersion)) return false;
    if (!r.U16(info.maximumProtocolVersion)) return false;
    if (!r.U8(info.launchMode) || !IsValidLaunchMode(info.launchMode)) return false;
    if (!r.U8(info.runtimeState) || !IsValidRuntimeState(info.runtimeState)) return false;
    return true;
}

// ---- Header --------------------------------------------------------------

struct FrameHeader
{
    std::uint16_t protocolVersion{};
    std::uint16_t headerSize{};
    std::uint16_t messageKind{};
    std::uint16_t operation{};
    std::uint32_t requestId{};
    std::uint32_t status{};
    std::uint32_t payloadSize{};
};

// Validates the fixed header and total frame length; on success `payload`
// spans exactly payloadSize bytes.
ControlStatus ParseHeader(std::span<const std::uint8_t> bytes, MessageKind expectedKind,
    FrameHeader& header, std::span<const std::uint8_t>& payload)
{
    if (bytes.size() < kHeaderSize) return ControlStatus::InvalidFrame;
    if (std::memcmp(bytes.data(), kMagic, 4) != 0) return ControlStatus::InvalidFrame;

    Reader r(bytes.subspan(4));
    r.U16(header.protocolVersion);
    r.U16(header.headerSize);
    r.U16(header.messageKind);
    r.U16(header.operation);
    r.U32(header.requestId);
    r.U32(header.status);
    r.U32(header.payloadSize);

    if (header.protocolVersion != kProtocolVersion)
        return ControlStatus::UnsupportedVersion;
    if (header.headerSize != kHeaderSize) return ControlStatus::InvalidFrame;
    if (header.messageKind != static_cast<std::uint16_t>(expectedKind))
        return ControlStatus::InvalidFrame;
    if (header.requestId == 0) return ControlStatus::InvalidFrame;
    if (expectedKind == MessageKind::Request && header.status != 0)
        return ControlStatus::InvalidFrame;
    if (header.payloadSize > kMaxPayloadBytes) return ControlStatus::InvalidFrame;
    if (bytes.size() != static_cast<std::size_t>(kHeaderSize) + header.payloadSize)
        return ControlStatus::InvalidFrame;

    payload = bytes.subspan(kHeaderSize, header.payloadSize);
    return ControlStatus::Ok;
}

void WriteHeader(std::vector<std::uint8_t>& out, MessageKind kind,
    std::uint16_t operationId, std::uint32_t requestId, ControlStatus status,
    std::uint32_t payloadSize)
{
    Writer w(out);
    w.Bytes(std::string_view(reinterpret_cast<const char*>(kMagic), 4));
    w.U16(kProtocolVersion);
    w.U16(kHeaderSize);
    w.U16(static_cast<std::uint16_t>(kind));
    w.U16(operationId);
    w.U32(requestId);
    w.U32(static_cast<std::uint32_t>(status));
    w.U32(payloadSize);
}
}

// ---- Value validation ---------------------------------------------------

bool IsKnownOperation(std::uint16_t operation) noexcept
{
    switch (static_cast<Operation>(operation))
    {
    case Operation::GetRuntimeInfo:
    case Operation::GetSettingsSnapshot:
    case Operation::SetStartWithWindows:
    case Operation::SetHudEnabled:
    case Operation::SetHudVisibilityMode:
    case Operation::SetHudSizeOffset:
    case Operation::SetHudFont:
    case Operation::SetHudAlignment:
    case Operation::SetHudBackgroundMode:
    case Operation::PreviewHudOpacity:
    case Operation::CommitHudOpacity:
    case Operation::SetIntelVrrRangeFixEnabled:
    case Operation::RequestShutdown:
        return true;
    }
    return false;
}

bool IsValidVisibilityMode(std::uint8_t v) noexcept { return v >= 1 && v <= 2; }
bool IsValidAlignment(std::uint8_t v) noexcept { return v >= 1 && v <= 3; }
bool IsValidFont(std::uint8_t v) noexcept { return v >= 1 && v <= 2; }
bool IsValidBackgroundMode(std::uint8_t v) noexcept { return v >= 1 && v <= 2; }
bool IsValidIntelVrrStatus(std::uint8_t v) noexcept { return v >= 1 && v <= 9; }
bool IsValidLaunchMode(std::uint8_t v) noexcept { return v >= 1 && v <= 2; }
bool IsValidRuntimeState(std::uint8_t v) noexcept { return v >= 1 && v <= 3; }

bool IsValidHudSizeOffset(std::int32_t v) noexcept
{
    return v >= kMinHudSizeOffset && v <= kMaxHudSizeOffset;
}

bool IsValidOpacityPercent(std::uint16_t v) noexcept
{
    return v >= kMinOpacityPercent && v <= kMaxOpacityPercent &&
        (v % kOpacityStepPercent) == 0;
}

bool IsValidWireString(std::string_view text) noexcept
{
    if (text.size() > kMaxStringBytes) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t n = text.size();
    for (std::size_t i = 0; i < n;)
    {
        const unsigned char c = p[i];
        if (c == 0x00) return false; // no embedded NUL
        std::size_t extra = 0;
        unsigned int code = 0;
        unsigned int minCode = 0;
        if (c < 0x80) { ++i; continue; }
        else if ((c & 0xE0) == 0xC0) { extra = 1; code = c & 0x1F; minCode = 0x80; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; code = c & 0x0F; minCode = 0x800; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; code = c & 0x07; minCode = 0x10000; }
        else return false;
        if (i + extra >= n) return false;
        for (std::size_t k = 1; k <= extra; ++k)
        {
            const unsigned char cc = p[i + k];
            if ((cc & 0xC0) != 0x80) return false;
            code = (code << 6) | (cc & 0x3F);
        }
        if (code < minCode) return false;                 // overlong
        if (code > 0x10FFFF) return false;                 // out of range
        if (code >= 0xD800 && code <= 0xDFFF) return false; // surrogate
        i += extra + 1;
    }
    return true;
}

// ---- Request codec ----------------------------------------------------

std::optional<std::vector<std::uint8_t>> EncodeControlRequest(const ControlRequest& request)
{
    if (!IsKnownOperation(static_cast<std::uint16_t>(request.operation))) return std::nullopt;
    if (request.requestId == 0) return std::nullopt;

    std::vector<std::uint8_t> payload;
    switch (ShapeOf(request.operation))
    {
    case RequestPayloadShape::Empty:
        break;
    case RequestPayloadShape::Bool:
        payload.push_back(request.flag ? 1 : 0);
        break;
    case RequestPayloadShape::Enum:
        if (!ValidateRequestEnum(request.operation, request.wireEnum)) return std::nullopt;
        payload.push_back(request.wireEnum);
        break;
    case RequestPayloadShape::SizeOffset:
    {
        if (!IsValidHudSizeOffset(request.sizeOffset)) return std::nullopt;
        Writer w(payload);
        w.I32(request.sizeOffset);
        break;
    }
    case RequestPayloadShape::Opacity:
    {
        if (!IsValidOpacityPercent(request.opacityPercent)) return std::nullopt;
        Writer w(payload);
        w.U16(request.opacityPercent);
        break;
    }
    }

    std::vector<std::uint8_t> frame;
    WriteHeader(frame, MessageKind::Request,
        static_cast<std::uint16_t>(request.operation), request.requestId,
        ControlStatus::Ok, static_cast<std::uint32_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

RequestDecode DecodeControlRequest(std::span<const std::uint8_t> bytes)
{
    FrameHeader header;
    std::span<const std::uint8_t> payload;
    const ControlStatus headerStatus =
        ParseHeader(bytes, MessageKind::Request, header, payload);
    if (headerStatus != ControlStatus::Ok)
        return RequestDecode::Failure(headerStatus);

    // Header validated: from here every failure stays correlated to the request.
    const FrameIdentity id{header.operation, header.requestId};

    if (!IsKnownOperation(header.operation))
        return RequestDecode::Failure(ControlStatus::UnknownOperation, id);

    ControlRequest request;
    request.operation = static_cast<Operation>(header.operation);
    request.requestId = header.requestId;

    Reader r(payload);
    switch (ShapeOf(request.operation))
    {
    case RequestPayloadShape::Empty:
        if (payload.size() != 0) return RequestDecode::Failure(ControlStatus::InvalidPayload, id);
        break;
    case RequestPayloadShape::Bool:
    {
        if (payload.size() != 1) return RequestDecode::Failure(ControlStatus::InvalidPayload, id);
        if (!ReadBoolByte(r, request.flag))
            return RequestDecode::Failure(ControlStatus::InvalidValue, id);
        break;
    }
    case RequestPayloadShape::Enum:
    {
        if (payload.size() != 1) return RequestDecode::Failure(ControlStatus::InvalidPayload, id);
        r.U8(request.wireEnum);
        if (!ValidateRequestEnum(request.operation, request.wireEnum))
            return RequestDecode::Failure(ControlStatus::InvalidValue, id);
        break;
    }
    case RequestPayloadShape::SizeOffset:
    {
        if (payload.size() != 4) return RequestDecode::Failure(ControlStatus::InvalidPayload, id);
        r.I32(request.sizeOffset);
        if (!IsValidHudSizeOffset(request.sizeOffset))
            return RequestDecode::Failure(ControlStatus::InvalidValue, id);
        break;
    }
    case RequestPayloadShape::Opacity:
    {
        if (payload.size() != 2) return RequestDecode::Failure(ControlStatus::InvalidPayload, id);
        r.U16(request.opacityPercent);
        if (!IsValidOpacityPercent(request.opacityPercent))
            return RequestDecode::Failure(ControlStatus::InvalidValue, id);
        break;
    }
    }

    return RequestDecode::Success(std::move(request), id);
}

// ---- Response codec --------------------------------------------------

std::optional<std::vector<std::uint8_t>> EncodeControlResponse(const ControlResponse& response)
{
    if (response.requestId == 0) return std::nullopt;

    const bool knownOperation = IsKnownOperation(response.operationId);

    std::vector<std::uint8_t> payload;
    if (response.status == ControlStatus::Ok)
    {
        // A successful response must name a known v1 operation.
        if (!knownOperation) return std::nullopt;
        const auto operation = static_cast<Operation>(response.operationId);
        if (operation == Operation::GetRuntimeInfo)
        {
            if (!response.runtimeInfo) return std::nullopt;
            if (!EncodeRuntimeInfoPayload(*response.runtimeInfo, payload)) return std::nullopt;
        }
        else if (ResponseCarriesSnapshot(operation))
        {
            if (!response.snapshot) return std::nullopt;
            if (!EncodeSnapshotPayload(*response.snapshot, payload)) return std::nullopt;
        }
        else if (operation == Operation::RequestShutdown)
        {
            // empty successful response
        }
        else
        {
            return std::nullopt;
        }
    }
    // Non-Ok responses always carry an empty payload in v1, and may echo an
    // unknown raw operation id (e.g. UnknownOperation to a version-skewed client).

    if (payload.size() > kMaxPayloadBytes) return std::nullopt;

    std::vector<std::uint8_t> frame;
    WriteHeader(frame, MessageKind::Response, response.operationId, response.requestId,
        response.status, static_cast<std::uint32_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

ResponseDecode DecodeControlResponse(std::span<const std::uint8_t> bytes)
{
    FrameHeader header;
    std::span<const std::uint8_t> payload;
    const ControlStatus headerStatus =
        ParseHeader(bytes, MessageKind::Response, header, payload);
    if (headerStatus != ControlStatus::Ok)
        return ResponseDecode::Failure(headerStatus);

    const FrameIdentity id{header.operation, header.requestId};

    if (!IsKnownStatus(header.status))
        return ResponseDecode::Failure(ControlStatus::InvalidFrame, id);

    const auto status = static_cast<ControlStatus>(header.status);
    const bool knownOperation = IsKnownOperation(header.operation);
    // An unknown operation is only meaningful on an UnknownOperation error
    // (the version-skew case). A successful response must name a known op;
    // any other status paired with an unknown op is a malformed frame.
    if (status == ControlStatus::Ok && !knownOperation)
        return ResponseDecode::Failure(ControlStatus::UnknownOperation, id);
    if (!knownOperation && status != ControlStatus::UnknownOperation)
        return ResponseDecode::Failure(ControlStatus::InvalidFrame, id);

    ControlResponse response;
    response.operationId = header.operation;
    response.requestId = header.requestId;
    response.status = status;

    Reader r(payload);
    if (status != ControlStatus::Ok)
    {
        if (payload.size() != 0) return ResponseDecode::Failure(ControlStatus::InvalidPayload, id);
        return ResponseDecode::Success(std::move(response), id);
    }

    const auto operation = static_cast<Operation>(header.operation);
    if (operation == Operation::GetRuntimeInfo)
    {
        WireRuntimeInfo info;
        if (!DecodeRuntimeInfoPayload(r, info) || !r.AtEnd())
            return ResponseDecode::Failure(ControlStatus::InvalidPayload, id);
        response.runtimeInfo = std::move(info);
    }
    else if (ResponseCarriesSnapshot(operation))
    {
        WireSettingsSnapshot snapshot;
        if (!DecodeSnapshotPayload(r, snapshot) || !r.AtEnd())
            return ResponseDecode::Failure(ControlStatus::InvalidPayload, id);
        response.snapshot = std::move(snapshot);
    }
    else if (operation == Operation::RequestShutdown)
    {
        if (payload.size() != 0) return ResponseDecode::Failure(ControlStatus::InvalidPayload, id);
    }
    else
    {
        return ResponseDecode::Failure(ControlStatus::InvalidFrame, id);
    }

    return ResponseDecode::Success(std::move(response), id);
}
}
