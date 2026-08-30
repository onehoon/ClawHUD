#pragma once

#include "GameDetectionCoordinator.h"

#include <optional>
namespace clawhud
{
struct GenericForegroundObservation
{
    HWND window{};
    DWORD processId{};
};

struct GenericForegroundEvidence
{
    HWND window{};
    DWORD processId{};
};

class GenericForegroundTrigger
{
public:
    std::optional<GenericForegroundEvidence> Inspect(
        HWND window, DWORD processId) const noexcept;

    static GameDetectionTransitionResult ApplyEvidence(
        GameDetectionCoordinator& coordinator,
        const GenericForegroundEvidence& evidence) noexcept;
};
}
