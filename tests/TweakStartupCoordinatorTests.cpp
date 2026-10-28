#include "Tweaks/TweakStartupCoordinator.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace clawhud;
using namespace std::chrono_literals;
int main()
{
    bool ok = true; std::vector<std::chrono::seconds> delays; int calls = 0;
    const int attempts = TweakStartupCoordinator::RunRetrySequence([&](int) { ++calls; return IntelVrrRunResult{ calls == 4 ? IntelVrrRunStatus::Applied : IntelVrrRunStatus::Unavailable }; }, [&](auto delay) { delays.push_back(delay); return true; });
    if (attempts != 4 || calls != 4 || delays != std::vector<std::chrono::seconds>{ 2s, 5s, 15s }) { std::cerr << "FAILED: unavailable retry sequence\n"; ok = false; }
    delays.clear(); calls = 0;
    const int stopped = TweakStartupCoordinator::RunRetrySequence([&](int) { ++calls; return IntelVrrRunResult{ IntelVrrRunStatus::AlreadyCorrect }; }, [&](auto delay) { delays.push_back(delay); return true; });
    if (stopped != 1 || calls != 1 || !delays.empty()) { std::cerr << "FAILED: non-unavailable stops retry\n"; ok = false; }

    int drrCalls = 0; int intelCalls = 0; delays.clear();
    const int integratedAttempts = TweakStartupCoordinator::RunStartupSequence(true,
        [&] { ++drrCalls; },
        [&](int) { ++intelCalls; return IntelVrrRunResult{ intelCalls == 4 ? IntelVrrRunStatus::Applied : IntelVrrRunStatus::Unavailable }; },
        [&](auto delay) { delays.push_back(delay); return true; });
    if (integratedAttempts != 4 || drrCalls != 1 || intelCalls != 4 ||
        delays != std::vector<std::chrono::seconds>{ 2s, 5s, 15s })
    { std::cerr << "FAILED: DRR runs once outside Intel retry sequence\n"; ok = false; }

    int drrOnUnsupportedCalls = 0; int unsupportedIntelCalls = 0;
    const int unsupportedAttempts = TweakStartupCoordinator::RunStartupSequence(true,
        [&] { ++drrOnUnsupportedCalls; },
        [&](int) { ++unsupportedIntelCalls; return IntelVrrRunResult{ IntelVrrRunStatus::UnsupportedPanel }; },
        [](auto) { return true; });
    if (unsupportedAttempts != 1 || drrOnUnsupportedCalls != 1 || unsupportedIntelCalls != 1)
    { std::cerr << "FAILED: unsupported Intel panel does not suppress DRR stage\n"; ok = false; }

    int disabledDrrCalls = 0; int disabledIntelCalls = 0; delays.clear();
    const int disabledAttempts = TweakStartupCoordinator::RunStartupSequence(false,
        [&] { ++disabledDrrCalls; },
        [&](int) { ++disabledIntelCalls; return IntelVrrRunResult{ IntelVrrRunStatus::Disabled }; },
        [&](auto delay) { delays.push_back(delay); return true; });
    if (disabledAttempts != 1 || disabledDrrCalls != 0 || disabledIntelCalls != 1 || !delays.empty())
    { std::cerr << "FAILED: disabled VRR Fix performs no DRR attempt\n"; ok = false; }

    int afterDrrFailureCalls = 0;
    const int afterDrrFailure = TweakStartupCoordinator::RunStartupSequence(true,
        [] { throw std::runtime_error("simulated DRR failure"); },
        [&](int) { ++afterDrrFailureCalls; return IntelVrrRunResult{ IntelVrrRunStatus::AlreadyCorrect }; },
        [](auto) { return true; });
    if (afterDrrFailure != 1 || afterDrrFailureCalls != 1)
    { std::cerr << "FAILED: DRR failure does not skip Intel VRR stage\n"; ok = false; }
    return ok ? 0 : 1;
}
