#pragma once

#include "AffectedPanelDetector.h"
#include "IntelArcSyncClient.h"
#include "IntelVrrRunResult.h"
#include <functional>
#include <memory>
#include <vector>

namespace clawhud
{
class IntelVrrRangeTweak
{
public:
    using ClientFactory = std::function<std::unique_ptr<IIntelArcSyncClient>()>;
    using PanelProvider = std::function<std::vector<PanelIdentity>()>;
    IntelVrrRangeTweak(ClientFactory clientFactory, PanelProvider panelProvider);
    IntelVrrRunResult Run(bool enabled);
    const std::vector<std::string>& LastLog() const { return log_; }

private:
    ClientFactory clientFactory_;
    PanelProvider panelProvider_;
    std::vector<std::string> log_;
    std::size_t loggedCallCount_{};
};
}
