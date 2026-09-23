#pragma once

#include "DiagD3dkmtCadenceAnalysis.h"
#include "DisplayPathProbe.h"
#include "VrrAnalysis.h"
#include "VrrTargetAcquisition.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct VrrDiagnosticReportData
{
    VrrLockedTarget target;
    VrrDisplayPath displayPath;
    DiagIntelVrrState igcl;
    VrrAnalysisResult analysis;
    std::vector<std::string> additionalReasons;
    bool displayPathAvailable{};
    std::uint16_t presentMonMajor{};
    std::uint16_t presentMonMinor{};
    std::int64_t qpcFrequency{};
    std::uint64_t measurementStartQpc{};
    double captureDurationSeconds{};
};

struct VrrDiagnosticOutputFiles
{
    std::filesystem::path directory;
    std::filesystem::path report;
    std::filesystem::path framesCsv;
    std::filesystem::path vblankCsv;
};

std::optional<VrrDiagnosticOutputFiles> WriteVrrDiagnosticFiles(
    const std::filesystem::path& baseDirectory,
    const VrrDiagnosticReportData& report,
    std::span<const VrrFrameSample> frames,
    const DiagD3dkmtCadenceCapture& d3dkmt);
