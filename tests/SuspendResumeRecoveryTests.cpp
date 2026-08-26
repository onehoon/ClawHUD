#include "App.h"

#include <iostream>

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}
}

int main()
{
    bool ok = true;
    ok &= Check(kResumeRecoveryIntervalMs == 500, "recovery uses a 500 ms timer");
    ok &= Check(kResumeRecoveryMaxAttempts == 6, "recovery has six maximum attempts");
    ok &= Check(ResumeRecoveryShouldStart(true, false),
        "first resume starts recovery");
    ok &= Check(!ResumeRecoveryShouldStart(true, true),
        "duplicate resume does not start recovery");
    ok &= Check(ResumeRecoveryHasAttemptsRemaining(5),
        "recovery retries before the limit");
    ok &= Check(!ResumeRecoveryHasAttemptsRemaining(6),
        "recovery stops at the limit");
    ok &= Check(ResumeRecoveryCanRetainPresentMon(42, 42, true),
        "same live PresentMon target is retained");
    ok &= Check(!ResumeRecoveryCanRetainPresentMon(42, 42, false),
        "dead PresentMon target is not retained");
    ok &= Check(!ResumeRecoveryCanRetainPresentMon(42, 43, true),
        "different PresentMon target is not retained");
    return ok ? 0 : 1;
}
