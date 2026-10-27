#pragma once

#include "IntelVrrRunResult.h"
#include "DiagIntelVrrStateProbe.h"

#include <string>
#include <string_view>
#include <vector>

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

struct Lg27gs65fVrrFixOutcome
{
    IntelVrrRunResult result;
    std::vector<std::string> log;
};

// Manual Settings toggle controls startup execution; this never changes the HUD.
Lg27gs65fVrrFixOutcome RunLg27gs65fProductionVrrFix();
}
