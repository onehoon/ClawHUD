#pragma once

#include "DiagIntelVrrStateProbe.h"

#include <string_view>

namespace clawhud
{
enum class Lg27gs65fProfileDecision
{
    Unavailable,
    Unsupported,
    AlreadyCorrect,
    SkipUserProfile,
    ApplyExcellent,
};

// Verify the exact Windows target identity before any IGCL write.
bool IsLg27gs65fWindowsIdentity(std::wstring_view monitorDevicePath,
    std::wstring_view friendlyName) noexcept;

// Pure eligibility decision, shared with Windows-free policy tests.
Lg27gs65fProfileDecision EvaluateLg27gs65fProductionProfile(
    const DiagIntelVrrState& state) noexcept;
}
