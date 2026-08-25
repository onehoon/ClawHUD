#include "IntelVrrRunResult.h"
#include <windows.h>
#include <chrono>
#include <ctime>

namespace clawhud
{
const char* IntelVrrRunStatusName(IntelVrrRunStatus status)
{
    switch (status) { case IntelVrrRunStatus::Disabled: return "Disabled"; case IntelVrrRunStatus::Unavailable: return "Unavailable"; case IntelVrrRunStatus::UnsupportedPanel: return "UnsupportedPanel"; case IntelVrrRunStatus::AmbiguousDisplay: return "AmbiguousDisplay"; case IntelVrrRunStatus::AlreadyCorrect: return "AlreadyCorrect"; case IntelVrrRunStatus::SkippedUserProfile: return "SkippedUserProfile"; case IntelVrrRunStatus::Applied: return "Applied"; case IntelVrrRunStatus::ApplyFailed: return "ApplyFailed"; case IntelVrrRunStatus::VerificationFailed: return "VerificationFailed"; default: return "Unknown"; }
}
}
