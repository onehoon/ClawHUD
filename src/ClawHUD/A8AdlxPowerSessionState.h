#pragma once

#include "AmdAdlxPowerTelemetry.h"

namespace clawhud
{
// Tracks one explicitly owned ADLX reader lifetime. Hidden/suspended sampling
// does not end it; explicit HUD disable or app shutdown does.
class A8AdlxPowerSessionState
{
public:
    bool Configure(bool enabled) noexcept
    {
        if (configured_)
            return false;
        configured_ = true;
        enabled_ = enabled;
        return true;
    }

    bool Enabled() const noexcept { return enabled_; }
    bool NeedsInitialization() const noexcept
    {
        return enabled_ && !initializationAttempted_;
    }
    bool Ready() const noexcept
    {
        return initializationAttempted_ && status_ == AdlxPowerStatus::Ok;
    }
    AdlxPowerStatus Status() const noexcept { return status_; }

    void RecordInitialization(AdlxPowerStatus status) noexcept
    {
        initializationAttempted_ = true;
        status_ = status;
    }

    void EndLifetime() noexcept
    {
        initializationAttempted_ = false;
        status_ = AdlxPowerStatus::InitializationFailed;
    }

private:
    bool configured_{};
    bool enabled_{};
    bool initializationAttempted_{};
    AdlxPowerStatus status_{ AdlxPowerStatus::InitializationFailed };
};
}
