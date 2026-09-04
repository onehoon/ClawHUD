#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace clawhud
{
enum class ProductionWindowEventType
{
    Create,
    Show,
    Hide,
    LocationChange,
    Destroy,
    NameChange
};

std::optional<ProductionWindowEventType> MapProductionWindowEvent(DWORD event) noexcept;
bool IsProductionWindowObject(LONG objectId, LONG childId) noexcept;
bool IsProductionTopLevelObservation(HWND hwnd, HWND immediateRoot) noexcept;

struct ProductionWindowEvent
{
    std::uint64_t sequence{};
    ProductionWindowEventType type{};
    HWND window{};
    DWORD processId{};
    DWORD windowThreadId{};
    HWND immediateRoot{};
    bool immediateTopLevel{};
    DWORD sourceEventTimeMs{};
    ULONGLONG receivedTickMs{};
};

class ProductionWindowEventQueue
{
public:
    static constexpr std::size_t kCapacity = 512;

    bool TryPush(ProductionWindowEvent event);
    std::optional<ProductionWindowEvent> TryPop();
    std::uint64_t TakeDroppedCount() noexcept;
    std::size_t Size() const noexcept { return events_.size(); }
    void Clear() noexcept;

private:
    std::deque<ProductionWindowEvent> events_;
    std::uint64_t droppedCount_{};
};

class ProductionGameWindowSource
{
public:
    using EventCallback = std::function<void(const ProductionWindowEvent&)>;

    ProductionGameWindowSource() = default;
    ~ProductionGameWindowSource();

    ProductionGameWindowSource(const ProductionGameWindowSource&) = delete;
    ProductionGameWindowSource& operator=(const ProductionGameWindowSource&) = delete;

    bool Start(EventCallback callback) noexcept;
    void Stop() noexcept;
    bool Running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
        LONG objectId, LONG childId, DWORD eventThreadId, DWORD eventTime);
    void AcceptCallback(DWORD event, HWND hwnd, LONG objectId, LONG childId,
        DWORD eventThreadId, DWORD eventTime) noexcept;
    void WorkerMain(std::stop_token stop) noexcept;
    void StopWorker() noexcept;
    void UnhookAll() noexcept;

    std::array<HWINEVENTHOOK, 6> hooks_{};
    std::mutex queueMutex_;
    std::condition_variable_any queueWake_;
    ProductionWindowEventQueue pendingEvents_;
    std::jthread worker_;
    EventCallback callback_;
    std::atomic_bool accepting_{};
    std::atomic_bool running_{};
    std::atomic_uint64_t nextSequence_{1};

    static std::mutex activeMutex_;
    static ProductionGameWindowSource* active_;
};
}
