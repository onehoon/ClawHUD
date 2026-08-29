#pragma once

#include "GameDetectionCoordinator.h"
#include "ProductionGameWindowSource.h"
#include "WindowsGameIdentityProbe.h"

#include <cstdint>
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
    std::optional<MicrosoftGameTriggerEvidence> InspectWindowEvent(
        const ProductionWindowEvent& event) noexcept;

    static GameDetectionTransitionResult ApplyEvidence(
        GameDetectionCoordinator& coordinator,
        const MicrosoftGameTriggerEvidence& evidence) noexcept;

private:
    WindowsGameIdentityProbe probe_;
};
}
