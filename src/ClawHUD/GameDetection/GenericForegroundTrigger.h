#pragma once

#include "GameDetectionCoordinator.h"

#include <optional>
#include <string_view>

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

bool IsGenericForegroundImageEligible(std::wstring_view image) noexcept;

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
