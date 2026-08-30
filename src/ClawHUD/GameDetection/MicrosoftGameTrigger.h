#pragma once

#include "GameDetectionCoordinator.h"
#include "ProductionGameWindowSource.h"
#include "WindowsGameIdentityProbe.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

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

struct MicrosoftGameProcessIdentity
{
    DWORD processId{};
    ULONGLONG creationTime{};
};

bool IsSameMicrosoftGameProcessInstance(
    const MicrosoftGameProcessIdentity& left,
    const MicrosoftGameProcessIdentity& right) noexcept;
std::optional<MicrosoftGameProcessIdentity>
QueryMicrosoftGameProcessIdentity(DWORD processId) noexcept;

class MicrosoftGameTrigger
{
public:
    using ProbeFunction =
        std::function<WindowsGameIdentityProbeResult(DWORD processId)>;
    using ProcessIdentityQuery =
        std::function<std::optional<MicrosoftGameProcessIdentity>(DWORD processId)>;

    MicrosoftGameTrigger();
    MicrosoftGameTrigger(ProbeFunction probe,
        ProcessIdentityQuery identityQuery);

    std::optional<MicrosoftGameTriggerEvidence> InspectWindowEvent(
        const ProductionWindowEvent& event) noexcept;

    static GameDetectionTransitionResult ApplyEvidence(
        GameDetectionCoordinator& coordinator,
        const MicrosoftGameTriggerEvidence& evidence) noexcept;

private:
    WindowsGameIdentityProbe probe_;
    ProbeFunction probeFunction_;
    ProcessIdentityQuery identityQuery_;
    std::unordered_map<DWORD, ULONGLONG> positiveProcessCache_;
};
}
