#pragma once

namespace clawhud
{
// Why a production sampling stop happened. This is policy input; the matching
// log text is derived from it, never parsed back out of a string.
enum class SamplingStopCause
{
    HudHidden,    // In-Game Only has no eligible foreground game, or visibility
                  // reconciliation paused sampling while the HUD stays enabled.
    Suspend,      // System suspend / missed-suspend resume fallback.
    HudDisabled,  // User explicitly turned Enable HUD off.
    AppShutdown,  // Normal exit, RequestShutdown, or Velopack-driven shutdown.
};

// Whether the elevated EC helper connection should survive this sampling stop.
// Transient gates preserve a healthy helper so normal HUD hide/show and
// suspend/resume never re-prompt for UAC; explicit lifetime boundaries release
// it so the elevated child exits with its private pipe.
enum class EcHelperLifetime
{
    Preserve,
    Release,
};

constexpr EcHelperLifetime EcHelperLifetimeForStop(SamplingStopCause cause) noexcept
{
    switch (cause)
    {
    case SamplingStopCause::HudHidden:
    case SamplingStopCause::Suspend:
        return EcHelperLifetime::Preserve;
    case SamplingStopCause::HudDisabled:
    case SamplingStopCause::AppShutdown:
        return EcHelperLifetime::Release;
    }
    return EcHelperLifetime::Preserve;
}

constexpr const wchar_t* SamplingStopCauseReason(SamplingStopCause cause) noexcept
{
    switch (cause)
    {
    case SamplingStopCause::HudHidden:
        return L"hud-hidden";
    case SamplingStopCause::Suspend:
        return L"suspend";
    case SamplingStopCause::HudDisabled:
        return L"hud-disabled";
    case SamplingStopCause::AppShutdown:
        return L"app-shutdown";
    }
    return L"unknown";
}
}
