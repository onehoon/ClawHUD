#include "ClawHudControlCodec.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

using namespace clawhud::control;

namespace
{
int g_failures = 0;

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

using Bytes = std::vector<std::uint8_t>;

Bytes Encode(const ControlRequest& request)
{
    auto encoded = EncodeControlRequest(request);
    Check(encoded.has_value(), "expected request to encode");
    return encoded.value_or(Bytes{});
}

Bytes Encode(const ControlResponse& response)
{
    auto encoded = EncodeControlResponse(response);
    Check(encoded.has_value(), "expected response to encode");
    return encoded.value_or(Bytes{});
}

ControlRequest BoolRequest(Operation op, bool flag, std::uint32_t id = 7)
{
    ControlRequest r;
    r.operation = op;
    r.requestId = id;
    r.flag = flag;
    return r;
}

ControlRequest EnumRequest(Operation op, std::uint8_t value, std::uint32_t id = 7)
{
    ControlRequest r;
    r.operation = op;
    r.requestId = id;
    r.wireEnum = value;
    return r;
}

ControlRequest EmptyRequest(Operation op, std::uint32_t id = 7)
{
    ControlRequest r;
    r.operation = op;
    r.requestId = id;
    return r;
}

ControlRequest SizeRequest(std::int32_t offset, std::uint32_t id = 7)
{
    ControlRequest r;
    r.operation = Operation::SetHudSizeOffset;
    r.requestId = id;
    r.sizeOffset = offset;
    return r;
}

ControlRequest OpacityRequest(Operation op, std::uint16_t percent, std::uint32_t id = 7)
{
    ControlRequest r;
    r.operation = op;
    r.requestId = id;
    r.opacityPercent = percent;
    return r;
}

// Patches a frame's payloadSize (low 2 bytes cover every legal size) and
// resizes the buffer so the frame length still matches payloadSize.
Bytes WithPayloadResized(Bytes frame, std::size_t payloadBytes)
{
    frame.resize(24 + payloadBytes, 0);
    frame[20] = static_cast<std::uint8_t>(payloadBytes & 0xFF);
    frame[21] = static_cast<std::uint8_t>((payloadBytes >> 8) & 0xFF);
    return frame;
}

WireSettingsSnapshot SampleSnapshot()
{
    WireSettingsSnapshot s;
    s.startWithWindows = true;
    s.hudEnabled = false;
    s.hudSizeOffset = -1;
    s.hudFont = static_cast<std::uint8_t>(WireFont::SegoeUiVariable);
    s.visibilityMode = static_cast<std::uint8_t>(WireVisibilityMode::InGameOnly);
    s.alignment = static_cast<std::uint8_t>(WireAlignment::Right);
    s.backgroundMode = static_cast<std::uint8_t>(WireBackgroundMode::FullWidth);
    s.backgroundOpacityPercent = 65;
    s.intelVrrRangeFixEnabled = true;
    return s;
}

// ---- 14.1 header / frame round-trip --------------------------------------

void HeaderRoundTrip()
{
    const auto frame = Encode(EmptyRequest(Operation::GetSettingsSnapshot, 4242));
    Check(frame.size() == 24, "empty request frame is header-sized");
    Check(frame[0] == 'C' && frame[1] == 'H' && frame[2] == 'U' && frame[3] == 'D',
        "magic bytes present");

    const auto decoded = DecodeControlRequest(frame);
    Check(decoded.ok, "empty request round-trips");
    Check(decoded.value.operation == Operation::GetSettingsSnapshot, "operation preserved");
    Check(decoded.value.requestId == 4242, "requestId preserved");

    ControlResponse response;
    response.operationId = static_cast<std::uint16_t>(Operation::GetSettingsSnapshot);
    response.requestId = 4242;
    response.status = ControlStatus::Ok;
    response.snapshot = SampleSnapshot();
    const auto responseFrame = Encode(response);
    const auto decodedResponse = DecodeControlResponse(responseFrame);
    Check(decodedResponse.ok, "response round-trips");
    Check(decodedResponse.value.requestId == 4242, "response requestId preserved");
    Check(decodedResponse.value.snapshot.has_value(), "response payload byte count preserved");

    // A bool setter payload byte survives.
    const auto boolFrame = Encode(BoolRequest(Operation::SetHudEnabled, true, 9));
    const auto boolDecoded = DecodeControlRequest(boolFrame);
    Check(boolDecoded.ok && boolDecoded.value.flag, "bool payload preserved");
}

// ---- 14.2 header rejection ---------------------------------------------

void HeaderRejection()
{
    const Bytes valid = Encode(EmptyRequest(Operation::GetRuntimeInfo, 3));

    Check(!DecodeControlRequest(Bytes{}).ok, "0-byte input rejected");
    Check(DecodeControlRequest(Bytes{}).error == ControlStatus::InvalidFrame,
        "0-byte input is InvalidFrame");

    Bytes truncated(valid.begin(), valid.begin() + 10);
    Check(!DecodeControlRequest(truncated).ok, "truncated header rejected");

    Bytes badMagic = valid;
    badMagic[0] = 'X';
    Check(!DecodeControlRequest(badMagic).ok, "wrong magic rejected");

    Bytes badVersion = valid;
    badVersion[4] = 2; // protocolVersion low byte
    auto badVersionResult = DecodeControlRequest(badVersion);
    Check(!badVersionResult.ok, "unsupported version rejected");
    Check(badVersionResult.error == ControlStatus::UnsupportedVersion,
        "unsupported version reports UnsupportedVersion");

    Bytes badHeaderSize = valid;
    badHeaderSize[6] = 20; // headerSize low byte
    Check(!DecodeControlRequest(badHeaderSize).ok, "wrong headerSize rejected");

    Bytes badKind = valid;
    badKind[8] = 9; // messageKind low byte
    Check(!DecodeControlRequest(badKind).ok, "invalid messageKind rejected");

    Bytes responseKind = valid;
    responseKind[8] = 2; // Response kind fed to request decoder
    Check(!DecodeControlRequest(responseKind).ok, "response kind rejected by request decoder");

    Bytes zeroId = valid;
    for (int i = 12; i < 16; ++i) zeroId[i] = 0;
    Check(!DecodeControlRequest(zeroId).ok, "requestId = 0 rejected");

    Bytes nonZeroStatus = valid;
    nonZeroStatus[16] = 1; // status low byte
    Check(!DecodeControlRequest(nonZeroStatus).ok, "request with non-zero status rejected");

    Bytes hugePayload = valid;
    hugePayload[20] = 0x00;
    hugePayload[21] = 0x00;
    hugePayload[22] = 0x01; // payloadSize = 0x00010000 = 65536 > max
    Check(!DecodeControlRequest(hugePayload).ok, "payloadSize over maximum rejected");

    Bytes payloadTooSmall = valid;
    payloadTooSmall[20] = 4; // declares 4 payload bytes, none present
    Check(!DecodeControlRequest(payloadTooSmall).ok, "payloadSize larger than actual rejected");

    Bytes trailing = valid;
    trailing.push_back(0xAA);
    Check(!DecodeControlRequest(trailing).ok, "trailing bytes after frame rejected");

    // payloadSize smaller than actual bytes: build a 1-byte bool frame but
    // declare 0 payload.
    Bytes shrunk = Encode(BoolRequest(Operation::SetHudEnabled, true, 5));
    shrunk[20] = 0;
    Check(!DecodeControlRequest(shrunk).ok, "payloadSize smaller than actual rejected");
}

// ---- 14.3 operation rejection ---------------------------------------

void OperationRejection()
{
    Bytes unknownOp = Encode(EmptyRequest(Operation::GetRuntimeInfo, 3));
    unknownOp[10] = 99; // operation low byte
    auto result = DecodeControlRequest(unknownOp);
    Check(!result.ok, "unknown operation rejected");
    Check(result.error == ControlStatus::UnknownOperation, "unknown operation reports UnknownOperation");
    Check(result.identity.has_value() && result.identity->operationId == 99 &&
        result.identity->requestId == 3,
        "unknown operation preserves frame identity");

    // Non-empty payload for an empty operation.
    Bytes emptyWithPayload = WithPayloadResized(
        Encode(EmptyRequest(Operation::GetRuntimeInfo, 3)), 1);
    auto emptyResult = DecodeControlRequest(emptyWithPayload);
    Check(!emptyResult.ok && emptyResult.error == ControlStatus::InvalidPayload,
        "non-empty payload for empty operation rejected");

    // Wrong payload size for a bool operation (2 bytes instead of 1).
    Bytes wideBool = WithPayloadResized(
        Encode(BoolRequest(Operation::SetHudEnabled, true, 3)), 2);
    auto wideBoolResult = DecodeControlRequest(wideBool);
    Check(!wideBoolResult.ok && wideBoolResult.error == ControlStatus::InvalidPayload,
        "wrong bool payload size rejected");

    // Wrong payload size for the i32 operation (2 bytes instead of 4).
    Bytes shortI32 = WithPayloadResized(Encode(SizeRequest(1, 3)), 2);
    auto shortI32Result = DecodeControlRequest(shortI32);
    Check(!shortI32Result.ok && shortI32Result.error == ControlStatus::InvalidPayload,
        "wrong i32 payload size rejected");

    // Wrong payload size for opacity (1 byte instead of 2).
    Bytes shortOpacity = WithPayloadResized(
        Encode(OpacityRequest(Operation::CommitHudOpacity, 70, 3)), 1);
    auto shortOpacityResult = DecodeControlRequest(shortOpacity);
    Check(!shortOpacityResult.ok && shortOpacityResult.error == ControlStatus::InvalidPayload,
        "wrong opacity payload size rejected");
}

// ---- 14.4 value validation ---------------------------------------

void ValueValidation()
{
    // bool
    Check(DecodeControlRequest(Encode(BoolRequest(Operation::SetHudEnabled, false))).ok,
        "bool = 0 valid");
    Check(DecodeControlRequest(Encode(BoolRequest(Operation::SetHudEnabled, true))).ok,
        "bool = 1 valid");
    {
        Bytes bad = Encode(BoolRequest(Operation::SetHudEnabled, true));
        bad[24] = 2;
        Check(!DecodeControlRequest(bad).ok, "bool = 2 invalid");
    }

    // visibility
    for (std::uint8_t v : {1, 2})
        Check(DecodeControlRequest(Encode(EnumRequest(Operation::SetHudVisibilityMode, v))).ok,
            "visibility value valid");
    Check(!EncodeControlRequest(EnumRequest(Operation::SetHudVisibilityMode, 3)).has_value(),
        "unknown visibility value rejected on encode");

    // font
    for (std::uint8_t v : {1, 2})
        Check(DecodeControlRequest(Encode(EnumRequest(Operation::SetHudFont, v))).ok,
            "font value valid");
    {
        Bytes bad = Encode(EnumRequest(Operation::SetHudFont, 1));
        bad[24] = 5;
        Check(!DecodeControlRequest(bad).ok, "unknown font value invalid");
    }

    // alignment
    for (std::uint8_t v : {1, 2, 3})
        Check(DecodeControlRequest(Encode(EnumRequest(Operation::SetHudAlignment, v))).ok,
            "alignment value valid");
    {
        Bytes bad = Encode(EnumRequest(Operation::SetHudAlignment, 1));
        bad[24] = 0;
        Check(!DecodeControlRequest(bad).ok, "unknown alignment value invalid");
    }

    // background mode
    for (std::uint8_t v : {1, 2})
        Check(DecodeControlRequest(Encode(EnumRequest(Operation::SetHudBackgroundMode, v))).ok,
            "background value valid");
    {
        Bytes bad = Encode(EnumRequest(Operation::SetHudBackgroundMode, 1));
        bad[24] = 4;
        Check(!DecodeControlRequest(bad).ok, "unknown background value invalid");
    }

    // HUD size
    for (std::int32_t offset : {-2, -1, 0, 1, 2})
    {
        ControlRequest r;
        r.operation = Operation::SetHudSizeOffset;
        r.requestId = 7;
        r.sizeOffset = offset;
        auto decoded = DecodeControlRequest(Encode(r));
        Check(decoded.ok && decoded.value.sizeOffset == offset, "HUD size in range valid");
    }
    {
        ControlRequest r;
        r.operation = Operation::SetHudSizeOffset;
        r.requestId = 7;
        r.sizeOffset = 3;
        Check(!EncodeControlRequest(r).has_value(), "HUD size out of range rejected");
    }

    // opacity
    for (std::uint16_t percent = 50; percent <= 100; percent += 5)
    {
        ControlRequest r;
        r.operation = Operation::PreviewHudOpacity;
        r.requestId = 7;
        r.opacityPercent = percent;
        auto decoded = DecodeControlRequest(Encode(r));
        Check(decoded.ok && decoded.value.opacityPercent == percent,
            "opacity 5% step round-trips");
    }
    for (std::uint16_t bad : {std::uint16_t{49}, std::uint16_t{101}, std::uint16_t{53}})
    {
        ControlRequest r;
        r.operation = Operation::CommitHudOpacity;
        r.requestId = 7;
        r.opacityPercent = bad;
        Check(!EncodeControlRequest(r).has_value(), "invalid opacity rejected on encode");
    }
    Check(IsValidOpacityPercent(50) && IsValidOpacityPercent(100), "opacity 50 and 100 valid");
    Check(!IsValidOpacityPercent(0), "opacity 0 invalid");
}

// ---- 14.5 settings snapshot round-trip -------------------------------

void SnapshotRoundTrip()
{
    ControlResponse response;
    response.operationId = static_cast<std::uint16_t>(Operation::GetSettingsSnapshot);
    response.requestId = 11;
    response.status = ControlStatus::Ok;

    // No Intel VRR result.
    response.snapshot = SampleSnapshot();
    {
        const auto decoded = DecodeControlResponse(Encode(response));
        Check(decoded.ok, "snapshot without VRR result round-trips");
        const auto& s = decoded.value.snapshot.value();
        Check(s.startWithWindows && !s.hudEnabled, "snapshot bools preserved");
        Check(s.hudSizeOffset == -1, "snapshot size offset preserved");
        Check(s.backgroundOpacityPercent == 65, "snapshot opacity preserved");
        Check(!s.intelVrrLastResult.has_value(), "no VRR result preserved");
    }

    // With Intel VRR result, exercising every status value + all strings.
    for (std::uint8_t status = 1; status <= 9; ++status)
    {
        WireSettingsSnapshot s = SampleSnapshot();
        WireIntelVrrResult r;
        r.status = status;
        r.panelName = "MSI Claw \xE2\x84\xA2"; // includes a multi-byte UTF-8 char
        r.rangeBefore = "48-120";
        r.rangeAfter = "1-120";
        r.message = "ok";
        r.timestampUtc = "2026-09-02T00:00:00Z";
        s.intelVrrLastResult = r;
        response.snapshot = s;

        const auto decoded = DecodeControlResponse(Encode(response));
        Check(decoded.ok, "snapshot with VRR result round-trips");
        const auto& out = decoded.value.snapshot.value().intelVrrLastResult.value();
        Check(out.status == status, "VRR status preserved");
        Check(out.panelName == r.panelName, "VRR panel string preserved");
        Check(out.timestampUtc == r.timestampUtc, "VRR timestamp string preserved");
    }

    // A successful mutation response carries the authoritative snapshot.
    ControlResponse mutation;
    mutation.operationId = static_cast<std::uint16_t>(Operation::SetStartWithWindows);
    mutation.requestId = 12;
    mutation.status = ControlStatus::Ok;
    mutation.snapshot = SampleSnapshot();
    const auto decodedMutation = DecodeControlResponse(Encode(mutation));
    Check(decodedMutation.ok && decodedMutation.value.snapshot.has_value(),
        "mutation response carries snapshot");

    // An error response is empty.
    ControlResponse error;
    error.operationId = static_cast<std::uint16_t>(Operation::SetStartWithWindows);
    error.requestId = 13;
    error.status = ControlStatus::OperationFailed;
    const auto errorFrame = Encode(error);
    Check(errorFrame.size() == 24, "error response has empty payload");
    const auto decodedError = DecodeControlResponse(errorFrame);
    Check(decodedError.ok && decodedError.value.status == ControlStatus::OperationFailed,
        "error response decodes with status");
    Check(!decodedError.value.snapshot.has_value(), "error response has no snapshot");

    // RequestShutdown success is empty.
    ControlResponse shutdown;
    shutdown.operationId = static_cast<std::uint16_t>(Operation::RequestShutdown);
    shutdown.requestId = 14;
    shutdown.status = ControlStatus::Ok;
    const auto shutdownFrame = Encode(shutdown);
    Check(shutdownFrame.size() == 24, "shutdown response is empty");
    Check(DecodeControlResponse(shutdownFrame).ok, "shutdown response decodes");
}

// ---- 14.6 string rejection ----------------------------------------

Bytes SnapshotResponseWithVrrStrings(const std::string& panel)
{
    WireSettingsSnapshot s = SampleSnapshot();
    WireIntelVrrResult r;
    r.status = 1;
    r.panelName = panel;
    s.intelVrrLastResult = r;
    ControlResponse response;
    response.operationId = static_cast<std::uint16_t>(Operation::GetSettingsSnapshot);
    response.requestId = 21;
    response.status = ControlStatus::Ok;
    response.snapshot = s;
    return EncodeControlResponse(response).value_or(Bytes{});
}

void StringRejection()
{
    Check(!IsValidWireString(std::string("bad\0nul", 7)), "embedded NUL rejected");
    Check(!IsValidWireString("\xC3\x28"), "malformed UTF-8 rejected");
    Check(!IsValidWireString(std::string(kMaxStringBytes + 1, 'a')),
        "over-long string rejected");
    Check(IsValidWireString("plain ascii"), "plain ascii accepted");
    Check(IsValidWireString("\xE2\x84\xA2"), "valid multi-byte accepted");

    // Encoder refuses a snapshot whose string is too long / has a NUL.
    {
        WireSettingsSnapshot s = SampleSnapshot();
        WireIntelVrrResult r;
        r.status = 1;
        r.panelName = std::string("has\0nul", 7);
        s.intelVrrLastResult = r;
        ControlResponse response;
        response.operationId = static_cast<std::uint16_t>(Operation::GetSettingsSnapshot);
        response.requestId = 21;
        response.status = ControlStatus::Ok;
        response.snapshot = s;
        Check(!EncodeControlResponse(response).has_value(),
            "encoder refuses NUL in wire string");
    }

    // Corrupt a valid frame: declare a string length beyond the payload.
    Bytes frame = SnapshotResponseWithVrrStrings("panel");
    Check(!frame.empty(), "baseline VRR-string frame built");
    // Drop the last 2 payload bytes (and match payloadSize) so a later
    // length-prefixed string reads past the end of the payload.
    if (frame.size() > 26)
    {
        Bytes shrunk = frame;
        const std::size_t newPayload = frame.size() - 24 - 2;
        shrunk.resize(frame.size() - 2);
        shrunk[20] = static_cast<std::uint8_t>(newPayload & 0xFF);
        shrunk[21] = static_cast<std::uint8_t>((newPayload >> 8) & 0xFF);
        Check(!DecodeControlResponse(shrunk).ok,
            "declared string length beyond payload rejected");
    }

    // Aggregate payload larger than the maximum.
    {
        Bytes oversized = Encode(EmptyRequest(Operation::GetSettingsSnapshot, 5));
        oversized[20] = 0x01;
        oversized[21] = 0x00;
        oversized[22] = 0x01; // 0x00010001 > kMaxPayloadBytes
        Check(!DecodeControlRequest(oversized).ok, "aggregate payload over maximum rejected");
    }
}

// ---- 14.7 runtime info round-trip -------------------------------

void RuntimeInfoRoundTrip()
{
    ControlResponse response;
    response.operationId = static_cast<std::uint16_t>(Operation::GetRuntimeInfo);
    response.requestId = 31;
    response.status = ControlStatus::Ok;

    WireRuntimeInfo info;
    info.applicationVersion = "1.2.3";
    info.minimumProtocolVersion = 1;
    info.maximumProtocolVersion = 1;
    info.launchMode = static_cast<std::uint8_t>(WireLaunchMode::Managed);
    info.runtimeState = static_cast<std::uint8_t>(WireRuntimeState::Ready);
    response.runtimeInfo = info;

    const auto decoded = DecodeControlResponse(Encode(response));
    Check(decoded.ok, "runtime info round-trips");
    const auto& out = decoded.value.runtimeInfo.value();
    Check(out.applicationVersion == "1.2.3", "version string preserved");
    Check(out.minimumProtocolVersion == 1 && out.maximumProtocolVersion == 1,
        "protocol range preserved");
    Check(out.launchMode == static_cast<std::uint8_t>(WireLaunchMode::Managed),
        "launch mode preserved");
    Check(out.runtimeState == static_cast<std::uint8_t>(WireRuntimeState::Ready),
        "runtime state preserved");

    for (std::uint8_t mode : {std::uint8_t{1}, std::uint8_t{2}})
    {
        info.launchMode = mode;
        response.runtimeInfo = info;
        Check(DecodeControlResponse(Encode(response)).ok, "valid launch mode accepted");
    }

    // Invalid launch mode / runtime state rejected on decode (patch the byte).
    {
        info.launchMode = static_cast<std::uint8_t>(WireLaunchMode::Standalone);
        info.runtimeState = static_cast<std::uint8_t>(WireRuntimeState::Ready);
        response.runtimeInfo = info;
        Bytes frame = Encode(response);
        Bytes badMode = frame;
        badMode[badMode.size() - 2] = 7; // launchMode byte
        Check(!DecodeControlResponse(badMode).ok, "invalid launch mode rejected");
        Bytes badState = frame;
        badState[badState.size() - 1] = 9; // runtimeState byte
        Check(!DecodeControlResponse(badState).ok, "invalid runtime state rejected");
    }
}

// ---- version skew: header-valid request with an unknown operation --------

void VersionSkewCorrelation()
{
    // A newer client sends a valid v1 frame carrying an operation an older
    // ClawHUD does not know, with requestId 42.
    constexpr std::uint16_t kFutureOperation = 250;
    Bytes frame = Encode(EmptyRequest(Operation::GetRuntimeInfo, 42));
    frame[10] = static_cast<std::uint8_t>(kFutureOperation & 0xFF);
    frame[11] = static_cast<std::uint8_t>((kFutureOperation >> 8) & 0xFF);

    const auto decoded = DecodeControlRequest(frame);
    Check(!decoded.ok && decoded.error == ControlStatus::UnknownOperation,
        "version-skew request decodes as UnknownOperation");
    Check(decoded.identity.has_value(), "version-skew request keeps identity");
    Check(decoded.identity->operationId == kFutureOperation &&
        decoded.identity->requestId == 42,
        "version-skew identity preserves raw operationId and requestId");

    // The server encodes a correlated error response echoing the raw operation.
    ControlResponse error;
    error.operationId = decoded.identity->operationId;
    error.requestId = decoded.identity->requestId;
    error.status = ControlStatus::UnknownOperation;
    const auto errorFrame = EncodeControlResponse(error);
    Check(errorFrame.has_value(),
        "error response for an unknown operation encodes");

    // The client decodes it: status + requestId recovered, no timeout needed.
    const auto clientView = DecodeControlResponse(errorFrame.value());
    Check(clientView.ok, "client decodes the correlated error response");
    Check(clientView.value.status == ControlStatus::UnknownOperation,
        "client sees UnknownOperation");
    Check(clientView.value.operationId == kFutureOperation &&
        clientView.value.requestId == 42,
        "client recovers raw operationId and requestId 42");

    // An Ok response must still name a known operation.
    ControlResponse bogusOk;
    bogusOk.operationId = kFutureOperation;
    bogusOk.requestId = 42;
    bogusOk.status = ControlStatus::Ok;
    Check(!EncodeControlResponse(bogusOk).has_value(),
        "Ok response with an unknown operation is refused");

    // A non-UnknownOperation status paired with an unknown operation is a
    // malformed frame on decode.
    Bytes badPair = errorFrame.value();
    badPair[16] = static_cast<std::uint8_t>(ControlStatus::OperationFailed);
    Check(!DecodeControlResponse(badPair).ok,
        "unknown operation with a non-UnknownOperation status is rejected");
}

// ---- EncodeControlResponse refuses DTOs the decoder would reject --------

void EncoderRejectsInvalidResponseDtos()
{
    const auto snapshotOp = static_cast<std::uint16_t>(Operation::GetSettingsSnapshot);
    const auto runtimeInfoOp = static_cast<std::uint16_t>(Operation::GetRuntimeInfo);
    const auto shutdownOp = static_cast<std::uint16_t>(Operation::RequestShutdown);

    WireRuntimeInfo info;
    info.applicationVersion = "1.0.0";
    info.minimumProtocolVersion = 1;
    info.maximumProtocolVersion = 1;
    info.launchMode = static_cast<std::uint8_t>(WireLaunchMode::Standalone);
    info.runtimeState = static_cast<std::uint8_t>(WireRuntimeState::Ready);

    // Ok snapshot response with no snapshot payload.
    {
        ControlResponse r;
        r.operationId = snapshotOp;
        r.requestId = 1;
        r.status = ControlStatus::Ok;
        Check(!EncodeControlResponse(r).has_value(),
            "Ok snapshot response without a snapshot is refused");
    }

    // Ok snapshot response that also carries runtime info.
    {
        ControlResponse r;
        r.operationId = snapshotOp;
        r.requestId = 1;
        r.status = ControlStatus::Ok;
        r.snapshot = SampleSnapshot();
        r.runtimeInfo = info;
        Check(!EncodeControlResponse(r).has_value(),
            "Ok snapshot response with stray runtime info is refused");
    }

    // Ok runtime-info response that also carries a snapshot.
    {
        ControlResponse r;
        r.operationId = runtimeInfoOp;
        r.requestId = 1;
        r.status = ControlStatus::Ok;
        r.runtimeInfo = info;
        r.snapshot = SampleSnapshot();
        Check(!EncodeControlResponse(r).has_value(),
            "Ok runtime-info response with a stray snapshot is refused");
    }

    // Error response carrying a payload optional.
    {
        ControlResponse r;
        r.operationId = snapshotOp;
        r.requestId = 1;
        r.status = ControlStatus::OperationFailed;
        r.snapshot = SampleSnapshot();
        Check(!EncodeControlResponse(r).has_value(),
            "error response with a snapshot is refused");
    }

    // Ok RequestShutdown carrying a payload optional.
    {
        ControlResponse r;
        r.operationId = shutdownOp;
        r.requestId = 1;
        r.status = ControlStatus::Ok;
        r.snapshot = SampleSnapshot();
        Check(!EncodeControlResponse(r).has_value(),
            "Ok shutdown response with a snapshot is refused");
    }

    // Unknown status value.
    {
        ControlResponse r;
        r.operationId = snapshotOp;
        r.requestId = 1;
        r.status = static_cast<ControlStatus>(99);
        Check(!EncodeControlResponse(r).has_value(),
            "response with an unknown status value is refused");
    }

    // Sanity: the well-formed error response still encodes and round-trips.
    {
        ControlResponse r;
        r.operationId = snapshotOp;
        r.requestId = 5;
        r.status = ControlStatus::OperationFailed;
        const auto frame = EncodeControlResponse(r);
        Check(frame.has_value(), "well-formed error response still encodes");
        Check(DecodeControlResponse(frame.value()).ok,
            "well-formed error response round-trips");
    }
}
}

int main()
{
    HeaderRoundTrip();
    HeaderRejection();
    OperationRejection();
    ValueValidation();
    SnapshotRoundTrip();
    StringRejection();
    RuntimeInfoRoundTrip();
    VersionSkewCorrelation();
    EncoderRejectsInvalidResponseDtos();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "ClawHudControlCodecTests: all checks passed\n";
    return EXIT_SUCCESS;
}
