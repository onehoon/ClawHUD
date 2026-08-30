#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
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

enum class PresentMonHudEventType
{
    FirstDisplayedFrame,
    StreamEnded
};

struct PresentMonHudEvent
{
    PresentMonHudEventType type{};
    std::optional<double> displayedFps;
};

class PresentMonFrameAccumulator
{
public:
    std::vector<PresentMonHudEvent> Observe(const PresentMonFrameSample& frame);
    void Reset() noexcept;

private:
    bool firstDisplayedFrameEmitted_{};
};

std::optional<PresentMonFrameSample> ParseDisplayedFrame(
    const std::vector<std::string>& headers,
    const std::vector<std::string>& row);
std::wstring BuildPresentMonCommandLine(const std::wstring& executable,
    DWORD processId, const std::wstring& sessionName);

class PresentMonHudTelemetry
{
public:
    using UpdateCallback = std::function<void(const PresentMonHudEvent&)>;

    ~PresentMonHudTelemetry();
    bool Start(const std::wstring& executable, DWORD processId, UpdateCallback callback);
    DWORD Stop() noexcept;
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
