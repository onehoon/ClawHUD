#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace clawhud
{
enum class WindowLifecycleEventType
{
    Create,
    Destroy,
    Show,
    Hide,
    NameChange,
};

std::optional<WindowLifecycleEventType> MapWinEvent(DWORD event) noexcept;
bool IsWindowLifecycleObject(LONG objectId, LONG childId) noexcept;
bool ShouldKeepWindowEvent(bool liveTopLevel, bool immediateTopLevel,
    bool cachedTopLevel) noexcept;
std::wstring WindowLifecycleEventName(WindowLifecycleEventType type);
std::wstring EscapeWindowLifecycleValue(std::wstring_view value);

struct RawWindowLifecycleEvent
{
    std::uint64_t sequence{};
    DWORD event{};
    HWND hwnd{};
    LONG objectId{};
    LONG childId{};
    DWORD eventThreadId{};
    DWORD sourceEventTimeMs{};
    ULONGLONG receivedTickMs{};
    DWORD immediateProcessId{};
    DWORD immediateWindowThreadId{};
    HWND immediateRoot{};
    bool immediateTopLevel{};
};

struct WindowSnapshot
{
    HWND hwnd{};
    DWORD processId{};
    DWORD windowThreadId{};
    std::wstring title;
    std::wstring className;
    LONG_PTR style{};
    LONG_PTR exStyle{};
    bool visible{};
    bool iconic{};
    HWND owner{};
    HWND root{};
    RECT rectangle{};
    bool rectangleAvailable{};
    bool cloaked{};
    bool cloakedAvailable{};
};

class WindowLifecycleCache
{
public:
    static constexpr std::size_t kCapacity = 4096;

    void ReplaceOnCreate(const WindowSnapshot& snapshot);
    void Update(const WindowSnapshot& snapshot);
    std::optional<WindowSnapshot> Find(HWND hwnd) const;
    std::optional<WindowSnapshot> EvictIfOverCapacity();
    void Remove(HWND hwnd);
    void Clear() noexcept;
    std::size_t Size() const noexcept { return snapshots_.size(); }

private:
    std::unordered_map<HWND, WindowSnapshot> snapshots_;
};

class WindowLifecycleQueue
{
public:
    static constexpr std::size_t kCapacity = 4096;

    bool TryPush(RawWindowLifecycleEvent event);
    std::optional<RawWindowLifecycleEvent> TryPop();
    std::uint64_t TakeDroppedCount() noexcept;
    std::size_t Size() const noexcept { return events_.size(); }
    void Clear() noexcept;

private:
    std::deque<RawWindowLifecycleEvent> events_;
    std::uint64_t droppedCount_{};
};

class WindowLifecycleSource
{
public:
    WindowLifecycleSource() = default;
    ~WindowLifecycleSource();

    WindowLifecycleSource(const WindowLifecycleSource&) = delete;
    WindowLifecycleSource& operator=(const WindowLifecycleSource&) = delete;

    bool Start() noexcept;
    void Stop() noexcept;
    bool Running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
        LONG objectId, LONG childId, DWORD eventThreadId, DWORD eventTime);
    void AcceptCallback(DWORD event, HWND hwnd, LONG objectId, LONG childId,
        DWORD eventThreadId, DWORD eventTime) noexcept;
    void WorkerMain(std::stop_token stop) noexcept;
    void ProcessEvent(const RawWindowLifecycleEvent& event) noexcept;
    void LogEvent(const RawWindowLifecycleEvent& event, const WindowSnapshot& snapshot,
        const wchar_t* metadataSource) noexcept;
    void LogDropped(std::uint64_t count) noexcept;
    void LogFailure(const wchar_t* eventName, DWORD error) noexcept;
    static std::optional<WindowSnapshot> CaptureSnapshot(HWND hwnd,
        DWORD fallbackProcessId, DWORD fallbackThreadId) noexcept;
    static bool IsLiveTopLevelWindow(HWND hwnd) noexcept;
    void StopWorker() noexcept;
    void UnhookAll() noexcept;

    std::array<HWINEVENTHOOK, 5> hooks_{};
    std::mutex queueMutex_;
    std::condition_variable_any queueWake_;
    WindowLifecycleQueue pendingEvents_;
    std::jthread worker_;
    std::atomic_bool accepting_{};
    std::atomic_bool running_{};
    std::atomic_uint64_t nextSequence_{1};
    WindowLifecycleCache cache_;

    static std::mutex activeMutex_;
    static WindowLifecycleSource* active_;
};
}
