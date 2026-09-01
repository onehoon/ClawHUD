#include "GameDetection/GameProcessInstance.h"
#include "GameDetection/KnownGameProcessCache.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
}

int main()
{
    using namespace clawhud;
    const GameProcessInstance first{100, 1000};
    const GameProcessInstance reused{100, 2000};
    const GameProcessInstance second{200, 3000};

    Check(first == GameProcessInstance{100, 1000} &&
        first != reused && first != GameProcessInstance{200, 1000},
        "process instance equality includes PID and creation time");
    Check(!QueryGameProcessInstance(0), "zero PID has no process instance");
    const auto current = QueryGameProcessInstance(GetCurrentProcessId());
    Check(current && current->processId == GetCurrentProcessId() &&
        current->creationTime != 0, "current process instance is queryable");

    KnownGameProcessCache cache;
    Check(!cache.Lookup(first) && !cache.IsKnownGame(first), "empty cache is unknown");

    cache.MarkMicrosoftGame(first);
    auto evidence = cache.Lookup(first);
    Check(evidence && evidence->microsoftGameIdentity && !evidence->rendererVerified &&
        cache.IsKnownGame(first), "Microsoft evidence is known game evidence");

    cache.MarkRendererVerified(first);
    evidence = cache.Lookup(first);
    Check(evidence && evidence->microsoftGameIdentity && evidence->rendererVerified,
        "positive evidence merges within one process generation");

    KnownGameProcessCache steamOnly;
    steamOnly.MarkObservedDuringSteamSession(first);
    evidence = steamOnly.Lookup(first);
    Check(evidence && evidence->observedDuringSteamSession &&
        !evidence->microsoftGameIdentity && !evidence->rendererVerified &&
        !steamOnly.IsKnownGame(first), "Steam context alone is not a game verdict");

    cache.MarkRendererVerified(second);
    Check(cache.IsKnownGame(first) && cache.IsKnownGame(second),
        "multiple known games coexist");

    Check(!cache.Lookup(reused) && !cache.IsKnownGame(reused),
        "PID reuse cannot inherit prior evidence");
    cache.MarkRendererVerified(reused);
    evidence = cache.Lookup(reused);
    Check(evidence && !evidence->microsoftGameIdentity && evidence->rendererVerified,
        "reused PID begins with only new-generation evidence");

    cache.Remove(first);
    Check(cache.Lookup(reused).has_value(), "stale generation remove preserves newer generation");
    cache.Remove(reused);
    Check(!cache.Lookup(reused), "matching generation remove erases entry");

    cache.MarkMicrosoftGame(first);
    cache.MarkRendererVerified(second);
    cache.Clear();
    Check(!cache.Lookup(first) && !cache.Lookup(second), "clear removes all evidence");
    std::cout << "PASS\n";
}
