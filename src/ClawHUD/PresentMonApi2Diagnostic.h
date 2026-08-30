#pragma once

#include "PresentMonApi2Api.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

namespace clawhud
{
enum class Api2MetricResult
{
    Working,
    Static,
    ZeroOnly,
    Unavailable,
    Invalid,
    QueryFailed,
    NotApplicable,
};

const char* Api2MetricResultName(Api2MetricResult result) noexcept;
Api2MetricResult ClassifyApi2Metric(bool available, bool querySucceeded,
    bool hasSample, bool hasNonZeroSample, bool dynamic) noexcept;
bool Api2MetricFailureIsNonFatal() noexcept;
bool Api2FrameConsumeZeroIsNonFatal(std::uint32_t frameCount) noexcept;
bool Api2TargetPidIsUsable(DWORD processId, DWORD currentProcessId) noexcept;
PM_DATA_TYPE Api2StaticMetricType(
    PM_DATA_TYPE polledType, PM_DATA_TYPE frameType) noexcept;
PM_DATA_TYPE Api2FrameMetricType(
    PM_DATA_TYPE polledType, PM_DATA_TYPE frameType) noexcept;
bool Api2MetricSupportsFrameQuery(PM_METRIC_TYPE type) noexcept;
std::string Api2DecodeStaticValue(
    const std::uint8_t* blob, PM_DATA_TYPE type);
std::filesystem::path Api2DiagnosticOutputPath(
    const std::filesystem::path& directory, const std::wstring& timestamp,
    const wchar_t* suffix);

class PresentMonApi2Diagnostic
{
public:
    explicit PresentMonApi2Diagnostic(HWND notifyWindow) : notifyWindow_(notifyWindow) {}
    ~PresentMonApi2Diagnostic();

    bool Start();
    void Stop() noexcept;
    bool Running() const noexcept { return running_.load(); }

private:
    void Run();
    void Status(const wchar_t* status) const;
    HWND notifyWindow_{};
    std::atomic_bool stop_{};
    std::atomic_bool running_{};
    std::thread worker_;
};

constexpr UINT kPresentMonApi2DiagnosticStatus = WM_APP + 25;
constexpr UINT kPresentMonApi2DiagnosticCompleted = WM_APP + 26;
}
