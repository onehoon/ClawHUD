#include "A8AdlxPowerSessionState.h"

#include <stdexcept>

int main()
{
    using clawhud::A8AdlxPowerSessionState;
    using clawhud::AdlxPowerStatus;
    A8AdlxPowerSessionState session;
    if (session.NeedsInitialization() || !session.Configure(true) ||
        !session.NeedsInitialization() || session.Configure(false))
        throw std::runtime_error("A8 ADLX model configuration must be set once before sampling");
    session.RecordInitialization(AdlxPowerStatus::RuntimeUnavailable);
    if (session.NeedsInitialization() || session.Ready() ||
        session.Status() != AdlxPowerStatus::RuntimeUnavailable)
        throw std::runtime_error("failed ADLX initialization must not retry in the same lifetime");
    session.EndLifetime();
    if (!session.NeedsInitialization())
        throw std::runtime_error("explicit lifetime release must allow a later initialization");
    session.RecordInitialization(AdlxPowerStatus::Ok);
    if (!session.Ready() || session.NeedsInitialization())
        throw std::runtime_error("healthy ADLX initialization must remain available without reinitializing");
    session.EndLifetime();
    if (!session.NeedsInitialization())
        throw std::runtime_error("explicit disable and re-enable must begin a new reader lifetime");

    A8AdlxPowerSessionState disabled;
    if (!disabled.Configure(false) || disabled.Enabled() || disabled.NeedsInitialization())
        throw std::runtime_error("non-A8 configuration must never initialize ADLX");
}
