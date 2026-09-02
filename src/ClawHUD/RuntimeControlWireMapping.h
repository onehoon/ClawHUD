#pragma once

// CH-RTF-5 — explicit wire <-> semantic mapping and request execution.
//
// Turns a decoded clawhud::control::ControlRequest into a call on the semantic
// IRuntimeControl boundary and builds the authoritative
// clawhud::control::ControlResponse. Pure: no HWND, no threading, no framing.
// The dispatch bridge runs this on the ClawHUD main thread.

#include <string>

#include "RuntimeControl.h"
#include "RuntimeControlExecutionResult.h"
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

// Executes one validated request against `runtimeControl` on the main thread.
// Returns the authoritative response plus, for a successfully approved
// RequestShutdown only, shutdownAfterResponse = true (main-thread approval of
// the shutdown request; the caller must deliver the response before starting
// teardown). A request a later hand-built caller left out of range fails with
// InvalidValue rather than casting an unknown value onto the wire.
RuntimeControlExecutionResult ExecuteRuntimeControlRequest(
    const control::ControlRequest& request,
    IRuntimeControl& runtimeControl,
    const RuntimeControlMetadata& metadata);
}
