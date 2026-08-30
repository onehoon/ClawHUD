#include "GameDetectionProbe.h"

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
    ok &= Check(ParseGpuEngineProcessId(L"pid_9124_luid_0x1_eng_0_engtype_3D") == 9124,
        "3D instance PID is parsed");
    ok &= Check(!IsGpuEngine3DInstance(L"pid_9124_luid_0x1_eng_1_engtype_Compute"),
        "compute instance is rejected");
    ok &= Check(PositiveCounterDelta(100.0, 107.0) == 7.0 &&
        PositiveCounterDelta(107.0, 100.0) == 0.0,
        "PDH ranking uses positive two-sample deltas");
    ok &= Check(!ParseGpuEngineProcessId(L"malformed"), "malformed instance is rejected");
    ok &= Check(IsPresentMonCandidateWindow(true, nullptr) &&
        !IsPresentMonCandidateWindow(false, nullptr) &&
        !IsPresentMonCandidateWindow(true, reinterpret_cast<HWND>(1)),
        "candidate windows require visible ownerless top-level windows");

    const std::vector<GameDetectionEngineDelta> engines{
        {100, 4.0}, {100, 3.0}, {200, 11.0}, {300, 0.0} };
    const std::vector<GameDetectionCandidate> windows{
        {100, reinterpret_cast<HWND>(1), L"a.exe", L"A", 0},
        {200, reinterpret_cast<HWND>(2), L"b.exe", L"B", 0},
        {300, reinterpret_cast<HWND>(3), L"c.exe", L"C", 0},
        {400, reinterpret_cast<HWND>(4), L"explorer.exe", L"Explorer", 0},
        {500, reinterpret_cast<HWND>(5), L"adcefwebbrowser.exe", L"CEF", 0} };
    const auto ranked = RankGpuCandidates(
        { {100, 4.0}, {100, 3.0}, {200, 11.0}, {300, 0.0}, {400, 12.0},
          {500, 13.0} }, windows);
    ok &= Check(ranked.size() == 4 && ranked[0].processId == 500 &&
        ranked[0].gpu3dDelta == 13.0 && ranked[1].processId == 400 &&
        ranked[2].processId == 200 && ranked[3].gpu3dDelta == 7.0,
        "multiple 3D engines aggregate and rank");
    const auto blocklistPath = std::filesystem::temp_directory_path() /
        L"ClawHUD.PresentMonAutoTargetBlockListTests.txt";
    {
        std::wofstream output(blocklistPath);
        output << L"steamwebhelper.exe\n" << L"adcefwebbrowser.exe\n"
            << L"explorer.exe\n";
    }
    PresentMonAutoTargetBlocklist blocklist;
    ok &= Check(blocklist.Load(blocklistPath) && blocklist.Contains(L"STEAMWEBHELPER.EXE"),
        "diagnostic blocklist normalizes and loads basenames");
    std::filesystem::remove(blocklistPath);
    const auto parity = FilterPresentMonAutoTargetCandidates(ranked, blocklist);
    ok &= Check(parity.size() == 2 && parity[0].processId == 200 &&
        parity[1].processId == 100,
        "PresentMon parity filter is separate from production target policy");

    const RECT monitor{ 0, 0, 1920, 1200 };
    ok &= Check(IsFullscreenLike(monitor, monitor) &&
        IsFullscreenLike(RECT{ 1, 1, 1919, 1199 }, monitor) &&
        !IsFullscreenLike(RECT{ 10, 10, 1910, 1190 }, monitor) &&
        !IsFullscreenLike(RECT{ 1920, 0, 3840, 1200 }, monitor),
        "fullscreen-like geometry honors tolerance and monitor");
    ok &= Check(FormatProbePid(0) == "n/a" && FormatProbePid(42) == "42" &&
        FormatProbeOptional(std::nullopt) == "n/a" &&
        FormatProbeOptional(12.5) == "12.5",
        "diagnostic optional formatting is explicit");
    return ok ? 0 : 1;
}
