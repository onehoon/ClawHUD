#pragma once

// CH-RTF-7 — internal result of executing one Control request on the main
// thread. Process-internal: `shutdownAfterResponse` is NOT serialized and is
// not part of the wire contract.

#include "ClawHudControlProtocol.h"

namespace clawhud
{
struct RuntimeControlExecutionResult
{
    control::ControlResponse response;

    // True only for a successfully approved RequestShutdown. The pipe worker
    // must deliver `response` first, then post the shutdown-ready message; it
    // must never call App::Exit() directly. Every other operation, and every
    // terminal bridge failure (ShuttingDown / RuntimeUnavailable), is false.
    bool shutdownAfterResponse{};
};
}
