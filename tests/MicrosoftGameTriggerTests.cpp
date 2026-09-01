#include "GameDetection/MicrosoftGameTrigger.h"

#include <cstdlib>
#include <iostream>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

clawhud::ProductionWindowEvent WindowEvent(
    clawhud::ProductionWindowEventType type, DWORD processId = 6008,
    bool immediateTopLevel = true)
{
    clawhud::ProductionWindowEvent event;
    event.sequence = 123;
    event.type = type;
    event.window = reinterpret_cast<HWND>(0x1234);
    event.processId = processId;
    event.immediateRoot = event.window;
    event.immediateTopLevel = immediateTopLevel;
    return event;
}

clawhud::WindowsGameIdentityProbeResult IdentityResult(
    bool readable, bool evaluated, bool matched)
{
    clawhud::WindowsGameIdentityProbeResult result;
    result.microsoftGameConfigs.push_back({
        1, L"package", L"package\\MicrosoftGame.config", true, true, 0,
        true, readable, 0, {}, evaluated, matched});
    return result;
}

clawhud::ProductionWindowEvent InstanceEvent(
    std::uint64_t sequence, HWND window = reinterpret_cast<HWND>(0x1234))
{
    auto event = WindowEvent(clawhud::ProductionWindowEventType::Show);
    event.sequence = sequence;
    event.window = window;
    return event;
}
}

int main()
{
    using namespace clawhud;
    Check(ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::Create)), "CREATE is eligible");
    Check(ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::Show)), "SHOW is eligible");
    Check(!ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::Hide)), "HIDE is ignored");
    Check(!ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::LocationChange)), "LOCATIONCHANGE is ignored");
    Check(!ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::Destroy)), "DESTROY is ignored");
    Check(!ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::Create, 0)), "zero PID is ignored");
    Check(!ShouldInspectMicrosoftGameWindowEvent(
        WindowEvent(ProductionWindowEventType::Create, 6008, false)),
        "non-top-level event is ignored");
    auto vanished = WindowEvent(ProductionWindowEventType::Create);
    vanished.window = reinterpret_cast<HWND>(0xDEAD);
    Check(ShouldInspectMicrosoftGameWindowEvent(vanished),
        "callback-time top-level evidence does not require a live HWND");

    Check(!ShouldEmitMicrosoftGameTrigger(IdentityResult(false, true, true)),
        "unreadable config is not evidence");
    Check(!ShouldEmitMicrosoftGameTrigger(IdentityResult(true, false, true)),
        "unevaluated match is not evidence");
    Check(!ShouldEmitMicrosoftGameTrigger(IdentityResult(true, true, false)),
        "non-matching executable is not evidence");
    Check(ShouldEmitMicrosoftGameTrigger(IdentityResult(true, true, true)),
        "readable exact executable match is evidence");

    Check(GameProcessInstance{100, 1000} == GameProcessInstance{100, 1000},
        "equal PID and creation time identify one process instance");
    Check(GameProcessInstance{100, 1000} != GameProcessInstance{101, 1000} &&
        GameProcessInstance{100, 1000} != GameProcessInstance{100, 2000},
        "PID or creation-time changes identify a different process instance");
    const auto currentProcessIdentity =
        QueryGameProcessInstance(GetCurrentProcessId());
    Check(currentProcessIdentity &&
        currentProcessIdentity->processId == GetCurrentProcessId() &&
        currentProcessIdentity->creationTime != 0,
        "process identity query reads creation time from the process handle");

    int positiveProbeCalls = 0;
    KnownGameProcessCache positiveCache;
    MicrosoftGameTrigger cached(
        positiveCache,
        [&](DWORD) { ++positiveProbeCalls; return IdentityResult(true, true, true); },
        [](DWORD processId) -> std::optional<GameProcessInstance>
        {
            return GameProcessInstance{processId, 1000};
        });
    const auto firstEvidence = cached.InspectWindowEvent(InstanceEvent(10));
    const auto secondEvidence = cached.InspectWindowEvent(
        InstanceEvent(11, reinterpret_cast<HWND>(0x5678)));
    Check(firstEvidence && secondEvidence && positiveProbeCalls == 1 &&
        secondEvidence->sourceSequence == 11 &&
        secondEvidence->window == reinterpret_cast<HWND>(0x5678),
        "positive process identity cache skips repeated probe and keeps event evidence");
    Check(positiveCache.Lookup({6008, 1000})->microsoftGameIdentity,
        "positive probe stores shared Microsoft evidence");

    int reusedProbeCalls = 0;
    std::uint64_t creationTime = 1000;
    KnownGameProcessCache reusedCache;
    MicrosoftGameTrigger reused(
        reusedCache,
        [&](DWORD) { ++reusedProbeCalls; return IdentityResult(true, true, true); },
        [&](DWORD processId) -> std::optional<GameProcessInstance>
        {
            return GameProcessInstance{processId, creationTime};
        });
    Check(reused.InspectWindowEvent(InstanceEvent(20)) &&
        (creationTime = 2000, reused.InspectWindowEvent(InstanceEvent(21))) &&
        reusedProbeCalls == 2,
        "PID reuse with a different creation time requires a new probe");

    int negativeProbeCalls = 0;
    KnownGameProcessCache negativeCache;
    MicrosoftGameTrigger negative(
        negativeCache,
        [&](DWORD) { ++negativeProbeCalls; return IdentityResult(true, true, false); },
        [](DWORD processId) -> std::optional<GameProcessInstance>
        {
            return GameProcessInstance{processId, 3000};
        });
    Check(!negative.InspectWindowEvent(InstanceEvent(30)) &&
        !negative.InspectWindowEvent(InstanceEvent(31)) &&
        negativeProbeCalls == 2,
        "negative Microsoft identity results are not cached");
    Check(!negativeCache.Lookup({6008, 3000}),
        "negative probe does not create shared evidence");

    int identityFailureProbeCalls = 0;
    KnownGameProcessCache identityFailureCache;
    MicrosoftGameTrigger identityFailure(
        identityFailureCache,
        [&](DWORD) { ++identityFailureProbeCalls; return IdentityResult(true, true, true); },
        [](DWORD) -> std::optional<GameProcessInstance>
        {
            return std::nullopt;
        });
    Check(identityFailure.InspectWindowEvent(InstanceEvent(40)) &&
        identityFailure.InspectWindowEvent(InstanceEvent(41)) &&
        identityFailureProbeCalls == 2,
        "identity query failure falls back without unsafe caching");
    Check(!identityFailureCache.Lookup({6008, 4000}),
        "identity query failure skips shared-cache persistence");

    int rendererOnlyProbeCalls = 0;
    KnownGameProcessCache rendererOnlyCache;
    rendererOnlyCache.MarkRendererVerified({6008, 5000});
    MicrosoftGameTrigger rendererOnly(
        rendererOnlyCache,
        [&](DWORD) { ++rendererOnlyProbeCalls; return IdentityResult(true, true, true); },
        [](DWORD processId) -> std::optional<GameProcessInstance>
        {
            return GameProcessInstance{processId, 5000};
        });
    Check(rendererOnly.InspectWindowEvent(InstanceEvent(50)) &&
        rendererOnlyProbeCalls == 1,
        "renderer-only evidence does not bypass Microsoft probe");

    KnownGameProcessCache sourceCache;
    MicrosoftGameTrigger source(sourceCache);
    Check(!source.InspectWindowEvent(WindowEvent(
        ProductionWindowEventType::Destroy)), "destroy does not probe");
    std::cout << "PASS\n";
}
