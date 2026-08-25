#include "ForegroundTracker.h"

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
    ok &= Check(ForegroundTracker::PidsMatch(42, 42), "matching PIDs");
    ok &= Check(!ForegroundTracker::PidsMatch(42, 43), "mismatched PIDs");
    ok &= Check(!ForegroundTracker::PidsMatch(42, 0), "zero tracked PID");
    ok &= Check(!ForegroundTracker::PidsMatch(0, 42), "zero foreground PID");
    return ok ? 0 : 1;
}
