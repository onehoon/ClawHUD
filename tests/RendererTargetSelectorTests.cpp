#include "GlobalPresentMonTelemetry.h"
#include "RendererTargetSelector.h"

#include <cmath>
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

GlobalPresentFrame Frame(DWORD pid, std::uint64_t swapChain,
    double interval, std::uint64_t tick, const wchar_t* app = L"game.exe")
{
    return {pid, app, swapChain, interval, "Application", tick};
}

void Feed(RendererTargetSelector& selector, DWORD pid,
    std::uint64_t swapChain, double fps, std::uint64_t tick = GetTickCount64(),
    const wchar_t* app = L"game.exe")
{
    const double interval = 1000.0 / fps;
    for (int i = 0; i < static_cast<int>(fps); ++i)
        selector.ObserveFrame(Frame(pid, swapChain, interval, tick, app));
}
}

int main()
{
    bool ok = true;
    ok &= Check(!GlobalRendererTelemetryStartAllowed(true, false, false, true, false) &&
        GlobalRendererTelemetryStartAllowed(false, false, false, true, false),
        "stream failure blocks respawn until a new lifecycle clears the gate");

    std::optional<RendererTargetSelection> reported = RendererTargetSelection{
        100, L"game.exe", 60.0, RendererSelectionReason::Highest};
    auto sameTarget = reported;
    sameTarget->fps = 120.0;
    ok &= Check(!RendererTargetSelectionIdentityChanged(reported, sameTarget) &&
        RendererTargetSelectionIdentityChanged(reported, std::nullopt),
        "renderer reporting tracks target transitions without logging FPS buckets");

    const auto command = BuildGlobalPresentMonCommandLine(
        L"C:\\tools\\PresentMon.exe", L"ClawHUD-Renderer");
    ok &= Check(command.find(L"--output_stdout") != std::wstring::npos &&
        command.find(L"--session_name \"ClawHUD-Renderer\"") != std::wstring::npos &&
        command.find(L"--process_id") == std::wstring::npos &&
        command.find(L"--terminate_on_proc_exit") == std::wstring::npos,
        "global command is system-wide and independently named");

    const std::vector<std::string> headers{
        "Application", "ProcessID", "SwapChainAddress", "FrameType",
        "MsBetweenDisplayChange"};
    const std::vector<std::string> row{
        "game.exe", "1234", "0xABC", "Application", "8.33"};
    const auto parsed = ParseGlobalPresentFrame(headers, row, 42);
    ok &= Check(parsed && parsed->processId == 1234 &&
        parsed->swapChain == 0xABC && parsed->application == L"game.exe" &&
        parsed->observedTick == 42, "global frame retains process and swapchain identity");

    RendererTargetSelector highest;
    Feed(highest, 100, 1, 60.0);
    Feed(highest, 200, 1, 120.0);
    ok &= Check(highest.Selection() && highest.Selection()->processId == 200 &&
        highest.Selection()->reason == RendererSelectionReason::Highest,
        "highest FPS eligible renderer is selected");

    RendererTargetSelector foreground;
    foreground.SetForegroundProcess(200);
    Feed(foreground, 100, 1, 120.0);
    Feed(foreground, 200, 1, 45.0);
    ok &= Check(foreground.Selection() && foreground.Selection()->processId == 200 &&
        foreground.Selection()->reason == RendererSelectionReason::Foreground,
        "active foreground renderer beats higher FPS background renderer");

    RendererTargetSelector microsoft;
    microsoft.SetMicrosoftHint(200);
    Feed(microsoft, 100, 1, 120.0);
    Feed(microsoft, 200, 1, 60.0);
    ok &= Check(microsoft.Selection() && microsoft.Selection()->processId == 200 &&
        microsoft.Selection()->reason == RendererSelectionReason::Microsoft,
        "active Microsoft hint wins");

    RendererTargetSelector deadMicrosoft;
    deadMicrosoft.SetMicrosoftHint(200);
    Feed(deadMicrosoft, 100, 1, 60.0);
    ok &= Check(deadMicrosoft.Selection() && deadMicrosoft.Selection()->processId == 100,
        "Microsoft hint without frames does not block fallback");

    RendererTargetSelector steam;
    steam.SetSteamHint(200);
    Feed(steam, 100, 1, 120.0);
    Feed(steam, 200, 1, 60.0);
    ok &= Check(steam.Selection() && steam.Selection()->processId == 200 &&
        steam.Selection()->reason == RendererSelectionReason::Steam,
        "active Steam hint is preferred");
    steam.SetForegroundProcess(100);
    ok &= Check(steam.Selection() && steam.Selection()->processId == 100 &&
        steam.Selection()->reason == RendererSelectionReason::Foreground,
        "foreground renderer beats Steam hint");

    RendererTargetSelector swapchains;
    Feed(swapchains, 100, 1, 60.0);
    Feed(swapchains, 100, 2, 60.0);
    Feed(swapchains, 200, 1, 90.0);
    ok &= Check(swapchains.Selection() && swapchains.Selection()->processId == 200,
        "process FPS uses the highest swapchain instead of summing swapchains");

    RendererTargetSelector rejected;
    Feed(rejected, 300, 1, 144.0, GetTickCount64(), L"steamwebhelper.exe");
    ok &= Check(!rejected.Selection(),
        "centralized production target policy rejects utility renderers");

    RendererTargetSelector stale;
    Feed(stale, 100, 1, 60.0, 1000);
    stale.Reevaluate(3000);
    ok &= Check(!stale.Selection() && !stale.ForegroundHasActiveRenderer(3000),
        "renderer activity becomes stale without a polling loop");

    RendererTargetSelector visibility;
    visibility.SetForegroundProcess(100);
    Feed(visibility, 100, 1, 60.0);
    ok &= Check(visibility.ForegroundHasActiveRenderer(GetTickCount64()),
        "first displayed renderer activity can establish foreground visibility");
    visibility.SetForegroundProcess(200);
    ok &= Check(!visibility.ForegroundHasActiveRenderer(GetTickCount64()),
        "non-rendering foreground hides renderer-based visibility");
    return ok ? 0 : 1;
}
