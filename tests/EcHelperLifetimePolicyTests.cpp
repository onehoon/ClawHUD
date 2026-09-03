#include "EcHelperLifetimePolicy.h"

#include <cassert>
#include <string>

// Cleanup 1 (work order 9.3): the sampling-stop -> EC helper lifetime mapping is
// a pure decision. Transient gates preserve a healthy elevated helper; explicit
// boundaries release it. No reason-string matching anywhere.

using clawhud::EcHelperLifetime;
using clawhud::EcHelperLifetimeForStop;
using clawhud::SamplingStopCause;
using clawhud::SamplingStopCauseReason;

static_assert(EcHelperLifetimeForStop(SamplingStopCause::HudHidden) ==
    EcHelperLifetime::Preserve);
static_assert(EcHelperLifetimeForStop(SamplingStopCause::Suspend) ==
    EcHelperLifetime::Preserve);
static_assert(EcHelperLifetimeForStop(SamplingStopCause::HudDisabled) ==
    EcHelperLifetime::Release);
static_assert(EcHelperLifetimeForStop(SamplingStopCause::AppShutdown) ==
    EcHelperLifetime::Release);

int main()
{
    // Transient sampling stops keep the helper.
    assert(EcHelperLifetimeForStop(SamplingStopCause::HudHidden) ==
        EcHelperLifetime::Preserve);
    assert(EcHelperLifetimeForStop(SamplingStopCause::Suspend) ==
        EcHelperLifetime::Preserve);

    // Explicit lifetime boundaries release the elevated helper.
    assert(EcHelperLifetimeForStop(SamplingStopCause::HudDisabled) ==
        EcHelperLifetime::Release);
    assert(EcHelperLifetimeForStop(SamplingStopCause::AppShutdown) ==
        EcHelperLifetime::Release);

    // Reason text is derived from the cause, never parsed back into policy.
    assert(std::wstring(SamplingStopCauseReason(SamplingStopCause::HudHidden)) ==
        L"hud-hidden");
    assert(std::wstring(SamplingStopCauseReason(SamplingStopCause::Suspend)) ==
        L"suspend");
    assert(std::wstring(SamplingStopCauseReason(SamplingStopCause::HudDisabled)) ==
        L"hud-disabled");
    assert(std::wstring(SamplingStopCauseReason(SamplingStopCause::AppShutdown)) ==
        L"app-shutdown");
    return 0;
}
