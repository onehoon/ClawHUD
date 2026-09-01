#include "GameDetection/GameRenderVerifier.h"

#include "PresentMonTelemetryProvider.h"

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

GameRenderVerifierEvent Event(DWORD processId, std::uint64_t generation)
{
    return MakeGameRenderVerifierEvent(processId, generation,
        GameRenderVerifierEventType::FirstDisplayedFrame);
}
}

int main()
{
    bool ok = true;

    const auto first = Event(6008, 7);
    ok &= Check(first.processId == 6008 && first.generation == 7 &&
        first.type == GameRenderVerifierEventType::FirstDisplayedFrame,
        "FirstDisplayedFrame keeps PID and generation");

    // An uninitialized provider cannot lease the target, so Start fails cleanly.
    PresentMonTelemetryProvider provider;
    GameRenderVerifier verifier(provider);
    ok &= Check(!verifier.Start(0, 0, {}) && !verifier.Running() &&
        verifier.ProcessId() == 0 && verifier.Generation() == 0,
        "invalid start leaves the verifier stopped");
    ok &= Check(!verifier.Start(1234, 9,
        [](const GameRenderVerifierEvent&) {}) && !verifier.Running(),
        "start fails when the shared session cannot lease the process");
    verifier.Stop();
    ok &= Check(!verifier.Running() && verifier.ProcessId() == 0 &&
        verifier.Generation() == 0, "repeated stop is safe");

    return ok ? 0 : 1;
}
