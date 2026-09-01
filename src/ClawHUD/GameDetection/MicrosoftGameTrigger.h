#pragma once

#include "GameProcessInstance.h"
#include "KnownGameProcessCache.h"
#include "ProductionGameWindowSource.h"
#include "WindowsGameIdentityProbe.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace clawhud
{
struct MicrosoftGameTriggerEvidence
{
    std::uint64_t sourceSequence{};
    HWND window{};
    DWORD processId{};
};

bool ShouldInspectMicrosoftGameWindowEvent(
    const ProductionWindowEvent& event) noexcept;
bool ShouldEmitMicrosoftGameTrigger(
    const WindowsGameIdentityProbeResult& result) noexcept;

class MicrosoftGameTrigger
{
public:
    using ProbeFunction =
        std::function<WindowsGameIdentityProbeResult(DWORD processId)>;
    using ProcessInstanceQuery =
        std::function<std::optional<GameProcessInstance>(DWORD processId)>;

    explicit MicrosoftGameTrigger(KnownGameProcessCache& knownGames) noexcept;
    MicrosoftGameTrigger(KnownGameProcessCache& knownGames,
        ProbeFunction probe, ProcessInstanceQuery instanceQuery);

    std::optional<MicrosoftGameTriggerEvidence> InspectWindowEvent(
        const ProductionWindowEvent& event) noexcept;

private:
    WindowsGameIdentityProbe probe_;
    KnownGameProcessCache& knownGames_;
    ProbeFunction probeFunction_;
    ProcessInstanceQuery instanceQuery_;
};
}
