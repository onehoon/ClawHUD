#include "FpsStaleHold.h"

namespace clawhud
{
std::optional<double> FpsStaleHold::Observe(DWORD processId,
    std::optional<double> freshFps, ULONGLONG nowMs) noexcept
{
    if (processId == 0)
    {
        Reset();
        return std::nullopt;
    }

    if (processId != processId_)
    {
        // Target changed: the previous PID's FPS never carries across, not
        // even for a single tick.
        processId_ = processId;
        lastValidFps_.reset();
        lastValidTick_ = 0;
    }

    if (freshFps)
    {
        lastValidFps_ = freshFps;
        lastValidTick_ = nowMs;
        return freshFps;
    }

    // Same-PID miss: keep the last valid value briefly.
    if (lastValidFps_ && nowMs - lastValidTick_ < kFpsStaleHoldMs)
        return lastValidFps_;

    // Nothing to hold, or the hold has aged out.
    lastValidFps_.reset();
    lastValidTick_ = 0;
    return std::nullopt;
}

void FpsStaleHold::Reset() noexcept
{
    processId_ = 0;
    lastValidFps_.reset();
    lastValidTick_ = 0;
}
}
