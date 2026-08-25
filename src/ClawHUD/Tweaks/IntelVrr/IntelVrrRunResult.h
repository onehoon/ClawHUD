#pragma once

#include <string>

namespace clawhud
{
enum class IntelVrrRunStatus
{
    Disabled, Unavailable, UnsupportedPanel, AmbiguousDisplay, AlreadyCorrect,
    SkippedUserProfile, Applied, ApplyFailed, VerificationFailed
};

struct IntelVrrRunResult
{
    IntelVrrRunStatus status{ IntelVrrRunStatus::Unavailable };
    std::string panelName;
    std::string rangeBefore;
    std::string rangeAfter;
    std::string message;
    std::string timestampUtc;
};

const char* IntelVrrRunStatusName(IntelVrrRunStatus status);
}
