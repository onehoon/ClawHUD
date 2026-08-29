#pragma once

#include <windows.h>
#include <wbemidl.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <wrl/client.h>

namespace clawhud
{
enum class ProcessLifecycleEventType
{
    Start,
    Stop,
};

struct ProcessLifecycleEvent
{
    std::uint64_t sequence{};
    ProcessLifecycleEventType type{};
    DWORD processId{};
    DWORD parentProcessId{};
    DWORD sessionId{};
    std::wstring processName;
    std::uint64_t sourceTimestamp{};
    ULONGLONG receivedTickMs{};
    std::optional<DWORD> exitStatus;
};

struct ProcessLifecycleTraceFields
{
    std::optional<DWORD> processId;
    std::optional<DWORD> parentProcessId;
    std::optional<DWORD> sessionId;
    std::optional<std::wstring> processName;
    std::optional<std::uint64_t> sourceTimestamp;
    std::optional<DWORD> exitStatus;
};

std::optional<ProcessLifecycleEvent> MapProcessLifecycleTraceEvent(
    ProcessLifecycleEventType type, const ProcessLifecycleTraceFields& fields,
    std::uint64_t sequence, ULONGLONG receivedTickMs);
std::wstring EscapeProcessLifecycleValue(std::wstring_view value);

class ProcessLifecycleSource
{
public:
    ProcessLifecycleSource() = default;
    ~ProcessLifecycleSource();

    ProcessLifecycleSource(const ProcessLifecycleSource&) = delete;
    ProcessLifecycleSource& operator=(const ProcessLifecycleSource&) = delete;

    bool Start() noexcept;
    void Stop() noexcept;
    bool Running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    class EventSink;

    HRESULT StartSubscription(ProcessLifecycleEventType type, EventSink* sink,
        Microsoft::WRL::ComPtr<IWbemObjectSink>& retainedSink) noexcept;
    void HandleEvent(ProcessLifecycleEventType type, IWbemClassObject* object) noexcept;
    void Enqueue(ProcessLifecycleEvent event) noexcept;
    void WorkerMain(std::stop_token stop) noexcept;
    void LogEvent(const ProcessLifecycleEvent& event) noexcept;
    void LogFailure(const wchar_t* operation, const wchar_t* stage, HRESULT hr) noexcept;
    void CleanupWmi() noexcept;

    Microsoft::WRL::ComPtr<IWbemLocator> locator_;
    Microsoft::WRL::ComPtr<IWbemServices> services_;
    Microsoft::WRL::ComPtr<IWbemObjectSink> startSink_;
    Microsoft::WRL::ComPtr<IWbemObjectSink> stopSink_;
    std::mutex queueMutex_;
    std::condition_variable_any queueWake_;
    std::deque<ProcessLifecycleEvent> pendingEvents_;
    std::jthread worker_;
    std::atomic_bool running_{};
    std::atomic_uint64_t nextSequence_{1};
    bool comOwned_{};
};
}
