#pragma once

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
