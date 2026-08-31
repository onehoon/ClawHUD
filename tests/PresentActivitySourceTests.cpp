#include "GameDetection/PresentActivitySource.h"

#include "PresentMonDebugFrameTelemetry.h"
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
}

int main()
{
    bool ok = true;

    PresentMonDebugFrame frame;
    frame.swapChainAddress = 0x7FF0ABCD1234ULL;
    frame.presentMode = PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP;
    frame.frameType = PM_FRAME_TYPE_APPLICATION;
    frame.betweenDisplayChangeMs = 8.33;
    const auto line = FormatPresentActivityLine(4321, frame);
    ok &= Check(line.rfind(L"[PresentActivity] pid=4321", 0) == 0,
        "the debug line is prefixed and carries the PID");
    ok &= Check(line.find(L"swapChain=0x00007FF0ABCD1234") != std::wstring::npos,
        "the swap-chain address is rendered as fixed-width hex");
    ok &= Check(line.find(L"presentMode=HardwareIndependentFlip") != std::wstring::npos,
        "the present mode enum is named");
    ok &= Check(line.find(L"frameType=Application") != std::wstring::npos,
        "the frame type enum is named");
    ok &= Check(line.find(L"displayed=1") != std::wstring::npos,
        "a positive display-change interval is reported as displayed");

    PresentMonDebugFrame empty;
    ok &= Check(FormatPresentActivityLine(9, empty) == L"[PresentActivity] pid=9",
        "an evidence-free frame still logs the PID only");

    // Lifecycle against an unready provider: nothing is launched and it is safe.
    PresentMonTelemetryProvider provider;
    PresentActivitySource source;
    source.Start(provider);
    source.Watch(1234);
    source.Watch(0);
    ok &= Check(source.Watched() == 0, "Watch(0) clears the observed PID");
    source.Stop();
    ok &= Check(!source.Running(), "Stop leaves the source idle");
    source.Stop();
    ok &= Check(!source.Running(), "repeated Stop is safe");

    return ok ? 0 : 1;
}
