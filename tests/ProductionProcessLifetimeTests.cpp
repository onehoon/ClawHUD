#include "GameDetection/ProductionProcessLifetime.h"

#include <iostream>

using namespace clawhud;

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

    ProductionProcessLifetimeWatcher watcher;
    ok &= Check(!watcher.Arm(0, 1, [](DWORD, std::uint64_t) {}),
        "invalid process cannot arm the watcher");
    ok &= Check(!watcher.Arm(GetCurrentProcessId(), 23, nullptr),
        "a null callback cannot arm the watcher");
    ok &= Check(watcher.Arm(GetCurrentProcessId(), 23,
        [](DWORD, std::uint64_t) {}),
        "watcher arms a retained current-process handle");
    ok &= Check(watcher.Armed(), "watcher reports its active wait");
    watcher.Disarm();
    ok &= Check(!watcher.Armed(), "disarm removes the active wait");
    watcher.Disarm();
    ok &= Check(!watcher.Armed(), "disarm is idempotent");

    return ok ? 0 : 1;
}
