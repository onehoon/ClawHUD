#include "FpsStaleHold.h"

#include <iostream>
#include <optional>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

constexpr DWORD kPidA = 100;
constexpr DWORD kPidB = 200;

// Valid sample -> displayed FPS is that value.
void ValidSample(bool& ok)
{
    FpsStaleHold hold;
    ok &= Check(hold.Observe(kPidA, 99.0, 0) == 99.0,
        "a valid sample is displayed directly");
}

// A single same-PID miss within the window keeps the last valid value.
void SingleMissWithinWindow(bool& ok)
{
    FpsStaleHold hold;
    hold.Observe(kPidA, 99.0, 0);
    ok &= Check(hold.Observe(kPidA, std::nullopt, 500) == 99.0,
        "a miss 500 ms after a valid sample retains it");
}

// Several misses, all under 2 s, keep retaining the value.
void MultipleMissesUnderWindow(bool& ok)
{
    FpsStaleHold hold;
    hold.Observe(kPidA, 99.0, 0);
    ok &= Check(hold.Observe(kPidA, std::nullopt, 500) == 99.0, "miss @500 retains");
    ok &= Check(hold.Observe(kPidA, std::nullopt, 1000) == 99.0, "miss @1000 retains");
    ok &= Check(hold.Observe(kPidA, std::nullopt, 1500) == 99.0, "miss @1500 retains");
}

// A miss at/after 2 s clears the displayed FPS.
void StaleExpiration(bool& ok)
{
    FpsStaleHold hold;
    hold.Observe(kPidA, 99.0, 0);
    ok &= Check(hold.Observe(kPidA, std::nullopt, 1999) == 99.0,
        "just under 2 s still retains");
    ok &= Check(!hold.Observe(kPidA, std::nullopt, 2000),
        "exactly 2 s of misses hides the FPS");
    ok &= Check(!hold.Observe(kPidA, std::nullopt, 2500),
        "an ancient value does not reappear after expiry");
}

// A fresh valid sample before expiry replaces the retained value immediately
// and restarts the window.
void RecoveryBeforeExpiry(bool& ok)
{
    FpsStaleHold hold;
    hold.Observe(kPidA, 99.0, 0);
    ok &= Check(hold.Observe(kPidA, std::nullopt, 500) == 99.0, "retained during miss");
    ok &= Check(hold.Observe(kPidA, std::nullopt, 1000) == 99.0, "still retained");
    ok &= Check(hold.Observe(kPidA, 98.0, 1500) == 98.0,
        "a fresh sample replaces the retained value");
    ok &= Check(hold.Observe(kPidA, std::nullopt, 3000) == 98.0,
        "the window restarts from the fresh sample");
}

// A PID change bypasses the hold entirely.
void PidTransitionBypassesHold(bool& ok)
{
    FpsStaleHold hold;
    hold.Observe(kPidA, 99.0, 0);
    ok &= Check(!hold.Observe(kPidB, std::nullopt, 200),
        "PID B miss never shows PID A's retained FPS");
    ok &= Check(hold.Observe(kPidB, 60.0, 400) == 60.0,
        "PID B's own valid sample is shown");
    ok &= Check(hold.Observe(kPidB, std::nullopt, 500) == 60.0,
        "PID B then gets its own same-PID hold");
}

// A late poll that switches back to the old PID must not resurrect its value.
void OldPidValueNotResurrected(bool& ok)
{
    FpsStaleHold hold;
    hold.Observe(kPidA, 120.0, 0);
    hold.Observe(kPidB, std::nullopt, 100);
    ok &= Check(!hold.Observe(kPidA, std::nullopt, 200),
        "returning to PID A does not restore its pre-transition FPS");
}

// PID 0 (no target) always hides and clears retained state.
void NoTargetClears(bool& ok)
{
    FpsStaleHold hold;
    hold.Observe(kPidA, 99.0, 0);
    ok &= Check(!hold.Observe(0, std::nullopt, 100), "PID 0 hides the FPS");
    ok &= Check(!hold.Observe(kPidA, std::nullopt, 150),
        "state cleared: PID A miss after a PID-0 tick shows nothing");
}

// Explicit Reset() drops the hold.
void ExplicitReset(bool& ok)
{
    FpsStaleHold hold;
    hold.Observe(kPidA, 99.0, 0);
    hold.Reset();
    ok &= Check(!hold.Observe(kPidA, std::nullopt, 100),
        "Reset() discards the retained FPS");
}
}

int main()
{
    bool ok = true;
    ValidSample(ok);
    SingleMissWithinWindow(ok);
    MultipleMissesUnderWindow(ok);
    StaleExpiration(ok);
    RecoveryBeforeExpiry(ok);
    PidTransitionBypassesHold(ok);
    OldPidValueNotResurrected(ok);
    NoTargetClears(ok);
    ExplicitReset(ok);
    return ok ? 0 : 1;
}
