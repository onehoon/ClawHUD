#include "Tweaks/TweakStartupCoordinator.h"
#include <iostream>
#include <vector>
using namespace clawhud;
using namespace std::chrono_literals;
int main()
{
    bool ok = true; std::vector<std::chrono::seconds> delays; int calls = 0;
    const int attempts = TweakStartupCoordinator::RunRetrySequenceForTests([&] { ++calls; return IntelVrrRunResult{ calls == 4 ? IntelVrrRunStatus::Applied : IntelVrrRunStatus::Unavailable }; }, [&](auto delay) { delays.push_back(delay); });
    if (attempts != 4 || calls != 4 || delays != std::vector<std::chrono::seconds>{ 2s, 5s, 15s }) { std::cerr << "FAILED: unavailable retry sequence\n"; ok = false; }
    delays.clear(); calls = 0;
    const int stopped = TweakStartupCoordinator::RunRetrySequenceForTests([&] { ++calls; return IntelVrrRunResult{ IntelVrrRunStatus::AlreadyCorrect }; }, [&](auto delay) { delays.push_back(delay); });
    if (stopped != 1 || calls != 1 || !delays.empty()) { std::cerr << "FAILED: non-unavailable stops retry\n"; ok = false; }
    return ok ? 0 : 1;
}
