#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

namespace clawhud
{
class PresentMonTelemetryProvider;
struct PresentMonDebugFrame;

// Debug-only present/frame observation. When Debug Logging is on, App points
// this at the process it already cares about (foreground / candidate PID) and
// it logs periodic per-frame evidence pulled from the shared PresentMon API2
// session. It never launches PresentMon.exe and never feeds game detection.

// Builds the rate-limited "[PresentActivity]" debug line for one observed frame.
std::wstring FormatPresentActivityLine(DWORD processId,
    const PresentMonDebugFrame& frame);

class PresentActivitySource
{
public:
    PresentActivitySource() = default;
    ~PresentActivitySource();

    PresentActivitySource(const PresentActivitySource&) = delete;
    PresentActivitySource& operator=(const PresentActivitySource&) = delete;

    void Start(PresentMonTelemetryProvider& provider);
    void Stop() noexcept;
    bool Running() const noexcept { return running_.load(); }

    // 0 stops observation; a non-zero PID leases it on the shared session and
    // begins periodic logging.
    void Watch(DWORD processId) noexcept;
    DWORD Watched() const noexcept { return watched_.load(); }

private:
    void PollLoop();

    PresentMonTelemetryProvider* provider_{};
    std::thread worker_;
    std::atomic_bool stop_{};
    std::atomic_bool running_{};
    std::atomic<DWORD> watched_{};
};
}
