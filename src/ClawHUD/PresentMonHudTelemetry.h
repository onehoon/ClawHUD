#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace clawhud
{
struct PresentMonFrameSample
{
    double msBetweenDisplayChange{};
    std::string frameType;
};

std::optional<PresentMonFrameSample> ParseDisplayedFrame(
    const std::vector<std::string>& headers,
    const std::vector<std::string>& row);
std::optional<double> CalculateDisplayedFps(
    std::size_t displayedFrameCount, double elapsedSeconds);
std::optional<double> CalculateDisplayedFpsFromIntervals(
    const std::vector<double>& displayIntervalsMs);
std::wstring BuildPresentMonCommandLine(const std::wstring& executable,
    DWORD processId, const std::wstring& sessionName);

class PresentMonHudTelemetry
{
public:
    using UpdateCallback = std::function<void(std::optional<double>)>;

    ~PresentMonHudTelemetry();
    bool Start(const std::wstring& executable, DWORD processId, UpdateCallback callback);
    void Stop() noexcept;
    bool Running() const noexcept
    {
        return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }
    const std::wstring& SessionName() const noexcept { return sessionName_; }
    DWORD ExitCode() const noexcept;

private:
    void ReadLoop();
    HANDLE process_{};
    HANDLE output_{};
    std::thread worker_;
    std::atomic_bool stop_{};
    UpdateCallback callback_;
    std::wstring sessionName_;
    DWORD processId_{};
};
}
