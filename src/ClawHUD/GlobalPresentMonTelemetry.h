#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace clawhud
{
struct GlobalPresentFrame
{
    DWORD processId{};
    std::wstring application;
    std::uint64_t swapChain{};
    double msBetweenDisplayChange{};
    std::string frameType;
    std::uint64_t observedTick{};
};

struct GlobalPresentMonEvent
{
    enum class Type
    {
        DisplayedFrame,
        StreamEnded
    };

    Type type{Type::DisplayedFrame};
    GlobalPresentFrame frame{};
};

constexpr bool GlobalRendererTelemetryStartAllowed(
    bool unavailable, bool suspended, bool diagnosticRunning,
    bool hudEnabled, bool alreadyRunning) noexcept
{
    return !unavailable && !suspended && !diagnosticRunning && hudEnabled &&
        !alreadyRunning;
}

std::optional<GlobalPresentFrame> ParseGlobalPresentFrame(
    const std::vector<std::string>& headers,
    const std::vector<std::string>& row,
    std::uint64_t observedTick);
std::wstring BuildGlobalPresentMonCommandLine(
    const std::wstring& executable, const std::wstring& sessionName);

class GlobalPresentMonTelemetry
{
public:
    using UpdateCallback = std::function<void(const GlobalPresentMonEvent&)>;

    ~GlobalPresentMonTelemetry();
    bool Start(const std::wstring& executable, UpdateCallback callback);
    DWORD Stop() noexcept;
    bool Running() const noexcept
    {
        return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }
    const std::wstring& SessionName() const noexcept { return sessionName_; }

private:
    void ReadLoop();

    HANDLE process_{};
    HANDLE output_{};
    std::thread worker_;
    std::atomic_bool stop_{};
    UpdateCallback callback_;
    std::wstring sessionName_;
};
}
