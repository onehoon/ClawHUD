#pragma once

#include "DiagIntelVrrStateProbe.h"

#include <cstdint>
#include <string_view>

enum class Lg27gs65fVrrAction
{
    ApplyExcellent,
    RestoreRecommended,
};

enum class Lg27gs65fVrrDecision
{
    Skip,
    ConfirmationRequired,
    Write,
    AlreadyAtTarget,
};

bool IsLg27gs65fIdentity(std::wstring_view manufacturerCode,
    std::wstring_view friendlyName) noexcept;
int ParseLg27gs65fMenuSelection(std::string_view input) noexcept;
bool IsLg27gs65fConfirmationAccepted(std::string_view input) noexcept;
Lg27gs65fVrrDecision EvaluateLg27gs65fAction(Lg27gs65fVrrAction action,
    bool identityVerified, std::uint32_t activePathCount,
    std::uint32_t matchingMonitorCount, bool confirmed,
    const DiagIntelVrrState& state) noexcept;
bool VerifyLg27gs65fAction(Lg27gs65fVrrAction action, bool setterSucceeded,
    bool sameTarget, const DiagIntelVrrState& readback) noexcept;

void RunLg27gs65fVrrProfileCommand();
