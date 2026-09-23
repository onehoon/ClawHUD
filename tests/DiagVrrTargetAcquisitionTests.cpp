#ifdef NDEBUG
#undef NDEBUG
#endif

#include "VrrTargetAcquisition.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

int main()
{
    const VrrLaunchContext launchContext{
        reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x1001)), 10 };
    assert(!VrrLaunchContextHasChanged(launchContext,
        launchContext.foregroundWindow, launchContext.foregroundProcessId));
    assert(!VrrLaunchContextHasChanged(launchContext,
        reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x1002)), 10));
    assert(VrrLaunchContextHasChanged(launchContext,
        launchContext.foregroundWindow, 11));

    const VrrProcessIdentity generationA{ 42, 1000, L"C:\\games\\game.exe", L"game.exe" };
    const VrrProcessIdentity sameGeneration{ 42, 1000, L"C:\\other\\renamed.exe", L"renamed.exe" };
    const VrrProcessIdentity generationB{ 42, 1001, L"C:\\games\\game.exe", L"game.exe" };
    assert(SameVrrProcessGeneration(generationA, sameGeneration));
    assert(!SameVrrProcessGeneration(generationA, generationB));
    assert(!SameVrrProcessGeneration(VrrProcessIdentity{}, generationA));

    constexpr std::string_view blocked = "# generated list\r\nexplorer.exe\r\nSteam.exe\r\n";
    assert(VrrExecutableIsBlocked(L"EXPLORER.EXE", blocked));
    assert(VrrExecutableIsBlocked(L"steam.exe", blocked));
    assert(!VrrExecutableIsBlocked(L"MyGame.exe", blocked));
    assert(VrrExecutableIsBlocked(L"", blocked));

    std::vector<VrrFrameSample> samples;
    samples.push_back({ 42, 0x1234, 16.7 });
    assert(VrrHasDisplayedFrameEvidence(samples, 42));

    samples.front().betweenDisplayChangeMs = 0.0;
    assert(!VrrHasDisplayedFrameEvidence(samples, 42));
    samples.front().betweenDisplayChangeMs = std::numeric_limits<double>::quiet_NaN();
    assert(!VrrHasDisplayedFrameEvidence(samples, 42));
    samples.front().betweenDisplayChangeMs = 16.7;
    samples.front().processId = 43;
    assert(!VrrHasDisplayedFrameEvidence(samples, 42));
    samples.front().processId = 42;
    samples.front().swapChainAddress = 0;
    assert(!VrrHasDisplayedFrameEvidence(samples, 42));
}
