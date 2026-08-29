#pragma once

#include <windows.h>

#include "WindowsGameIdentityProbe.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <system_error>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace clawhud
{
std::wstring EscapeWindowsIdentityDiagnosticValue(std::wstring_view value);

class WindowsGameIdentitySource
{
public:
    WindowsGameIdentitySource();
    ~WindowsGameIdentitySource();

    void QueueInspect(HWND foregroundWindow, DWORD processId) noexcept;
    void Inspect(HWND foregroundWindow, DWORD processId) noexcept;

private:
    struct Request
    {
        std::uint64_t sequence{};
        ULONGLONG eventTickMs{};
        HWND window{};
        DWORD processId{};
    };

    void WorkerMain(std::stop_token stop) noexcept;
    void InspectImpl(HWND foregroundWindow, DWORD processId,
        std::uint64_t sequence, ULONGLONG eventTickMs);
    HWND lastWindow_{};
    DWORD lastProcessId_{};
    std::mutex queueMutex_;
    std::condition_variable_any queueWake_;
    std::deque<Request> pendingRequests_;
    std::atomic_uint64_t nextSequence_{1};
    std::jthread worker_;
    WindowsGameIdentityProbe probe_;
};
}
