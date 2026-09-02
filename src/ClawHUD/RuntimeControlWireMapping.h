#pragma once

// CH-RTF-5 — explicit wire <-> semantic mapping and request execution.
//
// Turns a decoded clawhud::control::ControlRequest into a call on the semantic
// IRuntimeControl boundary and builds the authoritative
// clawhud::control::ControlResponse. Pure: no HWND, no threading, no framing.
// The dispatch bridge runs this on the ClawHUD main thread.

#include <string>

#include "RuntimeControl.h"
#include "ClawHudControlProtocol.h"

namespace clawhud
{
// Truthful runtime metadata for GetRuntimeInfo. CH-RTF-5 always reports
// Standalone/Ready; CH-RTF-8 replaces the launch-mode source.
struct RuntimeControlMetadata
{
    std::string applicationVersion;
    control::WireLaunchMode launchMode{control::WireLaunchMode::Standalone};
    control::WireRuntimeState runtimeState{control::WireRuntimeState::Ready};
};

// Executes one validated request against `runtimeControl` and returns the
// response. Must be called on the main thread. A request that a later
// hand-built caller left in an out-of-range state fails with InvalidValue
// rather than casting an unknown value onto the wire.
control::ControlResponse ExecuteRuntimeControlRequest(
    const control::ControlRequest& request,
    IRuntimeControl& runtimeControl,
    const RuntimeControlMetadata& metadata);
}
