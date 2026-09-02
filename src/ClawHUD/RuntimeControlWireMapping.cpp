#include "RuntimeControlWireMapping.h"

#include <optional>

#include "HudModel.h"
#include "ClawHudControlCodec.h"
#include "Tweaks/IntelVrr/IntelVrrRunResult.h"

namespace clawhud
{
namespace
{
namespace ctl = clawhud::control;

ctl::ControlResponse StatusResponse(const ctl::ControlRequest& request, ctl::ControlStatus status)
{
    ctl::ControlResponse response;
    response.operationId = static_cast<std::uint16_t>(request.operation);
    response.requestId = request.requestId;
    response.status = status;
    return response;
}

// ---- wire enum -> semantic (explicit) ----------------------------------

std::optional<HudVisibilityMode> ToVisibilityMode(std::uint8_t wire)
{
    switch (static_cast<ctl::WireVisibilityMode>(wire))
    {
    case ctl::WireVisibilityMode::Always: return HudVisibilityMode::Always;
    case ctl::WireVisibilityMode::InGameOnly: return HudVisibilityMode::InGameOnly;
    }
    return std::nullopt;
}

std::optional<HudAlignment> ToAlignment(std::uint8_t wire)
{
    switch (static_cast<ctl::WireAlignment>(wire))
    {
    case ctl::WireAlignment::Left: return HudAlignment::Left;
    case ctl::WireAlignment::Center: return HudAlignment::Center;
    case ctl::WireAlignment::Right: return HudAlignment::Right;
    }
    return std::nullopt;
}

std::optional<HudFont> ToFont(std::uint8_t wire)
{
    switch (static_cast<ctl::WireFont>(wire))
    {
    case ctl::WireFont::Unispace: return HudFont::Unispace;
    case ctl::WireFont::SegoeUiVariable: return HudFont::SegoeUiVariable;
    }
    return std::nullopt;
}

std::optional<HudBackgroundMode> ToBackgroundMode(std::uint8_t wire)
{
    switch (static_cast<ctl::WireBackgroundMode>(wire))
    {
    case ctl::WireBackgroundMode::FullWidth: return HudBackgroundMode::FullWidth;
    case ctl::WireBackgroundMode::ContentWidth: return HudBackgroundMode::ContentWidth;
    }
    return std::nullopt;
}

// ---- semantic -> wire enum (explicit) --------------------------------

std::uint8_t FromVisibilityMode(HudVisibilityMode mode)
{
    switch (mode)
    {
    case HudVisibilityMode::Always: return static_cast<std::uint8_t>(ctl::WireVisibilityMode::Always);
    case HudVisibilityMode::InGameOnly:
        return static_cast<std::uint8_t>(ctl::WireVisibilityMode::InGameOnly);
    }
    return 0;
}

std::uint8_t FromAlignment(HudAlignment alignment)
{
    switch (alignment)
    {
    case HudAlignment::Left: return static_cast<std::uint8_t>(ctl::WireAlignment::Left);
    case HudAlignment::Center: return static_cast<std::uint8_t>(ctl::WireAlignment::Center);
    case HudAlignment::Right: return static_cast<std::uint8_t>(ctl::WireAlignment::Right);
    }
    return 0;
}

std::uint8_t FromFont(HudFont font)
{
    switch (font)
    {
    case HudFont::Unispace: return static_cast<std::uint8_t>(ctl::WireFont::Unispace);
    case HudFont::SegoeUiVariable: return static_cast<std::uint8_t>(ctl::WireFont::SegoeUiVariable);
    }
    return 0;
}

std::uint8_t FromBackgroundMode(HudBackgroundMode mode)
{
    switch (mode)
    {
    case HudBackgroundMode::FullWidth:
        return static_cast<std::uint8_t>(ctl::WireBackgroundMode::FullWidth);
    case HudBackgroundMode::ContentWidth:
        return static_cast<std::uint8_t>(ctl::WireBackgroundMode::ContentWidth);
    }
    return 0;
}

std::optional<std::uint8_t> FromIntelVrrStatus(IntelVrrRunStatus status)
{
    switch (status)
    {
    case IntelVrrRunStatus::Disabled: return static_cast<std::uint8_t>(ctl::WireIntelVrrStatus::Disabled);
    case IntelVrrRunStatus::Unavailable:
        return static_cast<std::uint8_t>(ctl::WireIntelVrrStatus::Unavailable);
    case IntelVrrRunStatus::UnsupportedPanel:
        return static_cast<std::uint8_t>(ctl::WireIntelVrrStatus::UnsupportedPanel);
    case IntelVrrRunStatus::AmbiguousDisplay:
        return static_cast<std::uint8_t>(ctl::WireIntelVrrStatus::AmbiguousDisplay);
    case IntelVrrRunStatus::AlreadyCorrect:
        return static_cast<std::uint8_t>(ctl::WireIntelVrrStatus::AlreadyCorrect);
    case IntelVrrRunStatus::SkippedUserProfile:
        return static_cast<std::uint8_t>(ctl::WireIntelVrrStatus::SkippedUserProfile);
    case IntelVrrRunStatus::Applied: return static_cast<std::uint8_t>(ctl::WireIntelVrrStatus::Applied);
    case IntelVrrRunStatus::ApplyFailed:
        return static_cast<std::uint8_t>(ctl::WireIntelVrrStatus::ApplyFailed);
    case IntelVrrRunStatus::VerificationFailed:
        return static_cast<std::uint8_t>(ctl::WireIntelVrrStatus::VerificationFailed);
    }
    return std::nullopt;
}

// ---- semantic snapshot -> wire snapshot ------------------------------

std::optional<ctl::WireSettingsSnapshot> ToWireSnapshot(const RuntimeSettingsSnapshot& snapshot)
{
    ctl::WireSettingsSnapshot wire;
    wire.startWithWindows = snapshot.startWithWindows;
    wire.hudEnabled = snapshot.hudEnabled;
    wire.hudSizeOffset = snapshot.hudSizeOffset;
    wire.hudFont = FromFont(snapshot.hudFont);
    wire.visibilityMode = FromVisibilityMode(snapshot.hudOptions.visibilityMode);
    wire.alignment = FromAlignment(snapshot.hudOptions.alignment);
    wire.backgroundMode = FromBackgroundMode(snapshot.hudOptions.backgroundMode);
    wire.backgroundOpacityPercent = static_cast<std::uint16_t>(clawhud::ClampHudOpacityPercent(
        clawhud::HudOpacityPercentFromFraction(snapshot.hudOptions.backgroundOpacity)));
    wire.intelVrrRangeFixEnabled = snapshot.intelVrrRangeFixEnabled;

    if (!ctl::IsValidFont(wire.hudFont) || !ctl::IsValidVisibilityMode(wire.visibilityMode) ||
        !ctl::IsValidAlignment(wire.alignment) || !ctl::IsValidBackgroundMode(wire.backgroundMode) ||
        !ctl::IsValidOpacityPercent(wire.backgroundOpacityPercent) ||
        !ctl::IsValidHudSizeOffset(wire.hudSizeOffset))
        return std::nullopt;

    if (snapshot.intelVrrLastResult)
    {
        const auto& result = *snapshot.intelVrrLastResult;
        const auto wireStatus = FromIntelVrrStatus(result.status);
        if (!wireStatus) return std::nullopt;
        ctl::WireIntelVrrResult wireResult;
        wireResult.status = *wireStatus;
        wireResult.panelName = result.panelName;
        wireResult.rangeBefore = result.rangeBefore;
        wireResult.rangeAfter = result.rangeAfter;
        wireResult.message = result.message;
        wireResult.timestampUtc = result.timestampUtc;
        for (const auto* text : {&wireResult.panelName, &wireResult.rangeBefore,
                 &wireResult.rangeAfter, &wireResult.message, &wireResult.timestampUtc})
            if (!ctl::IsValidWireString(*text)) return std::nullopt;
        wire.intelVrrLastResult = std::move(wireResult);
    }
    return wire;
}

// A fresh authoritative snapshot response after a successful mutation.
ctl::ControlResponse SnapshotResponse(const ctl::ControlRequest& request, IRuntimeControl& rc)
{
    auto wire = ToWireSnapshot(rc.GetSettingsSnapshot());
    if (!wire) return StatusResponse(request, ctl::ControlStatus::OperationFailed);
    auto response = StatusResponse(request, ctl::ControlStatus::Ok);
    response.snapshot = std::move(*wire);
    return response;
}
}

control::ControlResponse ExecuteRuntimeControlRequest(
    const control::ControlRequest& request, IRuntimeControl& rc,
    const RuntimeControlMetadata& metadata)
{
    using ctl::ControlStatus;
    using ctl::Operation;

    switch (request.operation)
    {
    case Operation::GetRuntimeInfo:
    {
        ctl::WireRuntimeInfo info;
        info.applicationVersion = metadata.applicationVersion;
        info.minimumProtocolVersion = ctl::kProtocolVersion;
        info.maximumProtocolVersion = ctl::kProtocolVersion;
        info.launchMode = static_cast<std::uint8_t>(metadata.launchMode);
        info.runtimeState = static_cast<std::uint8_t>(metadata.runtimeState);
        if (!ctl::IsValidWireString(info.applicationVersion) ||
            !ctl::IsValidLaunchMode(info.launchMode) ||
            !ctl::IsValidRuntimeState(info.runtimeState))
            return StatusResponse(request, ControlStatus::OperationFailed);
        auto response = StatusResponse(request, ControlStatus::Ok);
        response.runtimeInfo = std::move(info);
        return response;
    }
    case Operation::GetSettingsSnapshot:
        return SnapshotResponse(request, rc);

    case Operation::SetStartWithWindows:
        rc.SetStartWithWindows(request.flag);
        return SnapshotResponse(request, rc);

    case Operation::SetHudEnabled:
        if (!rc.SetHudEnabled(request.flag))
            return StatusResponse(request, ControlStatus::OperationFailed);
        return SnapshotResponse(request, rc);

    case Operation::SetHudVisibilityMode:
    {
        const auto mode = ToVisibilityMode(request.wireEnum);
        if (!mode) return StatusResponse(request, ControlStatus::InvalidValue);
        rc.SetHudVisibilityMode(*mode);
        return SnapshotResponse(request, rc);
    }
    case Operation::SetHudSizeOffset:
        if (!ctl::IsValidHudSizeOffset(request.sizeOffset))
            return StatusResponse(request, ControlStatus::InvalidValue);
        rc.SetHudSizeOffset(request.sizeOffset);
        return SnapshotResponse(request, rc);

    case Operation::SetHudFont:
    {
        const auto font = ToFont(request.wireEnum);
        if (!font) return StatusResponse(request, ControlStatus::InvalidValue);
        rc.SetHudFont(*font);
        return SnapshotResponse(request, rc);
    }
    case Operation::SetHudAlignment:
    {
        const auto alignment = ToAlignment(request.wireEnum);
        if (!alignment) return StatusResponse(request, ControlStatus::InvalidValue);
        rc.SetHudAlignment(*alignment);
        return SnapshotResponse(request, rc);
    }
    case Operation::SetHudBackgroundMode:
    {
        const auto mode = ToBackgroundMode(request.wireEnum);
        if (!mode) return StatusResponse(request, ControlStatus::InvalidValue);
        rc.SetHudBackgroundMode(*mode);
        return SnapshotResponse(request, rc);
    }
    case Operation::PreviewHudOpacity:
        if (!ctl::IsValidOpacityPercent(request.opacityPercent))
            return StatusResponse(request, ControlStatus::InvalidValue);
        if (!rc.PreviewHudOpacity(request.opacityPercent / 100.0f))
            return StatusResponse(request, ControlStatus::OperationFailed);
        return SnapshotResponse(request, rc);

    case Operation::CommitHudOpacity:
        if (!ctl::IsValidOpacityPercent(request.opacityPercent))
            return StatusResponse(request, ControlStatus::InvalidValue);
        if (!rc.CommitHudOpacity(request.opacityPercent / 100.0f))
            return StatusResponse(request, ControlStatus::OperationFailed);
        return SnapshotResponse(request, rc);

    case Operation::SetIntelVrrRangeFixEnabled:
        rc.SetIntelVrrRangeFixEnabled(request.flag);
        return SnapshotResponse(request, rc);

    case Operation::RequestShutdown:
        // CH-RTF-5 does not connect shutdown to App::Exit(); the
        // response-before-exit lifecycle lands with the mutation pipe in CH-RTF-7.
        return StatusResponse(request, ControlStatus::RuntimeUnavailable);
    }

    return StatusResponse(request, ControlStatus::UnknownOperation);
}
}
