#ifdef NDEBUG
#undef NDEBUG
#endif

#include "VrrCaptureInvalidation.h"
#include "VrrReportWriter.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace
{
void TestForegroundEpochIsEdgeTriggeredAndPersistent()
{
    VrrForegroundChangeTracker tracker;
    tracker.Reset(100);
    tracker.ObserveForegroundProcess(200);
    assert(!tracker.ForeignForegroundObserved());

    tracker.BeginMeasurementEpoch();
    tracker.ObserveForegroundProcess(0);
    tracker.ObserveForegroundProcess(100);
    assert(!tracker.ForeignForegroundObserved());
    tracker.ObserveForegroundProcess(200);
    tracker.ObserveForegroundProcess(100);
    assert(tracker.ForeignForegroundObserved());

    tracker.EndMeasurementEpoch();
    tracker.Reset(100);
    assert(!tracker.ForeignForegroundObserved());
}

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

void TestReportWriterCreatesCompleteRunFiles()
{
    const auto root = std::filesystem::temp_directory_path() /
        ("ClawHUD-VrrDiagnosticOutputTests-" + std::to_string(GetCurrentProcessId()) +
            "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(root);

    VrrDiagnosticReportData report;
    report.target.process.processId = 44;
    report.target.process.executableName = L"sample-game.exe";
    report.target.window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x1234));
    wcscpy_s(report.target.monitorInfo.szDevice, L"\\\\.\\DISPLAY1");
    report.displayPath.monitorDeviceName = L"\\\\.\\DISPLAY1";
    report.displayPathAvailable = true;
    report.displayPath.targetId = 7;
    report.displayPath.nominalRefreshHz = 120.0;
    report.igcl.mappingStatus = DiagIgclTargetMappingStatus::Unknown;
    report.analysis.status = VrrOverallStatus::Inconclusive;
    report.analysis.confidence = VrrConfidence::Low;
    report.analysis.igcl = VrrIgclClass::Unknown;
    report.analysis.presentation.frameCount = 1;
    report.analysis.presentation.dominantSwapchain = 0xABC;
    report.analysis.presentation.dominantShare = 1.0;
    report.analysis.presentation.pacingMetrics.push_back({
        VrrPacingMetric::BetweenDisplayChange, 1, 16.0, 17.0, 18.0, 18.0 });
    report.analysis.cadence.classification = DiagD3dkmtCadenceClass::Indeterminate;
    report.analysis.cadence.windows.push_back({ 0.0, 1.0, 120, 120.0 });
    report.additionalReasons.push_back("foreground_changed");
    report.presentMonMajor = 3;
    report.presentMonMinor = 4;
    report.qpcFrequency = 1000;
    report.measurementStartQpc = 1000;
    report.captureDurationSeconds = 2.0;

    VrrFrameSample frame;
    frame.processId = 44;
    frame.swapChainAddress = 0xABC;
    frame.presentStartQpc = 1200;
    frame.presentMode = PM_PRESENT_MODE_HARDWARE_INDEPENDENT_FLIP;
    frame.dropped = true;
    frame.betweenDisplayChangeMs = std::numeric_limits<double>::quiet_NaN();

    DiagD3dkmtCadenceCapture capture;
    capture.available = true;
    capture.qpcFrequency = 1000;
    capture.timestamps = { 1050, 1200 };

    const std::vector<VrrFrameSample> frames{ frame };
    const auto first = WriteVrrDiagnosticFiles(root, report, frames, capture);
    const auto second = WriteVrrDiagnosticFiles(root, report, frames, capture);
    assert(first && second);
    assert(first->directory != second->directory);
    assert(first->directory.filename().wstring().starts_with(L"vrr-"));
    assert(std::filesystem::exists(first->report));
    assert(std::filesystem::exists(first->framesCsv));
    assert(std::filesystem::exists(first->vblankCsv));

    const auto summary = ReadText(first->report);
    assert(summary.find("VRR Status                   INCONCLUSIVE") != std::string::npos);
    assert(summary.find("foreground_changed") != std::string::npos);
    assert(summary.find("API                          3.4") != std::string::npos);

    const auto framesCsv = ReadText(first->framesCsv);
    assert(framesCsv.starts_with("ElapsedMs,ProcessId,SwapChainAddress,PresentStartQpc"));
    assert(framesCsv.find("200.000,44,0xABC,1200") != std::string::npos);
    assert(framesCsv.find("n/a") == std::string::npos);

    const auto vblankCsv = ReadText(first->vblankCsv);
    assert(vblankCsv.starts_with("EventIndex,Qpc,ElapsedMs\n"));
    assert(vblankCsv.find("0,1050,50.000") != std::string::npos);

    std::filesystem::remove_all(root);
}
}

int main()
{
    TestForegroundEpochIsEdgeTriggeredAndPersistent();
    TestReportWriterCreatesCompleteRunFiles();
    return 0;
}
